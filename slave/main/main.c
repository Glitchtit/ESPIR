/*
 * ESPIR slave — battery (LiPo) Zigbee Sleepy End Device. Transmit-only repeater for IR
 * coverage in another spot. Receives learned codes from the master (replicated via Home
 * Assistant) into NVS slots and transmits them on command. No IR receiver.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "espir_ir.h"
#include "espir_store.h"
#include "espir_device.h"

static const char *TAG = "espir-slave";

void app_main(void)
{
    ESP_ERROR_CHECK(espir_store_init());
    ESP_ERROR_CHECK(espir_ir_init(CONFIG_ESPIR_IR_TX_GPIO, -1));  /* transmit only */
    ESP_LOGI(TAG, "ESPIR slave: %d slots, tx=%d", espir_store_count(), CONFIG_ESPIR_IR_TX_GPIO);

    espir_device_cfg_t cfg = {
        .role = ESPIR_ROLE_SLAVE,
        .manufacturer = "ESPIR",
        .model = "ESPIR-SLAVE",
        .learn_timeout_ms = 0,
    };
    espir_device_start(&cfg);
}
