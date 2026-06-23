/*
 * ESPIR slave — battery (LiPo) Zigbee Sleepy End Device. Transmit-only repeater for IR
 * coverage in another spot. Receives learned codes from the master (replicated via Home
 * Assistant) into NVS slots and transmits them on command. No IR receiver.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "espir_ir.h"
#include "espir_store.h"
#include "espir_device.h"

static const char *TAG = "espir-slave";

/* XIAO ESP32-C6 antenna RF switch: GPIO3 LOW enables the switch; GPIO14 selects the antenna
 * (LOW = onboard ceramic, HIGH = external U.FL). Required or the radio can't reach the network. */
static void xiao_antenna_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << 3) | (1ULL << 14),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(3, 0);   /* enable RF switch */
#ifdef CONFIG_ESPIR_XIAO_EXT_ANTENNA
    gpio_set_level(14, 1);  /* external U.FL */
    ESP_LOGI(TAG, "XIAO antenna: external U.FL");
#else
    gpio_set_level(14, 0);  /* onboard ceramic */
    ESP_LOGI(TAG, "XIAO antenna: onboard");
#endif
}

void app_main(void)
{
    xiao_antenna_init();
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
