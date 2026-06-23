#include "espir_ir.h"
#include <string.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

static const char *TAG = "espir_ir";

#define IR_RESOLUTION_HZ   1000000      /* 1 us per tick */
#define RMT_DURATION_MAX   32767        /* 15-bit RMT duration field */
#define RX_MIN_NS          1000         /* ignore <1us glitches */
#define RX_IDLE_NS         12000000     /* 12 ms idle ends a frame */

static rmt_channel_handle_t s_tx;
static rmt_channel_handle_t s_rx;
static rmt_encoder_handle_t s_copy_enc;
static QueueHandle_t s_rx_queue;
static uint16_t s_carrier_khz = ESPIR_CARRIER_DEFAULT_KHZ;

static rmt_symbol_word_t s_tx_syms[ESPIR_RAW_MAX_SYMBOLS];
static rmt_symbol_word_t s_rx_syms[ESPIR_RAW_MAX_SYMBOLS];

static inline uint16_t clamp_dur(uint16_t us)
{
    return us > RMT_DURATION_MAX ? RMT_DURATION_MAX : us;
}

static bool IRAM_ATTR on_rx_done(rmt_channel_handle_t ch,
                                 const rmt_rx_done_event_data_t *edata, void *ctx)
{
    BaseType_t hp = pdFALSE;
    QueueHandle_t q = (QueueHandle_t)ctx;
    xQueueSendFromISR(q, edata, &hp);
    return hp == pdTRUE;
}

static esp_err_t apply_carrier(uint16_t carrier_khz)
{
    rmt_carrier_config_t cc = {
        .frequency_hz = carrier_khz * 1000,
        .duty_cycle = 0.33f,
        .flags = { .polarity_active_low = false, .always_on = false },
    };
    return rmt_apply_carrier(s_tx, &cc);
}

esp_err_t espir_ir_init(int tx_gpio, int rx_gpio)
{
    rmt_tx_channel_config_t txc = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = tx_gpio,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&txc, &s_tx), TAG, "tx channel");
    ESP_RETURN_ON_ERROR(apply_carrier(s_carrier_khz), TAG, "carrier");

    rmt_copy_encoder_config_t cec = {0};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&cec, &s_copy_enc), TAG, "copy encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx), TAG, "tx enable");

    if (rx_gpio >= 0) {
        s_rx_queue = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
        ESP_RETURN_ON_FALSE(s_rx_queue, ESP_ERR_NO_MEM, TAG, "rx queue");

        rmt_rx_channel_config_t rxc = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .gpio_num = rx_gpio,
            .resolution_hz = IR_RESOLUTION_HZ,
            .mem_block_symbols = 64,
        };
        ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rxc, &s_rx), TAG, "rx channel");
        rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_rx_done };
        ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(s_rx, &cbs, s_rx_queue),
                            TAG, "rx callbacks");
        ESP_RETURN_ON_ERROR(rmt_enable(s_rx), TAG, "rx enable");
    }
    ESP_LOGI(TAG, "IR init tx=%d rx=%d", tx_gpio, rx_gpio);
    return ESP_OK;
}

/* Pack a flat mark/space duration array into RMT symbols (mark = carrier-modulated high). */
static int pack_symbols(const uint16_t *dur, int n)
{
    int s = 0;
    for (int i = 0; i < n; i += 2, s++) {
        if (s >= ESPIR_RAW_MAX_SYMBOLS) return -1;
        s_tx_syms[s].level0 = 1;                       /* mark: carrier on */
        s_tx_syms[s].duration0 = clamp_dur(dur[i]);
        if (i + 1 < n) {
            s_tx_syms[s].level1 = 0;                   /* space */
            s_tx_syms[s].duration1 = clamp_dur(dur[i + 1]);
        } else {
            s_tx_syms[s].level1 = 0;                   /* trailing stop has no space */
            s_tx_syms[s].duration1 = 0;
        }
    }
    return s;
}

esp_err_t espir_ir_send(const espir_code_t *code)
{
    if (!code) return ESP_ERR_INVALID_ARG;

    const uint16_t *dur;
    int n;
    uint16_t nec_buf[128];   /* NEC envelope is 67 symbols */
    if (code->kind == ESPIR_KIND_NEC) {
        n = espir_nec_encode(code->nec, nec_buf, (int)(sizeof(nec_buf) / sizeof(nec_buf[0])));
        if (n < 0) return ESP_ERR_INVALID_SIZE;
        dur = nec_buf;
    } else {
        if (code->n_symbols == 0) return ESP_ERR_INVALID_ARG;
        dur = code->symbols;
        n = code->n_symbols;
    }

    uint16_t want = code->carrier_khz ? code->carrier_khz : ESPIR_CARRIER_DEFAULT_KHZ;
    if (want != s_carrier_khz) {
        ESP_RETURN_ON_ERROR(rmt_disable(s_tx), TAG, "tx disable");
        ESP_RETURN_ON_ERROR(apply_carrier(want), TAG, "recarrier");
        ESP_RETURN_ON_ERROR(rmt_enable(s_tx), TAG, "tx reenable");
        s_carrier_khz = want;
    }

    int nsym = pack_symbols(dur, n);
    if (nsym < 0) return ESP_ERR_INVALID_SIZE;

    rmt_transmit_config_t tcfg = { .loop_count = 0 };
    ESP_RETURN_ON_ERROR(rmt_transmit(s_tx, s_copy_enc, s_tx_syms,
                                     nsym * sizeof(rmt_symbol_word_t), &tcfg), TAG, "transmit");
    return rmt_tx_wait_all_done(s_tx, 1000);
}

#define IR_HEADER_MIN_US 2000   /* NEC ~9000, Samsung ~4500, most ACs have a long leading mark */
#define IR_GOOD_SYMBOLS  20     /* a real frame has many edges; fragments are short */

static uint16_t s_rx_tmp[ESPIR_RAW_MAX_SYMBOLS];

esp_err_t espir_ir_receive(espir_code_t *out, uint32_t timeout_ms)
{
    if (!s_rx) return ESP_ERR_INVALID_STATE;
    if (!out) return ESP_ERR_INVALID_ARG;

    rmt_receive_config_t rcfg = {
        .signal_range_min_ns = RX_MIN_NS,
        .signal_range_max_ns = RX_IDLE_NS,
    };
    memset(out, 0, sizeof(*out));
    out->kind = ESPIR_KIND_RAW;
    out->carrier_khz = ESPIR_CARRIER_DEFAULT_KHZ;  /* demodulated RX can't tell carrier */

    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int best = 0;

    /* Capture repeatedly within the window. Holding/mashing a remote makes the first arm
     * land mid-frame (a short, header-less fragment); re-arming lands in the inter-frame gap
     * and catches the next full frame. Keep the longest, and stop once we have a clean
     * header-led frame. */
    while (esp_timer_get_time() < deadline) {
        int64_t remain_ms = (deadline - esp_timer_get_time()) / 1000;
        if (remain_ms <= 0) break;
        if (rmt_receive(s_rx, s_rx_syms, sizeof(s_rx_syms), &rcfg) != ESP_OK) break;

        rmt_rx_done_event_data_t ev;
        if (xQueueReceive(s_rx_queue, &ev, pdMS_TO_TICKS(remain_ms)) != pdTRUE) break;

        int k = 0;
        for (size_t i = 0; i < ev.num_symbols && k < ESPIR_RAW_MAX_SYMBOLS; i++) {
            if (ev.received_symbols[i].duration0) s_rx_tmp[k++] = ev.received_symbols[i].duration0;
            if (k < ESPIR_RAW_MAX_SYMBOLS && ev.received_symbols[i].duration1)
                s_rx_tmp[k++] = ev.received_symbols[i].duration1;
        }
        if (k > best) {
            memcpy(out->symbols, s_rx_tmp, k * sizeof(uint16_t));
            out->n_symbols = k;
            best = k;
        }
        if (best >= IR_GOOD_SYMBOLS && out->symbols[0] >= IR_HEADER_MIN_US) break; /* full frame */
    }

    ESP_LOGI(TAG, "rx captured %d durations, header=%u us", best, best ? out->symbols[0] : 0);
    return best >= 2 ? ESP_OK : ESP_ERR_TIMEOUT;
}
