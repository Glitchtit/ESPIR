/*
 * espir_ir.h — IR transmit/receive over the ESP32-C6 RMT peripheral.
 *
 * TX generates the 38 kHz (or per-code) carrier in hardware and modulates a slot's
 * mark/space envelope onto the SZHJW transmitter's DAT line. RX captures the raw
 * demodulated envelope from a VS1838B/TSOP receiver.
 */
#ifndef ESPIR_IR_H
#define ESPIR_IR_H

#include "esp_err.h"
#include "espir_code.h"

/* tx_gpio drives the SZHJW DAT pin. rx_gpio reads the VS1838B OUT pin; pass a negative
 * value on transmit-only units (slaves) to skip receiver setup. */
esp_err_t espir_ir_init(int tx_gpio, int rx_gpio);

/* Transmit a code (NEC is re-encoded to an envelope; RAW is replayed as-is). Blocks until
 * the frame has finished transmitting. */
esp_err_t espir_ir_send(const espir_code_t *code);

/* Capture one raw envelope into `out` (kind = RAW). Returns ESP_ERR_TIMEOUT if nothing is
 * received within timeout_ms. Requires a receiver (rx_gpio >= 0 at init). */
esp_err_t espir_ir_receive(espir_code_t *out, uint32_t timeout_ms);

#endif /* ESPIR_IR_H */
