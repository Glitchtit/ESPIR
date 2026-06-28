/*
 * espir_ota.h — Zigbee OTA Upgrade *client* for the ESPIR master.
 *
 * Registers the OTA cluster on the device endpoint, streams received image blocks into
 * the inactive app partition, and confirms the new image (cancelling bootloader rollback)
 * once the device is healthy. Master-only; the slave apps do not enable it.
 */
#ifndef ESPIR_OTA_H
#define ESPIR_OTA_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee_core.h"

/* Build the OTA Upgrade client cluster, populated from ESPIR_FW_VERSION /
 * ESPIR_OTA_IMAGE_TYPE / ESPIR_MANUF_CODE. Caller adds it to the endpoint cluster list
 * with ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE. */
esp_zb_attribute_list_t *espir_ota_cluster_create(void);

/* Activate periodic image queries on the given endpoint. Call once joined. */
void espir_ota_start(uint8_t endpoint);

/* Feed an ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID message to the writer. Returns ESP_OK to
 * let the stack continue the transfer; non-OK aborts it. */
esp_err_t espir_ota_handle_value(const void *message);

/* If this boot is a freshly-applied image pending verification, mark it valid so the
 * bootloader keeps it. No-op otherwise. Call once the device is confirmed healthy. */
void espir_ota_mark_valid(void);

#endif /* ESPIR_OTA_H */
