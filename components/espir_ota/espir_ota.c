#include "espir_ota.h"
#include "espir_proto.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_zigbee_core.h"

static const char *TAG = "espir-ota";

/* Hardware version reported to the OTA server (informational). 0x0101 = hw 1.1. */
#define ESPIR_OTA_HW_VERSION         0x0101
/* Minutes between automatic image queries once joined. */
#define ESPIR_OTA_QUERY_INTERVAL_MIN 60
/* Max OTA payload per image block (ZCL frame budget). */
#define ESPIR_OTA_MAX_DATA_SIZE      223

static const esp_partition_t *s_part;
static esp_ota_handle_t        s_handle;
static bool                    s_in_progress;

esp_zb_attribute_list_t *espir_ota_cluster_create(void)
{
    esp_zb_ota_cluster_cfg_t ota_cfg = {
        .ota_upgrade_file_version        = ESPIR_FW_VERSION,
        .ota_upgrade_manufacturer        = ESPIR_MANUF_CODE,
        .ota_upgrade_image_type          = ESPIR_OTA_IMAGE_TYPE,
        .ota_upgrade_downloaded_file_ver = ESPIR_FW_VERSION,
    };
    esp_zb_attribute_list_t *ota = esp_zb_ota_cluster_create(&ota_cfg);

    esp_zb_zcl_ota_upgrade_client_variable_t cli = {
        .timer_query   = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version    = ESPIR_OTA_HW_VERSION,
        .max_data_size = ESPIR_OTA_MAX_DATA_SIZE,
    };
    esp_zb_ota_cluster_add_attr(ota, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, &cli);
    return ota;
}

void espir_ota_start(uint8_t endpoint)
{
    esp_zb_ota_upgrade_client_query_interval_set(endpoint, ESPIR_OTA_QUERY_INTERVAL_MIN);
    ESP_LOGI(TAG, "OTA client active on ep %u (auto-query every %u min)",
             endpoint, ESPIR_OTA_QUERY_INTERVAL_MIN);
}

esp_err_t espir_ota_handle_value(const void *message)
{
    const esp_zb_zcl_ota_upgrade_value_message_t *m = message;
    esp_err_t ret = ESP_OK;

    switch (m->upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        s_part = esp_ota_get_next_update_partition(NULL);
        if (!s_part) { ESP_LOGE(TAG, "no free OTA partition"); return ESP_FAIL; }
        ret = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
        s_in_progress = (ret == ESP_OK);
        ESP_LOGI(TAG, "OTA start -> %s (%s)", s_part->label, esp_err_to_name(ret));
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        if (s_in_progress && m->payload_size && m->payload)
            ret = esp_ota_write(s_handle, m->payload, m->payload_size);
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
        if (s_in_progress) {
            ret = esp_ota_end(s_handle);
            if (ret == ESP_OK) ret = esp_ota_set_boot_partition(s_part);
            s_in_progress = false;
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "OTA complete, rebooting into %s", s_part->label);
                esp_restart();
            }
            ESP_LOGE(TAG, "OTA finalize failed: %s", esp_err_to_name(ret));
        }
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
        if (s_in_progress) { esp_ota_abort(s_handle); s_in_progress = false; }
        ESP_LOGW(TAG, "OTA aborted by server/stack");
        break;

    default:
        /* APPLY / CHECK / OK / ERROR etc. — nothing for the app to do. */
        break;
    }
    return ret;
}

void espir_ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            ESP_LOGI(TAG, "new image confirmed valid (rollback cancelled)");
    }
}
