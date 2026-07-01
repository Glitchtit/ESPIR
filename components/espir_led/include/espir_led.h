/*
 * espir_led.h — RGB status indicator for the custom-PCB slave.
 *
 * Drives a discrete common-anode or common-cathode RGB LED via LEDC PWM and runs a tiny
 * sleep-friendly FreeRTOS task that animates the current device state:
 *
 *   ESPIR_LED_SEARCHING  amber, slow blink   (network steering in progress)
 *   ESPIR_LED_CONNECTED  green, 3 s pulse     (just joined) then dark
 *   ESPIR_LED_SENDING    blue, rapid blink   (transmitting an IR frame)
 *   ESPIR_LED_OFF        dark
 *
 * Battery note: a steady LED is fatal to a sleepy end device's months-long battery life, so
 * green is never held on — it's a one-shot 3 s "joined" confirmation that then goes dark. The
 * behaviour is identical on USB and battery. When dark/static the animation task blocks
 * indefinitely, so it never holds the CPU awake or blocks light sleep.
 *
 * Wire it to the device state with espir_device_set_status_cb() (see espir_device.h).
 */
#ifndef ESPIR_LED_H
#define ESPIR_LED_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    ESPIR_LED_OFF       = 0,
    ESPIR_LED_SEARCHING = 1,   /* amber, slow blink  */
    ESPIR_LED_CONNECTED = 2,   /* green, 3 s pulse on join then dark */
    ESPIR_LED_SENDING   = 3,   /* blue, rapid blink  */
} espir_led_state_t;

typedef struct {
    int  gpio_r;        /* red   channel GPIO (-1 = channel unused) */
    int  gpio_g;        /* green channel GPIO (-1 = channel unused) */
    int  gpio_b;        /* blue  channel GPIO (-1 = channel unused) */
    bool common_anode;  /* true: common-anode (active-low); false: common-cathode (active-high) */
} espir_led_cfg_t;

/* Configures LEDC and starts the animation task. Call once from app_main. Safe to call
 * espir_led_set() before or after. */
esp_err_t espir_led_init(const espir_led_cfg_t *cfg);

/* Latch a new state and wake the animation task. Cheap and ISR-free; safe from any task. */
void espir_led_set(espir_led_state_t state);

#endif /* ESPIR_LED_H */
