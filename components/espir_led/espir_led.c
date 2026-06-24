#include "espir_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

static const char *TAG = "espir_led";

/* One low-speed timer feeds all three channels. Low-speed mode is the only LEDC mode on the
 * C6 and is the right one for a battery part — its config survives light sleep. */
#define LED_TIMER      LEDC_TIMER_0
#define LED_MODE       LEDC_LOW_SPEED_MODE
#define LED_RES        LEDC_TIMER_8_BIT          /* duty 0..255 */
#define LED_FREQ_HZ    4000
#define LED_CH_R       LEDC_CHANNEL_0
#define LED_CH_G       LEDC_CHANNEL_1
#define LED_CH_B       LEDC_CHANNEL_2

/* Per-channel duty for each colour (common-cathode / active-high reference; inverted for
 * common-anode). Amber = red full + green partial; the partial green keeps it orange rather
 * than yellow. Green/blue are driven full so the solid CONNECTED state holds its pin level
 * through light sleep instead of relying on the PWM clock. */
#define C_AMBER_R   255
#define C_AMBER_G   64
#define C_GREEN_G   255
#define C_BLUE_B    255

#define BLINK_SLOW_MS  300   /* amber: ~1.6 Hz */
#define BLINK_FAST_MS  80    /* blue:  ~6 Hz    */

static espir_led_cfg_t   s_cfg;
static volatile espir_led_state_t s_state = ESPIR_LED_OFF;
static TaskHandle_t      s_task;

static void chan_write(ledc_channel_t ch, int gpio, uint8_t duty)
{
    if (gpio < 0) return;
    uint8_t d = s_cfg.common_anode ? (uint8_t)(255 - duty) : duty;
    ledc_set_duty(LED_MODE, ch, d);
    ledc_update_duty(LED_MODE, ch);
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    chan_write(LED_CH_R, s_cfg.gpio_r, r);
    chan_write(LED_CH_G, s_cfg.gpio_g, g);
    chan_write(LED_CH_B, s_cfg.gpio_b, b);
}

static bool vbus_present(void)
{
    if (s_cfg.vbus_gpio < 0) return true;        /* no sensing → treat as powered */
    return gpio_get_level(s_cfg.vbus_gpio) != 0; /* HIGH via the VBUS divider = USB present */
}

/* USB plug/unplug while idle: wake the task so the solid-green state re-evaluates immediately
 * instead of waiting for the next device-state change. */
static void IRAM_ATTR vbus_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    if (s_task) vTaskNotifyGiveFromISR(s_task, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void led_task(void *arg)
{
    (void)arg;
    bool phase = false;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        switch (s_state) {
        case ESPIR_LED_SEARCHING:                /* amber slow blink — CPU is awake commissioning */
            phase = !phase;
            set_rgb(phase ? C_AMBER_R : 0, phase ? C_AMBER_G : 0, 0);
            wait = pdMS_TO_TICKS(BLINK_SLOW_MS);
            break;
        case ESPIR_LED_SENDING:                  /* blue rapid blink — CPU is awake sending */
            phase = !phase;
            set_rgb(0, 0, phase ? C_BLUE_B : 0);
            wait = pdMS_TO_TICKS(BLINK_FAST_MS);
            break;
        case ESPIR_LED_CONNECTED:                /* solid green, but only on USB (battery would drain) */
            set_rgb(0, vbus_present() ? C_GREEN_G : 0, 0);
            wait = portMAX_DELAY;                /* static → block; wakes on state change or VBUS edge */
            break;
        case ESPIR_LED_OFF:
        default:
            set_rgb(0, 0, 0);
            wait = portMAX_DELAY;
            break;
        }
        ulTaskNotifyTake(pdTRUE, wait);          /* espir_led_set()/vbus_isr() wake us early */
    }
}

esp_err_t espir_led_init(const espir_led_cfg_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg");
    s_cfg = *cfg;

    ledc_timer_config_t tc = {
        .speed_mode      = LED_MODE,
        .timer_num       = LED_TIMER,
        .duty_resolution = LED_RES,
        .freq_hz         = LED_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&tc), TAG, "ledc timer");

    const struct { ledc_channel_t ch; int gpio; } chans[] = {
        { LED_CH_R, cfg->gpio_r }, { LED_CH_G, cfg->gpio_g }, { LED_CH_B, cfg->gpio_b },
    };
    for (size_t i = 0; i < sizeof(chans) / sizeof(chans[0]); i++) {
        if (chans[i].gpio < 0) continue;
        ledc_channel_config_t cc = {
            .gpio_num   = chans[i].gpio,
            .speed_mode = LED_MODE,
            .channel    = chans[i].ch,
            .timer_sel  = LED_TIMER,
            .duty       = cfg->common_anode ? 255 : 0,   /* start dark either polarity */
            .hpoint     = 0,
            .intr_type  = LEDC_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&cc), TAG, "ledc ch %d", chans[i].ch);
    }
    set_rgb(0, 0, 0);

    if (cfg->vbus_gpio >= 0) {
        gpio_config_t vc = {
            .pin_bit_mask = 1ULL << cfg->vbus_gpio,
            .mode         = GPIO_MODE_INPUT,
            .intr_type    = GPIO_INTR_ANYEDGE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&vc), TAG, "vbus gpio");
        esp_err_t e = gpio_install_isr_service(0);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;  /* tolerate shared service */
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(cfg->vbus_gpio, vbus_isr, NULL), TAG, "vbus isr");
    }

    BaseType_t ok = xTaskCreate(led_task, "espir_led", 2560, NULL, 3, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "led task");
    ESP_LOGI(TAG, "RGB status LED on r=%d g=%d b=%d (%s), vbus=%d",
             cfg->gpio_r, cfg->gpio_g, cfg->gpio_b,
             cfg->common_anode ? "common-anode" : "common-cathode", cfg->vbus_gpio);
    return ESP_OK;
}

void espir_led_set(espir_led_state_t state)
{
    s_state = state;
    if (s_task) xTaskNotifyGive(s_task);
}
