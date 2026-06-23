#include "espir_irtm.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "espir_irtm";

#define IRTM_ADDR     0xA1   /* module default address */
#define IRTM_CMD_TX   0xF1   /* "transmit" operating position */
#define UART_BUF      256

static uart_port_t s_port;

esp_err_t espir_irtm_init(uart_port_t port, int tx_gpio, int rx_gpio, int baud)
{
    s_port = port;
    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(port, UART_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    if ((err = uart_param_config(port, &cfg)) != ESP_OK) return err;
    if ((err = uart_set_pin(port, tx_gpio, rx_gpio,
                            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) != ESP_OK) return err;
    ESP_LOGI(TAG, "YS-IRTM on uart%d tx=%d rx=%d @%d", port, tx_gpio, rx_gpio, baud);
    return ESP_OK;
}

esp_err_t espir_irtm_send(uint8_t b0, uint8_t b1, uint8_t cmd)
{
    uint8_t frame[5] = {IRTM_ADDR, IRTM_CMD_TX, b0, b1, cmd};
    uart_flush_input(s_port);
    int w = uart_write_bytes(s_port, frame, sizeof(frame));
    if (w != sizeof(frame)) return ESP_FAIL;
    uint8_t ack = 0;                                   /* module echoes 0xF1 on accept */
    uart_read_bytes(s_port, &ack, 1, pdMS_TO_TICKS(200));
    return ESP_OK;
}

esp_err_t espir_irtm_receive(uint8_t *b0, uint8_t *b1, uint8_t *cmd, uint32_t timeout_ms)
{
    uint8_t buf[3];
    uart_flush_input(s_port);                          /* drop anything stale, wait for next key */
    int n = uart_read_bytes(s_port, buf, sizeof(buf), pdMS_TO_TICKS(timeout_ms));
    if (n < 3) return ESP_ERR_TIMEOUT;
    *b0 = buf[0];
    *b1 = buf[1];
    *cmd = buf[2];
    return ESP_OK;
}
