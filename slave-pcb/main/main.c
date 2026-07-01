/*
 * ESPIR custom-PCB slave — same battery (LiPo) Zigbee Sleepy End Device as slave/, but on a
 * purpose-built carrier PCB: the XIAO ESP32-C6 is reflowed on, the SZHJW module is replaced by
 * a discrete MOSFET IR driver on the same GPIO, and a discrete RGB LED shows status:
 *   amber blink = searching for the network, green = joined, blue rapid blink = sending.
 * The green is gated on USB power so a steady LED can't flatten the battery (see espir_led.h).
 */
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "espir_ir.h"
#include "espir_store.h"
#include "espir_device.h"
#include "espir_led.h"

static const char *TAG = "espir-slave-pcb";

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

/* Map the device's high-level state onto the RGB LED animation. */
static void on_status(espir_status_t st)
{
    switch (st) {
    case ESPIR_STATUS_SEARCHING: espir_led_set(ESPIR_LED_SEARCHING); break;
    case ESPIR_STATUS_CONNECTED: espir_led_set(ESPIR_LED_CONNECTED); break;
    case ESPIR_STATUS_SENDING:   espir_led_set(ESPIR_LED_SENDING);   break;
    default:                     espir_led_set(ESPIR_LED_OFF);       break;
    }
}

void app_main(void)
{
    xiao_antenna_init();

    bool common_anode = false;
#ifdef CONFIG_ESPIR_LED_COMMON_ANODE
    common_anode = true;
#endif
    espir_led_cfg_t led = {
        .gpio_r       = CONFIG_ESPIR_LED_R_GPIO,
        .gpio_g       = CONFIG_ESPIR_LED_G_GPIO,
        .gpio_b       = CONFIG_ESPIR_LED_B_GPIO,
        .common_anode = common_anode,
    };
    ESP_ERROR_CHECK(espir_led_init(&led));
    espir_led_set(ESPIR_LED_SEARCHING);          /* show "searching" from boot until joined */
    espir_device_set_status_cb(on_status);

    ESP_ERROR_CHECK(espir_store_init());
    ESP_ERROR_CHECK(espir_ir_init(CONFIG_ESPIR_IR_TX_GPIO, -1));  /* transmit only */
    ESP_LOGI(TAG, "ESPIR slave-pcb: %d slots, tx=%d", espir_store_count(), CONFIG_ESPIR_IR_TX_GPIO);

    espir_device_cfg_t cfg = {
        .role = ESPIR_ROLE_SLAVE,
        .manufacturer = "ESPIR",
        .model = "ESPIR-SLAVE",
        .learn_timeout_ms = 0,
        .send_hold_ms = CONFIG_ESPIR_SEND_HOLD_MS,
        .ota = true,
        .ota_image_type = ESPIR_OTA_IMAGE_TYPE_SLAVE_PCB,
        .battery = true,
        .battery_adc_gpio = CONFIG_ESPIR_BATTERY_ADC_GPIO,
        .battery_div_x100 = CONFIG_ESPIR_BATTERY_DIV_X100,
    };
    espir_device_start(&cfg);
}
