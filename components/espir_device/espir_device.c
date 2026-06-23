#include "espir_device.h"
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_power_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "espir_code.h"
#include "espir_ir.h"
#include "espir_store.h"

static const char *TAG = "espir_dev";

#define ESPIR_ENDPOINT       10
#define ESPIR_LAST_CODE_MAX  48   /* max last_code bytes that fit one ZCL report frame */
#define ESPIR_SLEEP_GRACE_MS 12000 /* stay awake this long after boot so USB-JTAG flashing works */
#define COORDINATOR_SHORT    0x0000
#define COORDINATOR_ENDPOINT 1

static espir_device_cfg_t s_cfg;
static SemaphoreHandle_t  s_learn_sem;
static QueueHandle_t      s_send_q;   /* slot numbers to transmit, off the Zigbee task */
static volatile bool s_joined;   /* sleepy ED: only sleep once joined (commissioning must stay awake) */

/* Custom-cluster attribute backing storage. */
static uint8_t  s_slot_count;
static uint8_t  s_active_learn = ESPIR_SLOT_IDLE;
static uint8_t  s_learn_status = ESPIR_LEARN_IDLE;
static uint8_t  s_last_slot;
static uint8_t  s_last_kind = ESPIR_KIND_RAW;
static uint8_t  s_fw_role;
static uint16_t s_last_carrier = ESPIR_CARRIER_DEFAULT_KHZ;
static uint8_t  s_last_code[256];   /* ZCL octet string: [len][data...], max 255 payload */

/* Power Config cluster backing (slave/battery). ZCL units: voltage=100mV, percent=0.5%. */
static uint8_t  s_batt_voltage = 0xFF;   /* 0xFF = unknown until first sample */
static uint8_t  s_batt_percent = 0xFF;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;
static bool s_adc_ready;
static bool s_batt_started;
static void battery_cb(uint8_t param);
static void battery_adc_init(int gpio);

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
    esp_zb_zcl_set_manufacturer_attribute_val(ESPIR_ENDPOINT, ESPIR_CLUSTER_ID,
                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                              ESPIR_MANUF_CODE, id, val, false);
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
    cmd.manuf_specific = 1;
    cmd.manuf_code = ESPIR_MANUF_CODE;
    esp_err_t e = esp_zb_zcl_report_attr_cmd_req(&cmd);
    if (e != ESP_OK) ESP_LOGW(TAG, "report attr 0x%04x -> %s", attr_id, esp_err_to_name(e));
}

/* Push every attribute to the coordinator so Z2M shows real values instead of "Null".
 * Called shortly after (re)joining; runs in the Zigbee task context. */
static void report_all(void)
{
    ESP_LOGI(TAG, "reporting all attributes to coordinator");
    set_attr(ESPIR_ATTR_SLOT_COUNT, &s_slot_count);
    set_attr(ESPIR_ATTR_FW_ROLE, &s_fw_role);
    report_attr(ESPIR_ATTR_SLOT_COUNT);
    report_attr(ESPIR_ATTR_FW_ROLE);
    report_attr(ESPIR_ATTR_LEARN_STATUS);
    report_attr(ESPIR_ATTR_LAST_SLOT);
    report_attr(ESPIR_ATTR_LAST_KIND);
    report_attr(ESPIR_ATTR_LAST_CARRIER);
    report_attr(ESPIR_ATTR_LAST_CODE);
    if (s_cfg.battery && !s_batt_started) {
        s_batt_started = true;                       /* kick the periodic battery chain once joined */
        esp_zb_scheduler_alarm(battery_cb, 0, 1000);
    }
}

static void report_all_cb(uint8_t param)
{
    (void)param;
    report_all();
}

/* ---- battery (slave): read the BAT+ ÷2 divider and report over the Power Config cluster ---- */
#define ESPIR_BATT_PERIOD_MS  600000   /* re-sample + report every 10 min */
#define ESPIR_BATT_MV_EMPTY   3300     /* LiPo ~0% (brown-out margin) */
#define ESPIR_BATT_MV_FULL    4200     /* LiPo 100% */

static void report_std_attr(uint16_t cluster_id, uint16_t attr_id)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {0};
    cmd.zcl_basic_cmd.src_endpoint = ESPIR_ENDPOINT;
    cmd.zcl_basic_cmd.dst_endpoint = COORDINATOR_ENDPOINT;
    cmd.zcl_basic_cmd.dst_addr_u.addr_short = COORDINATOR_SHORT;
    cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd.clusterID = cluster_id;
    cmd.attributeID = attr_id;
    cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    esp_zb_zcl_report_attr_cmd_req(&cmd);
}

static void battery_adc_init(int gpio)
{
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_config_channel(s_adc, (adc_channel_t)gpio, &ccfg) != ESP_OK) return;  /* C6: ADC1 ch == GPIO0..6 */
    adc_cali_curve_fitting_config_t cal = { .unit_id = ADC_UNIT_1, .chan = (adc_channel_t)gpio,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_adc_cali) != ESP_OK) return;
    s_adc_ready = true;
}

static void battery_sample(void)
{
    if (!s_adc_ready) return;
    int acc = 0, n = 0;
    for (int i = 0; i < 16; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, (adc_channel_t)s_cfg.battery_adc_gpio, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) != ESP_OK) continue;
        acc += mv; n++;
    }
    if (!n) return;
    int div = s_cfg.battery_div_x100 ? s_cfg.battery_div_x100 : 200;   /* default ÷2 */
    int vbat_mv = (acc / n) * div / 100;   /* undo the resistor divider */
    int pct = (vbat_mv - ESPIR_BATT_MV_EMPTY) * 100 / (ESPIR_BATT_MV_FULL - ESPIR_BATT_MV_EMPTY);
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    s_batt_voltage = (uint8_t)((vbat_mv + 50) / 100);   /* 100mV units */
    s_batt_percent = (uint8_t)(pct * 2);                /* 0.5% units */
    esp_zb_zcl_set_attribute_val(ESPIR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &s_batt_voltage, false);
    esp_zb_zcl_set_attribute_val(ESPIR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &s_batt_percent, false);
    report_std_attr(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID);
    report_std_attr(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID);
    ESP_LOGI(TAG, "battery %d mV -> %d%%", vbat_mv, pct);
}

static void battery_cb(uint8_t param)
{
    (void)param;
    battery_sample();
    esp_zb_scheduler_alarm(battery_cb, 0, ESPIR_BATT_PERIOD_MS);   /* periodic */
}

/* ---- transmit (runs in its own task so a held send never blocks the Zigbee stack) ---- */
#define ESPIR_SEND_GAP_MS 40   /* inter-frame gap; ~frame+gap ≈ a real remote's repeat period */

static void send_worker(uint8_t slot)
{
    static espir_code_t code;   /* ~1 KB — keep off the task stack */
    if (espir_store_load(slot, &code) != ESP_OK) {
        ESP_LOGW(TAG, "send: slot %u empty", slot);
        return;
    }
    /* Repeat the frame for send_hold_ms to mimic holding the remote key (many appliances need
     * more than one frame to react). Both roles transmit via the SZHJW on the RMT peripheral. */
    TickType_t start = xTaskGetTickCount();
    TickType_t hold  = pdMS_TO_TICKS(s_cfg.send_hold_ms);
    int frames = 0;
    esp_err_t err;
    do {
        err = espir_ir_send(&code);
        if (err != ESP_OK) break;
        frames++;
        if ((xTaskGetTickCount() - start) >= hold) break;
        vTaskDelay(pdMS_TO_TICKS(ESPIR_SEND_GAP_MS));
    } while ((xTaskGetTickCount() - start) < hold);
    if (err != ESP_OK) ESP_LOGW(TAG, "send slot %u failed: %s", slot, esp_err_to_name(err));
    else ESP_LOGI(TAG, "sent slot %u (%d frames over %ums)", slot, frames, (unsigned)s_cfg.send_hold_ms);
}

static void send_task(void *arg)
{
    (void)arg;
    uint8_t slot;
    for (;;) {
        if (xQueueReceive(s_send_q, &slot, portMAX_DELAY) == pdTRUE) send_worker(slot);
    }
}

/* Queue a send from the Zigbee task — non-blocking, so the stack keeps running. */
static void do_send(uint8_t slot)
{
    if (s_send_q) xQueueSend(s_send_q, &slot, 0);
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

/* ---- master -> peer replication: push a stored slot to a slave over Zigbee ----------
 * Reuses the slave's program_begin/chunk/commit handler. Chunked, so it carries codes of
 * any length (long raw included) without the single-frame limit that blocks last_code. */
#define ESPIR_TX_CHUNK 56

static void send_custom_to(esp_zb_ieee_addr_t dst, uint8_t cmd_id, uint8_t *payload, uint16_t len)
{
    esp_zb_zcl_custom_cluster_cmd_t req = {0};
    memcpy(req.zcl_basic_cmd.dst_addr_u.addr_long, dst, sizeof(esp_zb_ieee_addr_t));
    req.zcl_basic_cmd.dst_endpoint = ESPIR_ENDPOINT;
    req.zcl_basic_cmd.src_endpoint = ESPIR_ENDPOINT;
    req.address_mode = ESP_ZB_APS_ADDR_MODE_64_ENDP_PRESENT;
    req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    req.cluster_id = ESPIR_CLUSTER_ID;
    req.custom_cmd_id = cmd_id;
    req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV;
    req.manuf_specific = 1;
    req.manuf_code = ESPIR_MANUF_CODE;
    req.data.type = ESP_ZB_ZCL_ATTR_TYPE_SET;   /* raw bytes, verbatim */
    req.data.size = len;
    req.data.value = payload;
    esp_zb_zcl_custom_cluster_cmd_req(&req);
}

static void replicate_slot_to(esp_zb_ieee_addr_t dst, uint8_t slot)
{
    static espir_code_t code;
    if (espir_store_load(slot, &code) != ESP_OK) return;   /* empty slot — skip */
    static uint8_t blob[ESPIR_RAW_MAX_BYTES];
    int blen = espir_code_to_blob(&code, blob, sizeof(blob));
    if (blen < 0) return;
    uint8_t buf[8 + ESPIR_TX_CHUNK];
    uint16_t carrier = code.carrier_khz;
    if (blen <= ESPIR_TX_CHUNK) {
        buf[0] = slot; buf[1] = code.kind; buf[2] = carrier & 0xff; buf[3] = carrier >> 8;
        buf[4] = (uint8_t)blen; memcpy(buf + 5, blob, blen);
        send_custom_to(dst, ESPIR_CMD_PROGRAM, buf, 5 + blen);
    } else {
        buf[0] = slot; buf[1] = code.kind; buf[2] = carrier & 0xff; buf[3] = carrier >> 8;
        buf[4] = blen & 0xff; buf[5] = (blen >> 8) & 0xff;
        send_custom_to(dst, ESPIR_CMD_PROGRAM_BEGIN, buf, 6);
        for (int off = 0, seq = 0; off < blen; off += ESPIR_TX_CHUNK, seq++) {
            int n = blen - off; if (n > ESPIR_TX_CHUNK) n = ESPIR_TX_CHUNK;
            buf[0] = slot; buf[1] = (uint8_t)seq; buf[2] = (uint8_t)n; memcpy(buf + 3, blob + off, n);
            send_custom_to(dst, ESPIR_CMD_PROGRAM_CHUNK, buf, 3 + n);
        }
        buf[0] = slot;
        send_custom_to(dst, ESPIR_CMD_PROGRAM_COMMIT, buf, 1);
    }
    ESP_LOGI(TAG, "replicated slot %u (%d B, %s) to peer", slot, blen,
             code.kind == ESPIR_KIND_NEC ? "NEC" : "RAW");
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
    case ESPIR_CMD_COPY_TO:                        /* slot, ieee(8 LE) */
        if (n >= 9) {
            esp_zb_ieee_addr_t dst;
            memcpy(dst, &p[1], sizeof(esp_zb_ieee_addr_t));
            replicate_slot_to(dst, p[0]);
        }
        break;
    case ESPIR_CMD_COPY_ALL:                       /* ieee(8 LE) */
        if (n >= 8) {
            esp_zb_ieee_addr_t dst;
            memcpy(dst, &p[0], sizeof(esp_zb_ieee_addr_t));
            for (uint8_t s = 0; s < (uint8_t)espir_store_count(); s++) replicate_slot_to(dst, s);
        }
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
        if (slot >= (uint8_t)espir_store_count()) continue;  /* ignore spurious/duplicate triggers */

        /* Master learns by raw-capturing the envelope from the VS1838B receiver (RMT RX),
         * then compacting to NEC when the capture decodes. Learns any protocol. */
        static espir_code_t code;   /* ~1 KB — keep off the learn-task stack */
        esp_err_t err = espir_ir_receive(&code, s_cfg.learn_timeout_ms);
        if (err == ESP_OK) {
            espir_code_try_compact(&code);
            espir_store_save(slot, &code);

            /* last_code is a reportable attribute, so it must fit one ZCL frame. NEC (4 B) and
             * short raw codes are exposed for slave replication; longer raw codes (e.g. a
             * Samsung TV frame, ~134 B) would overflow the frame and crash the stack, so we
             * leave last_code empty for those — the code is still stored and sendable. */
            static uint8_t blob[ESPIR_RAW_MAX_BYTES];
            int blen = espir_code_to_blob(&code, blob, sizeof(blob));
            int clip = (blen <= ESPIR_LAST_CODE_MAX) ? blen : 0;
            s_last_code[0] = (uint8_t)clip;
            if (clip) memcpy(&s_last_code[1], blob, clip);

            s_last_slot = slot;
            s_last_kind = code.kind;
            s_last_carrier = code.carrier_khz;
            s_learn_status = ESPIR_LEARN_CAPTURED;
            ESP_LOGI(TAG, "learned slot %u kind=%s carrier=%u (blob %d B%s)",
                     slot, code.kind == ESPIR_KIND_NEC ? "NEC" : "RAW",
                     code.carrier_khz, blen, clip ? "" : ", too long to expose in last_code");
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

    /* Manufacturer-specific attributes (see ESPIR_MANUF_CODE) so Z2M can resolve the
     * 0xFC00 cluster on inbound reports. */
    esp_zb_attribute_list_t *cust = esp_zb_zcl_attr_list_create(ESPIR_CLUSTER_ID);
    const uint16_t mc = ESPIR_MANUF_CODE;
    const uint8_t ro_rep = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
    const uint8_t rw = ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE;
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_SLOT_COUNT,   mc, ESP_ZB_ZCL_ATTR_TYPE_U8,           ro_rep, &s_slot_count);
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_ACTIVE_LEARN, mc, ESP_ZB_ZCL_ATTR_TYPE_U8,           rw,     &s_active_learn);
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_LEARN_STATUS, mc, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,    ro_rep, &s_learn_status);
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_LAST_SLOT,    mc, ESP_ZB_ZCL_ATTR_TYPE_U8,           ro_rep, &s_last_slot);
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_LAST_CODE,    mc, ESP_ZB_ZCL_ATTR_TYPE_OCTET_STRING, ro_rep, s_last_code);
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_LAST_KIND,    mc, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,    ro_rep, &s_last_kind);
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_FW_ROLE,      mc, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,    ro_rep, &s_fw_role);
    esp_zb_cluster_add_manufacturer_attr(cust, ESPIR_CLUSTER_ID, ESPIR_ATTR_LAST_CARRIER, mc, ESP_ZB_ZCL_ATTR_TYPE_U16,          ro_rep, &s_last_carrier);

    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cl, identify, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_custom_cluster(cl, cust, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    if (s_cfg.battery) {   /* LiPo level via the standard Power Configuration cluster */
        esp_zb_power_config_cluster_cfg_t pcfg = {0};
        esp_zb_attribute_list_t *power = esp_zb_power_config_cluster_create(&pcfg);
        esp_zb_power_config_cluster_add_attr(power, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &s_batt_voltage);
        esp_zb_power_config_cluster_add_attr(power, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &s_batt_percent);
        esp_zb_cluster_list_add_power_config_cluster(cl, power, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    }

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
    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
#if CONFIG_PM_ENABLE
        /* Sleep only after joining AND after a boot grace window — light sleep drops the native
         * USB-Serial-JTAG, so the grace period keeps a flash/console window open every boot. */
        if (s_joined && xTaskGetTickCount() >= pdMS_TO_TICKS(ESPIR_SLEEP_GRACE_MS)) esp_zb_sleep_now();
#endif
        break;
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
                s_joined = true;
                esp_zb_scheduler_alarm(report_all_cb, 0, 2000);
            }
        } else {
            ESP_LOGW(TAG, "stack start failed: %s", esp_err_to_name(st));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (st == ESP_OK) {
            ESP_LOGI(TAG, "joined network (PAN 0x%04hx, ch %d)",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            s_joined = true;
            esp_zb_scheduler_alarm(report_all_cb, 0, 2000);
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
#if CONFIG_PM_ENABLE
        esp_zb_sleep_enable(true);  /* sleepy end device (battery). Sleep API is ED-lib only. */
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
    s_send_q = xQueueCreate(4, sizeof(uint8_t));
    xTaskCreate(send_task, "espir_send", 4096, NULL, 5, NULL);
    if (cfg->battery) battery_adc_init(cfg->battery_adc_gpio);

    esp_zb_platform_config_t pc = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&pc));

    if (cfg->role == ESPIR_ROLE_MASTER) {
        xTaskCreate(learn_task, "espir_learn", 6144, NULL, 5, NULL);
    }
    xTaskCreate(esp_zb_task, "esp_zb", 8192, NULL, 5, NULL);
}
