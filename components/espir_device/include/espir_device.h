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
    bool         master_rmt_tx;   /* master only: transmit via the SZHJW (RMT) instead of the
                                     weaker YS-IRTM emitter. Requires espir_ir_init() in app. */
} espir_device_cfg_t;

/* Starts the Zigbee stack task. Does not return control of the Zigbee stack; safe to call
 * once and let app_main return. */
void espir_device_start(const espir_device_cfg_t *cfg);

#endif /* ESPIR_DEVICE_H */
