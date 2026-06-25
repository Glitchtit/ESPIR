/*
 * ESPIR master — USB-powered Zigbee Router. Learns IR codes by raw-capturing from a VS1838B
 * receiver and transmits them through an SZHJW dual-LED module — both on the ESP32-C6 RMT
 * peripheral. Stores codes in NVS and exposes the custom cluster 0xFC00 to Z2M / HA.
 * Learns any protocol (raw envelope), auto-compacting NEC.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "espir_ir.h"
#include "espir_store.h"
#include "espir_device.h"
#include "espir_oled.h"

static const char *TAG = "espir-master";

#if CONFIG_ESPIR_OLED_ENABLE
static void oled_info_cb(const espir_info_t *info) { espir_oled_update(info); }
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(espir_store_init());
    ESP_ERROR_CHECK(espir_ir_init(CONFIG_ESPIR_IR_TX_GPIO, CONFIG_ESPIR_IR_RX_GPIO));
    ESP_LOGI(TAG, "ESPIR master: %d slots, SZHJW tx=%d, VS1838B rx=%d (RMT)",
             espir_store_count(), CONFIG_ESPIR_IR_TX_GPIO, CONFIG_ESPIR_IR_RX_GPIO);

#if CONFIG_ESPIR_OLED_ENABLE
    espir_oled_cfg_t ocfg = {
        .sda_gpio = CONFIG_ESPIR_OLED_SDA_GPIO,
        .scl_gpio = CONFIG_ESPIR_OLED_SCL_GPIO,
        .i2c_addr = CONFIG_ESPIR_OLED_I2C_ADDR,
        .i2c_port = 0,
    };
    if (espir_oled_init(&ocfg) == ESP_OK)
        espir_device_set_info_cb(oled_info_cb);
    else
        ESP_LOGW(TAG, "OLED not found; continuing without a display");
#endif

    espir_device_cfg_t cfg = {
        .role = ESPIR_ROLE_MASTER,
        .manufacturer = "ESPIR",
        .model = "ESPIR-MASTER",
        .learn_timeout_ms = CONFIG_ESPIR_LEARN_TIMEOUT_MS,
        .send_hold_ms = CONFIG_ESPIR_SEND_HOLD_MS,
    };
    espir_device_start(&cfg);
}
