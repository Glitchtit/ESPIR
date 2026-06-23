/*
 * espir_irtm.h — driver for the YS-IRTM NEC infrared codec module (UART).
 *
 * The YS-IRTM does NEC decode/encode internally and speaks UART (default 9600 8N1):
 *   transmit:  5 bytes  ADDR 0xF1 b0 b1 cmd   (module acks with 0xF1)
 *   receive:   3 bytes  b0 b1 cmd             (sent whenever it decodes an NEC frame)
 * where b0,b1 are the two NEC address bytes and cmd is the key (the inverse byte is
 * handled by the module). This is the master's IR backend (replaces RMT + VS1838B).
 *
 * NEC-only: remotes using other protocols (RC5/RC6/Sony/AC) cannot be learned here.
 */
#ifndef ESPIR_IRTM_H
#define ESPIR_IRTM_H

#include "esp_err.h"
#include "driver/uart.h"

esp_err_t espir_irtm_init(uart_port_t port, int tx_gpio, int rx_gpio, int baud);

/* Transmit one NEC frame (the 3 wire bytes, in receive order). */
esp_err_t espir_irtm_send(uint8_t b0, uint8_t b1, uint8_t cmd);

/* Block until the module decodes a frame (or timeout). Flushes stale input first. */
esp_err_t espir_irtm_receive(uint8_t *b0, uint8_t *b1, uint8_t *cmd, uint32_t timeout_ms);

#endif /* ESPIR_IRTM_H */
