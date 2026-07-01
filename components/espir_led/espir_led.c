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
#define CONNECT_PULSE_MS 3000 /* green: solid for 3 s on join, then dark (both USB and battery) */

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

static void led_task(void *arg)
{
    (void)arg;
    bool phase = false;
    espir_led_state_t shown = ESPIR_LED_OFF;     /* last state acted on, to detect transitions */
    for (;;) {
        espir_led_state_t st = s_state;
        TickType_t wait = portMAX_DELAY;
        switch (st) {
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
        case ESPIR_LED_CONNECTED:
            /* Green is a one-shot "joined" confirmation, not a steady state: solid green for
             * CONNECT_PULSE_MS on the join transition, then dark. A steady LED would flatten the
             * LiPo, so it's never held on — and it's power-source independent (USB and battery
             * behave identically). Coming back from a send (SENDING → CONNECTED) skips the pulse
             * and goes straight dark, so IR sends don't re-flash green each time. */
            if (shown != ESPIR_LED_CONNECTED && shown != ESPIR_LED_SENDING) {
                set_rgb(0, C_GREEN_G, 0);
                wait = pdMS_TO_TICKS(CONNECT_PULSE_MS);
            } else {
                set_rgb(0, 0, 0);
                wait = portMAX_DELAY;            /* pulse elapsed / returned from send → hold dark */
            }
            break;
        case ESPIR_LED_OFF:
        default:
            set_rgb(0, 0, 0);
            wait = portMAX_DELAY;
            break;
        }
        shown = st;
        ulTaskNotifyTake(pdTRUE, wait);          /* espir_led_set() wakes us early on state change */
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

    /* Off-level for the drive pins: active-high (common-cathode / low-side FET) is off when LOW,
     * common-anode is off when HIGH. */
    const gpio_pull_mode_t off_pull = cfg->common_anode ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY;

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
        /* Hold the gate at its off-level through automatic light sleep. On a sleepy ED, ESP-IDF's
         * GPIO reset workaround isolates every pad (floating) during light sleep; with no external
         * MOSFET gate pulldown on this board, a floating gate drifts up and glimmers the LED. An
         * internal pull toward the off-level keeps the FET off while the indicator is dark. */
        gpio_sleep_set_pull_mode(chans[i].gpio, off_pull);
        gpio_sleep_sel_en(chans[i].gpio);
    }
    set_rgb(0, 0, 0);

    BaseType_t ok = xTaskCreate(led_task, "espir_led", 2560, NULL, 3, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "led task");
    ESP_LOGI(TAG, "RGB status LED on r=%d g=%d b=%d (%s)",
             cfg->gpio_r, cfg->gpio_g, cfg->gpio_b,
             cfg->common_anode ? "common-anode" : "common-cathode");
    return ESP_OK;
}

void espir_led_set(espir_led_state_t state)
{
    if (state == s_state) return;   /* ignore no-op re-sets so a repeated CONNECTED can't cut the green pulse short */
    s_state = state;
    if (s_task) xTaskNotifyGive(s_task);
}
