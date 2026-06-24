/*
 * espir_device.h — the ESPIR Zigbee device (shared by master and slave).
 *
 * Builds the HA endpoint (Basic + Identify + custom cluster 0xFC00), joins the network,
 * and services the custom commands defined in espir_proto.h. The master additionally runs
 * the learn FSM (capture from the IR receiver). Call espir_device_start() once from
 * app_main after espir_store_init() and espir_ir_init().
 */
#ifndef ESPIR_DEVICE_H
#define ESPIR_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "espir_proto.h"

typedef struct {
    espir_role_t role;            /* ESPIR_ROLE_MASTER (Router) or ESPIR_ROLE_SLAVE (End Device) */
    const char  *manufacturer;    /* Basic-cluster manufacturer string, e.g. "ESPIR" */
    const char  *model;           /* Basic-cluster model string, e.g. "ESPIR-MASTER" */
    uint32_t     learn_timeout_ms;/* master only: how long learn mode waits for a key */
    uint32_t     send_hold_ms;    /* repeat each send for this long (mimics holding a remote key); 0 = single shot */
    bool         battery;         /* expose the Power Config cluster + report LiPo level (slave) */
    int          battery_adc_gpio;/* ADC1 GPIO reading the BAT+ divider (XIAO A0 = GPIO0) */
    int          battery_div_x100;/* divider ratio ×100 = (R_top+R_bottom)*100/R_bottom; 200 = ÷2 */
} espir_device_cfg_t;

/* Starts the Zigbee stack task. Does not return control of the Zigbee stack; safe to call
 * once and let app_main return. */
void espir_device_start(const espir_device_cfg_t *cfg);

/* High-level device state, surfaced for an optional status indicator (e.g. an RGB LED on the
 * custom-PCB slave). Purely advisory — the device works fine with no callback registered. */
typedef enum {
    ESPIR_STATUS_BOOT      = 0,  /* powered up, stack not yet commissioning            */
    ESPIR_STATUS_SEARCHING = 1,  /* network steering in progress / retrying            */
    ESPIR_STATUS_CONNECTED = 2,  /* joined the network and idle                        */
    ESPIR_STATUS_SENDING   = 3,  /* actively transmitting an IR frame                  */
} espir_status_t;

/* Register a callback invoked on every device-state transition. Pass NULL to disable.
 * The callback may run from the Zigbee task or the send task, so keep it short and
 * non-blocking (the espir_led implementation just latches the state and wakes its own task).
 * Default: no callback, so master/breadboard-slave builds are unaffected. */
void espir_device_set_status_cb(void (*cb)(espir_status_t status));

#endif /* ESPIR_DEVICE_H */
