/*
 * ESPIR master — BENCH build (Phase 1).
 *
 * Temporary IR-only firmware to validate capture -> compact -> store -> replay on real
 * hardware before the Zigbee layer is added. It learns into slot 0 from whatever remote
 * you point at the receiver, prints what it captured, then transmits it back every few
 * seconds so you can confirm with the appliance (or a second receiver / scope).
 *
 * This file is replaced by the full Zigbee master in Phase 2.
 */
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "espir_ir.h"
#include "espir_store.h"

static const char *TAG = "espir-bench";

void app_main(void)
{
    ESP_ERROR_CHECK(espir_store_init());
    ESP_ERROR_CHECK(espir_ir_init(CONFIG_ESPIR_IR_TX_GPIO, CONFIG_ESPIR_IR_RX_GPIO));

    ESP_LOGI(TAG, "Point a remote at the receiver and press a key (learning slot 0)...");
    espir_code_t code;
    esp_err_t err = espir_ir_receive(&code, 20000);
    if (err == ESP_OK) {
        espir_code_try_compact(&code);
        ESP_LOGI(TAG, "captured: kind=%s carrier=%ukHz symbols=%u",
                 code.kind == ESPIR_KIND_NEC ? "NEC" : "RAW",
                 code.carrier_khz, code.n_symbols);
        if (code.kind == ESPIR_KIND_NEC) {
            ESP_LOGI(TAG, "NEC bytes: %02x %02x %02x %02x",
                     code.nec[0], code.nec[1], code.nec[2], code.nec[3]);
        }
        ESP_ERROR_CHECK(espir_store_save(0, &code));
        ESP_LOGI(TAG, "stored in slot 0");
    } else {
        ESP_LOGW(TAG, "no code captured (%s) — will replay slot 0 if present",
                 esp_err_to_name(err));
    }

    while (1) {
        espir_code_t out;
        if (espir_store_load(0, &out) == ESP_OK) {
            ESP_LOGI(TAG, "replaying slot 0 (%s)", out.kind == ESPIR_KIND_NEC ? "NEC" : "RAW");
            esp_err_t serr = espir_ir_send(&out);
            if (serr != ESP_OK) ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(serr));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
