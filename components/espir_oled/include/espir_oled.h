/*
 * espir_oled.h — 0.91" SSD1306 128x32 I2C status display for the ESPIR master.
 *
 * Pure renderer: fed device snapshots via espir_oled_update() (wire it to
 * espir_device_set_info_cb). Owns a minimal self-contained SSD1306 driver — no LVGL or
 * managed component. Optional: if the panel is absent, espir_oled_init() returns an error and
 * the device runs normally without a display.
 *
 * Layout (128x32, 6x8 font):
 *   ESPIR-MASTER    NET     model + connection state (BOOT / JOIN / NET)
 *   SLOT 05                 live selected slot
 *   LEARN: READY            learn / activity (READY / WAIT / OK! / FAIL / SENDING)
 */
#ifndef ESPIR_OLED_H
#define ESPIR_OLED_H

#include "esp_err.h"
#include "espir_device.h"   /* espir_info_t, espir_status_t */

typedef struct {
    int     sda_gpio;   /* I2C SDA */
    int     scl_gpio;   /* I2C SCL (the module's "SCK" silkscreen) */
    uint8_t i2c_addr;   /* typically 0x3C */
    int     i2c_port;   /* I2C port number, e.g. 0 */
} espir_oled_cfg_t;

esp_err_t espir_oled_init(const espir_oled_cfg_t *cfg);
void      espir_oled_update(const espir_info_t *info);

#endif /* ESPIR_OLED_H */
