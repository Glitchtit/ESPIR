#include "espir_device.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"

#include "espir_code.h"
#include "espir_ir.h"
#include "espir_store.h"

static const char *TAG = "espir_dev";

#define ESPIR_ENDPOINT       10
#define COORDINATOR_SHORT    0x0000
#define COORDINATOR_ENDPOINT 1

static espir_device_cfg_t s_cfg;
static SemaphoreHandle_t  s_learn_sem;

/* Custom-cluster attribute backing storage. */
static uint8_t  s_slot_count;
static uint8_t  s_active_learn = ESPIR_SLOT_IDLE;
static uint8_t  s_learn_status = ESPIR_LEARN_IDLE;
static uint8_t  s_last_slot;
static uint8_t  s_last_kind = ESPIR_KIND_RAW;
static uint8_t  s_fw_role;
static uint16_t s_last_carrier = ESPIR_CARRIER_DEFAULT_KHZ;
static uint8_t  s_last_code[256];   /* ZCL octet string: [len][data...], max 255 payload */

static uint8_t s_mfg_buf[32];
static uint8_t s_model_buf[32];

static void *zcl_str(uint8_t *buf, const char *s)
{
    size_t n = strlen(s);
    if (n > 30) n = 30;
    buf[0] = (uint8_t)n;
    memcpy(buf + 1, s, n);
    return buf;
}

/* ---- attribute update + report (call with the Zigbee lock held) ----------- */
static void set_attr(uint16_t id, void *val)
{
    esp_zb_zcl_set_attribute_val(ESPIR_ENDPOINT, ESPIR_CLUSTER_ID,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, id, val, false);
}

static void report_attr(uint16_t attr_id)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {0};
    cmd.zcl_basic_cmd.src_endpoint = ESPIR_ENDPOINT;
    cmd.zcl_basic_cmd.dst_endpoint = COORDINATOR_ENDPOINT;
    cmd.zcl_basic_cmd.dst_addr_u.addr_short = COORDINATOR_SHORT;
    cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd.clusterID = ESPIR_CLUSTER_ID;
    cmd.attributeID = attr_id;
    cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    esp_zb_zcl_report_attr_cmd_req(&cmd);
}

/* ---- command actions (run in the Zigbee task via the action handler) ------- */
static void do_send(uint8_t slot)
{
    espir_code_t code;
    if (espir_store_load(slot, &code) == ESP_OK) {
        esp_err_t err = espir_ir_send(&code);
        if (err != ESP_OK) ESP_LOGW(TAG, "send slot %u failed: %s", slot, esp_err_to_name(err));
        else ESP_LOGI(TAG, "sent slot %u", slot);
    } else {
        ESP_LOGW(TAG, "send: slot %u empty", slot);
    }
}

static void start_learn(uint8_t slot)
{
    if (s_cfg.role != ESPIR_ROLE_MASTER) return;
    s_active_learn = slot;
    s_learn_status = ESPIR_LEARN_WAITING;
    set_attr(ESPIR_ATTR_ACTIVE_LEARN, &s_active_learn);
    set_attr(ESPIR_ATTR_LEARN_STATUS, &s_learn_status);
    report_attr(ESPIR_ATTR_LEARN_STATUS);
    xSemaphoreGive(s_learn_sem);
    ESP_LOGI(TAG, "learn mode for slot %u", slot);
}

static void handle_custom_cmd(const esp_zb_zcl_custom_cluster_command_message_t *msg)
{
    const uint8_t *p = (const uint8_t *)msg->data.value;
    uint16_t n = msg->data.size;
    uint8_t cmd = msg->info.command.id;
    if (!p && n) return;

    switch (cmd) {
    case ESPIR_CMD_LEARN:
        if (n >= 1) start_learn(p[0]);
        break;
    case ESPIR_CMD_SEND:
        if (n >= 1) do_send(p[0]);
        break;
    case ESPIR_CMD_CLEAR:
        if (n >= 1) espir_store_clear(p[0]);
        break;
    case ESPIR_CMD_PROGRAM:                       /* slot,kind,carrier(2),code(octstr) */
        if (n >= 5) {
            uint8_t slot = p[0], kind = p[1];
            uint16_t carrier = p[2] | (p[3] << 8);
            uint8_t clen = p[4];
            if (5 + clen <= n)
                espir_store_program_single(slot, kind, carrier, &p[5], clen);
        }
        break;
    case ESPIR_CMD_PROGRAM_BEGIN:                  /* slot,kind,carrier(2),total(2) */
        if (n >= 6) {
            uint8_t slot = p[0], kind = p[1];
            uint16_t carrier = p[2] | (p[3] << 8);
            uint16_t total = p[4] | (p[5] << 8);
            espir_store_program_begin(slot, kind, carrier, total);
        }
        break;
    case ESPIR_CMD_PROGRAM_CHUNK:                  /* slot,seq,data(octstr) */
        if (n >= 3) {
            uint8_t slot = p[0], seq = p[1], dlen = p[2];
            if (3 + dlen <= n) espir_store_program_chunk(slot, seq, &p[3], dlen);
        }
        break;
    case ESPIR_CMD_PROGRAM_COMMIT:
        if (n >= 1) espir_store_program_commit(p[0]);
        break;
    default:
        ESP_LOGW(TAG, "unknown custom cmd 0x%02x", cmd);
        break;
    }
}

static esp_err_t action_handler(esp_zb_core_action_callback_id_t id, const void *message)
{
    if (id == ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID) {
        handle_custom_cmd((const esp_zb_zcl_custom_cluster_command_message_t *)message);
    }
    return ESP_OK;
}

/* ---- learn task (master): blocking IR capture, then publish results -------- */
static void learn_task(void *arg)
{
    while (1) {
        xSemaphoreTake(s_learn_sem, portMAX_DELAY);
        uint8_t slot = s_active_learn;
        espir_code_t code;
        esp_err_t err = espir_ir_receive(&code, s_cfg.learn_timeout_ms);

        if (err == ESP_OK) {
            espir_code_try_compact(&code);
            espir_store_save(slot, &code);

            uint8_t blob[ESPIR_RAW_MAX_BYTES];
            int blen = espir_code_to_blob(&code, blob, sizeof(blob));
            int clip = (blen > 254) ? 254 : blen;        /* octet string max */
            s_last_code[0] = (uint8_t)clip;
            memcpy(&s_last_code[1], blob, clip);

            s_last_slot = slot;
            s_last_kind = code.kind;
            s_last_carrier = code.carrier_khz;
            s_learn_status = ESPIR_LEARN_CAPTURED;
            ESP_LOGI(TAG, "learned slot %u kind=%s carrier=%u (blob %d B%s)",
                     slot, code.kind == ESPIR_KIND_NEC ? "NEC" : "RAW",
                     code.carrier_khz, blen, blen > 254 ? ", clipped for report" : "");
        } else {
            s_learn_status = ESPIR_LEARN_FAILED;
            ESP_LOGW(TAG, "learn slot %u failed: %s", slot, esp_err_to_name(err));
        }
        s_active_learn = ESPIR_SLOT_IDLE;

        if (esp_zb_lock_acquire(portMAX_DELAY)) {
            if (s_learn_status == ESPIR_LEARN_CAPTURED) {
                set_attr(ESPIR_ATTR_LAST_SLOT, &s_last_slot);
                set_attr(ESPIR_ATTR_LAST_KIND, &s_last_kind);
                set_attr(ESPIR_ATTR_LAST_CARRIER, &s_last_carrier);
                set_attr(ESPIR_ATTR_LAST_CODE, s_last_code);
                report_attr(ESPIR_ATTR_LAST_SLOT);
                report_attr(ESPIR_ATTR_LAST_KIND);
                report_attr(ESPIR_ATTR_LAST_CARRIER);
                report_attr(ESPIR_ATTR_LAST_CODE);
            }
            set_attr(ESPIR_ATTR_ACTIVE_LEARN, &s_active_learn);
            set_attr(ESPIR_ATTR_LEARN_STATUS, &s_learn_status);
            report_attr(ESPIR_ATTR_LEARN_STATUS);
            esp_zb_lock_release();
        }
    }
}

/* ---- endpoint construction ------------------------------------------------ */
static esp_zb_ep_list_t *build_endpoint(void)
{
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = (s_cfg.role == ESPIR_ROLE_SLAVE) ? 0x03 : 0x01,  /* battery / mains */
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                  zcl_str(s_mfg_buf, s_cfg.manufacturer));
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                  zcl_str(s_model_buf, s_cfg.model));

    esp_zb_identify_cluster_cfg_t id_cfg = {.identify_time = 0};
    esp_zb_attribute_list_t *identify = esp_zb_identify_cluster_create(&id_cfg);

    esp_zb_attribute_list_t *cust = esp_zb_zcl_attr_list_create(ESPIR_CLUSTER_ID);
    const uint8_t ro = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY;
    const uint8_t ro_rep = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
    const uint8_t rw = ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE;
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_SLOT_COUNT,   ESP_ZB_ZCL_ATTR_TYPE_U8,           ro,     &s_slot_count);
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_ACTIVE_LEARN, ESP_ZB_ZCL_ATTR_TYPE_U8,           rw,     &s_active_learn);
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_LEARN_STATUS, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,    ro_rep, &s_learn_status);
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_LAST_SLOT,    ESP_ZB_ZCL_ATTR_TYPE_U8,           ro_rep, &s_last_slot);
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_LAST_CODE,    ESP_ZB_ZCL_ATTR_TYPE_OCTET_STRING, ro_rep, s_last_code);
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_LAST_KIND,    ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,    ro_rep, &s_last_kind);
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_FW_ROLE,      ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,    ro,     &s_fw_role);
    esp_zb_custom_cluster_add_custom_attr(cust, ESPIR_ATTR_LAST_CARRIER, ESP_ZB_ZCL_ATTR_TYPE_U16,          ro_rep, &s_last_carrier);

    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cl, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_custom_cluster(cl, cust, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_ep_list_t *ep = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t epc = {
        .endpoint = ESPIR_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_TEST_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep, cl, epc);
    return ep;
}

/* ---- commissioning ------------------------------------------------------- */
static void bdb_retry_cb(uint8_t mode)
{
    esp_zb_bdb_start_top_level_commissioning(mode);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p = signal_struct->p_app_signal;
    esp_err_t st = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig = *p;

    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (st == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "factory new — start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "rejoined existing network");
            }
        } else {
            ESP_LOGW(TAG, "stack start failed: %s", esp_err_to_name(st));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (st == ESP_OK) {
            ESP_LOGI(TAG, "joined network (PAN 0x%04hx, ch %d)",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
        } else {
            ESP_LOGW(TAG, "steering failed (%s), retrying", esp_err_to_name(st));
            esp_zb_scheduler_alarm(bdb_retry_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "zdo signal 0x%x, status %s", sig, esp_err_to_name(st));
        break;
    }
}

static void esp_zb_task(void *arg)
{
    esp_zb_cfg_t zb_cfg;
    if (s_cfg.role == ESPIR_ROLE_MASTER) {
        zb_cfg = (esp_zb_cfg_t){
            .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
            .install_code_policy = false,
            .nwk_cfg.zczr_cfg = {.max_children = 10},
        };
    } else {
#if CONFIG_ZB_ZED
        esp_zb_sleep_enable(true);  /* sleep API only exists in the end-device lib variant */
#endif
        zb_cfg = (esp_zb_cfg_t){
            .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
            .install_code_policy = false,
            .nwk_cfg.zed_cfg = {.ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN, .keep_alive = 3000},
        };
    }
    esp_zb_init(&zb_cfg);
    esp_zb_device_register(build_endpoint());
    esp_zb_core_action_handler_register(action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void espir_device_start(const espir_device_cfg_t *cfg)
{
    s_cfg = *cfg;
    s_slot_count = (uint8_t)espir_store_count();
    s_fw_role = (uint8_t)cfg->role;
    s_last_code[0] = 0;

    s_learn_sem = xSemaphoreCreateBinary();

    esp_zb_platform_config_t pc = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&pc));

    if (cfg->role == ESPIR_ROLE_MASTER) {
        xTaskCreate(learn_task, "espir_learn", 4096, NULL, 5, NULL);
    }
    xTaskCreate(esp_zb_task, "esp_zb", 8192, NULL, 5, NULL);
}
