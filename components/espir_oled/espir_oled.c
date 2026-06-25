#include "espir_oled.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "espir_proto.h"   /* espir_learn_status_t */

static const char *TAG = "espir_oled";

#define OLED_W        128
#define OLED_H        32
#define OLED_PAGES    (OLED_H / 8)              /* 4 */
#define FB_BYTES      (OLED_W * OLED_PAGES)     /* 512 */
#define I2C_TIMEOUT_MS 100

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static TaskHandle_t s_task;

static uint8_t s_fb[FB_BYTES];
static volatile espir_info_t s_info;           /* latest snapshot to render */

/* ---- 5x7 font (drawn in a 6-wide cell). Uppercase + digits + ``- : ! . space``.
 * Each glyph = 5 columns, LSB = top pixel. Unknown chars render blank. ---------------- */
typedef struct { char c; uint8_t col[5]; } glyph_t;
static const glyph_t FONT[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'!', {0x00,0x00,0x5F,0x00,0x00}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'.', {0x00,0x60,0x60,0x00,0x00}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'0', {0x3E,0x51,0x49,0x45,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1E}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}},
    {'B', {0x7F,0x49,0x49,0x49,0x36}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}},
    {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'G', {0x3E,0x41,0x49,0x49,0x7A}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'J', {0x20,0x40,0x41,0x3F,0x01}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}},
    {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
    {'N', {0x7F,0x04,0x08,0x10,0x7F}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'Q', {0x3E,0x41,0x51,0x21,0x5E}},
    {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x3F,0x40,0x40,0x40,0x3F}},
    {'V', {0x1F,0x20,0x40,0x20,0x1F}},
    {'W', {0x7F,0x20,0x18,0x20,0x7F}},
    {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}},
    {'Z', {0x61,0x51,0x49,0x45,0x43}},
};

static const uint8_t *glyph_for(char c)
{
    for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++)
        if (FONT[i].c == c) return FONT[i].col;
    return FONT[0].col;   /* blank */
}

/* ---- SSD1306 I2C primitives ------------------------------------------------ */
static esp_err_t ssd1306_cmd(uint8_t c)
{
    uint8_t b[2] = { 0x00, c };   /* 0x00 = command stream */
    return i2c_master_transmit(s_dev, b, sizeof(b), I2C_TIMEOUT_MS);
}

static esp_err_t ssd1306_flush(void)
{
    ESP_RETURN_ON_ERROR(ssd1306_cmd(0x21), TAG, "col");   /* column addr */
    ESP_RETURN_ON_ERROR(ssd1306_cmd(0x00), TAG, "col0");
    ESP_RETURN_ON_ERROR(ssd1306_cmd(0x7F), TAG, "col127");
    ESP_RETURN_ON_ERROR(ssd1306_cmd(0x22), TAG, "page");  /* page addr */
    ESP_RETURN_ON_ERROR(ssd1306_cmd(0x00), TAG, "page0");
    ESP_RETURN_ON_ERROR(ssd1306_cmd(OLED_PAGES - 1), TAG, "pageN");

    static uint8_t buf[1 + FB_BYTES];
    buf[0] = 0x40;   /* 0x40 = data stream */
    memcpy(&buf[1], s_fb, FB_BYTES);
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

/* ---- framebuffer drawing --------------------------------------------------- */
static void fb_clear(void) { memset(s_fb, 0, FB_BYTES); }

/* Draw text at cell (x_px, page). 6 px per char; clips at the right edge. */
static void fb_text(int x_px, int page, const char *s)
{
    if (page < 0 || page >= OLED_PAGES || x_px < 0) return;
    for (; *s && x_px <= OLED_W - 6; s++, x_px += 6) {
        const uint8_t *g = glyph_for(*s);
        for (int col = 0; col < 5; col++)
            s_fb[page * OLED_W + x_px + col] = g[col];
        s_fb[page * OLED_W + x_px + 5] = 0x00;   /* inter-char gap */
    }
}

/* Right-align a short string on the given page. */
static void fb_text_right(int page, const char *s)
{
    int w = (int)strlen(s) * 6;
    fb_text(OLED_W - w, page, s);
}

/* ---- state -> strings ------------------------------------------------------ */
static const char *conn_text(espir_status_t st)
{
    switch (st) {
    case ESPIR_STATUS_SEARCHING: return "JOIN";
    case ESPIR_STATUS_CONNECTED: return "NET";
    case ESPIR_STATUS_SENDING:   return "TX";
    default:                     return "BOOT";
    }
}

static const char *learn_text(const espir_info_t *in)
{
    if (in->status == ESPIR_STATUS_SENDING) return "SENDING";
    switch (in->learn_status) {
    case ESPIR_LEARN_WAITING:  return "LEARN: WAIT";
    case ESPIR_LEARN_CAPTURED: return "LEARN: OK!";
    case ESPIR_LEARN_FAILED:   return "LEARN: FAIL";
    default:                   return "LEARN: READY";
    }
}

static void render(const espir_info_t *in)
{
    char slot[20];  /* "SLOT NNN  STORED\0" */
    /* During a learn, show the slot being captured; otherwise the selector. The occupancy
     * flag tracks the selected slot (== the learn target in the normal flow). */
    uint8_t show = (in->learn_slot != ESPIR_SLOT_IDLE) ? in->learn_slot : in->selected_slot;
    snprintf(slot, sizeof(slot), "SLOT %u  %s", (unsigned)show,
             in->slot_occupied ? "STORED" : "EMPTY");

    fb_clear();
    fb_text(0, 0, "ESPIR-MASTER");
    fb_text_right(0, conn_text(in->status));
    fb_text(0, 2, slot);
    fb_text(0, 3, learn_text(in));
    ssd1306_flush();
}

/* ---- render task ----------------------------------------------------------- */
static void oled_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        espir_info_t snap = s_info;   /* copy the latched snapshot */
        render(&snap);
    }
}

/* ---- init ------------------------------------------------------------------ */
static const uint8_t SSD1306_INIT[] = {
    0xAE,             /* display off */
    0xD5, 0x80,       /* clock div */
    0xA8, 0x1F,       /* multiplex = 31 (32 rows) */
    0xD3, 0x00,       /* display offset 0 */
    0x40,             /* start line 0 */
    0x8D, 0x14,       /* charge pump on */
    0x20, 0x00,       /* horizontal addressing */
    0xA1,             /* segment remap */
    0xC8,             /* COM scan dec */
    0xDA, 0x02,       /* COM pins (128x32) */
    0x81, 0x8F,       /* contrast */
    0xD9, 0xF1,       /* precharge */
    0xDB, 0x40,       /* vcom detect */
    0xA4,             /* resume to RAM */
    0xA6,             /* normal (non-inverted) */
    0xAF,             /* display on */
};

esp_err_t espir_oled_init(const espir_oled_cfg_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg");

    esp_err_t ret = ESP_OK;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = cfg->i2c_port,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->i2c_addr,
        .scl_speed_hz = 400000,
    };
    /* From here on, unwind the bus/device on failure so the panel-absent path (a probe
     * NACK) leaves no half-initialised I2C peripheral behind. */
    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), fail_bus, TAG, "i2c dev");

    /* Probe: if the panel doesn't ACK, bail so the caller skips the display. */
    ESP_GOTO_ON_ERROR(i2c_master_probe(s_bus, cfg->i2c_addr, I2C_TIMEOUT_MS), fail_dev, TAG,
                      "no SSD1306 at 0x%02x", cfg->i2c_addr);

    for (size_t i = 0; i < sizeof(SSD1306_INIT); i++)
        ESP_GOTO_ON_ERROR(ssd1306_cmd(SSD1306_INIT[i]), fail_dev, TAG, "init[%zu]", i);

    /* Boot banner so a working panel is obvious before the first snapshot. */
    fb_clear();
    fb_text(0, 0, "ESPIR-MASTER");
    fb_text(0, 2, "BOOT");
    ESP_GOTO_ON_ERROR(ssd1306_flush(), fail_dev, TAG, "flush");

    ESP_GOTO_ON_FALSE(xTaskCreate(oled_task, "espir_oled", 3072, NULL, 3, &s_task) == pdPASS,
                      ESP_ERR_NO_MEM, fail_dev, TAG, "task");

    ESP_LOGI(TAG, "SSD1306 128x32 on sda=%d scl=%d addr=0x%02x",
             cfg->sda_gpio, cfg->scl_gpio, cfg->i2c_addr);
    return ESP_OK;

fail_dev:
    i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
fail_bus:
    i2c_del_master_bus(s_bus);
    s_bus = NULL;
    return ret;
}

void espir_oled_update(const espir_info_t *info)
{
    if (!info) return;
    s_info = *info;
    if (s_task) xTaskNotifyGive(s_task);
}
