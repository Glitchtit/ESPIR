/*
 * ESPIR master — USB-powered Zigbee Router. Learns and transmits IR via a YS-IRTM NEC
 * codec module over UART, stores codes in NVS, and exposes the custom cluster 0xFC00 to
 * Zigbee2MQTT / Home Assistant. NEC-only (the YS-IRTM does not handle other protocols).
 */
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "espir_irtm.h"
#include "espir_ir.h"
#include "espir_store.h"
#include "espir_device.h"

static const char *TAG = "espir-master";

void app_main(void)
{
    ESP_ERROR_CHECK(espir_store_init());
    ESP_ERROR_CHECK(espir_irtm_init(CONFIG_ESPIR_IRTM_UART_NUM,
                                    CONFIG_ESPIR_IRTM_TX_GPIO,
                                    CONFIG_ESPIR_IRTM_RX_GPIO,
                                    CONFIG_ESPIR_IRTM_BAUD));

    bool rmt_tx = false;
#if CONFIG_ESPIR_MASTER_USE_SZHJW
    /* SZHJW dual-LED transmitter on the RMT peripheral (transmit-only; learning stays on
     * the YS-IRTM receiver). Stronger than the YS-IRTM's single emitter. */
    ESP_ERROR_CHECK(espir_ir_init(CONFIG_ESPIR_TX_GPIO, -1));
    rmt_tx = true;
    ESP_LOGI(TAG, "ESPIR master: %d slots, YS-IRTM uart%d rx=%d, SZHJW tx=%d (RMT)",
             espir_store_count(), CONFIG_ESPIR_IRTM_UART_NUM,
             CONFIG_ESPIR_IRTM_RX_GPIO, CONFIG_ESPIR_TX_GPIO);
#else
    ESP_LOGI(TAG, "ESPIR master: %d slots, YS-IRTM uart%d tx=%d rx=%d",
             espir_store_count(), CONFIG_ESPIR_IRTM_UART_NUM,
             CONFIG_ESPIR_IRTM_TX_GPIO, CONFIG_ESPIR_IRTM_RX_GPIO);
#endif

    espir_device_cfg_t cfg = {
        .role = ESPIR_ROLE_MASTER,
        .manufacturer = "ESPIR",
        .model = "ESPIR-MASTER",
        .learn_timeout_ms = CONFIG_ESPIR_LEARN_TIMEOUT_MS,
        .master_rmt_tx = rmt_tx,
    };
    espir_device_start(&cfg);
}
