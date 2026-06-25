#include "espir_store.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sdkconfig.h"

static const char *TAG = "espir_store";

#ifndef CONFIG_ESPIR_SLOT_COUNT
#define CONFIG_ESPIR_SLOT_COUNT 32
#endif

#define NVS_NS "espir"
#define REC_HDR 3                       /* kind(1) + carrier(2) */
#define REC_MAX (REC_HDR + ESPIR_RAW_MAX_BYTES)

/* In-progress chunked program assembly (one at a time). */
static struct {
    bool active;
    uint8_t slot;
    espir_kind_t kind;
    uint16_t carrier_khz;
    uint16_t total_len;
    uint16_t got;
    uint8_t buf[ESPIR_RAW_MAX_BYTES];
} s_prog;

int espir_store_count(void) { return CONFIG_ESPIR_SLOT_COUNT; }

static void slot_key(uint8_t slot, char key[16]) { snprintf(key, 16, "slot_%02u", slot); }

esp_err_t espir_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t write_record(uint8_t slot, espir_kind_t kind, uint16_t carrier_khz,
                              const uint8_t *blob, int len)
{
    if (slot >= CONFIG_ESPIR_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    if (len < 0 || len > ESPIR_RAW_MAX_BYTES) return ESP_ERR_INVALID_SIZE;

    uint8_t rec[REC_MAX];
    rec[0] = (uint8_t)kind;
    rec[1] = carrier_khz & 0xff;
    rec[2] = carrier_khz >> 8;
    memcpy(rec + REC_HDR, blob, len);

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    char key[16];
    slot_key(slot, key);
    esp_err_t err = nvs_set_blob(h, key, rec, REC_HDR + len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t espir_store_save(uint8_t slot, const espir_code_t *c)
{
    uint8_t blob[ESPIR_RAW_MAX_BYTES];
    int len = espir_code_to_blob(c, blob, sizeof(blob));
    if (len < 0) return ESP_ERR_INVALID_SIZE;
    return write_record(slot, c->kind, c->carrier_khz, blob, len);
}

esp_err_t espir_store_load(uint8_t slot, espir_code_t *c)
{
    if (slot >= CONFIG_ESPIR_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    /* On a fresh device the namespace doesn't exist yet — that's "empty slot", not an
     * error worth logging, so don't use ESP_RETURN_ON_ERROR here. */
    esp_err_t oerr = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (oerr != ESP_OK) return oerr;
    char key[16];
    slot_key(slot, key);
    uint8_t rec[REC_MAX];
    size_t len = sizeof(rec);
    esp_err_t err = nvs_get_blob(h, key, rec, &len);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (len < REC_HDR) return ESP_ERR_INVALID_SIZE;

    espir_kind_t kind = (espir_kind_t)rec[0];
    uint16_t carrier = rec[1] | (rec[2] << 8);
    return espir_code_from_blob(c, kind, carrier, rec + REC_HDR, (int)(len - REC_HDR))
               ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

bool espir_store_occupied(uint8_t slot)
{
    if (slot >= CONFIG_ESPIR_SLOT_COUNT) return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;  /* namespace absent = empty */
    char key[16];
    slot_key(slot, key);
    size_t len = 0;
    /* NULL out-buffer: nvs returns the blob size (or NOT_FOUND) without reading the payload. */
    esp_err_t err = nvs_get_blob(h, key, NULL, &len);
    nvs_close(h);
    return err == ESP_OK && len >= REC_HDR;
}

esp_err_t espir_store_clear(uint8_t slot)
{
    if (slot >= CONFIG_ESPIR_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    char key[16];
    slot_key(slot, key);
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;  /* already empty is fine */
}

esp_err_t espir_store_program_single(uint8_t slot, espir_kind_t kind, uint16_t carrier_khz,
                                     const uint8_t *blob, int len)
{
    return write_record(slot, kind, carrier_khz, blob, len);
}

esp_err_t espir_store_program_begin(uint8_t slot, espir_kind_t kind, uint16_t carrier_khz,
                                    uint16_t total_len)
{
    if (slot >= CONFIG_ESPIR_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    if (total_len > ESPIR_RAW_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    s_prog.active = true;
    s_prog.slot = slot;
    s_prog.kind = kind;
    s_prog.carrier_khz = carrier_khz;
    s_prog.total_len = total_len;
    s_prog.got = 0;
    return ESP_OK;
}

esp_err_t espir_store_program_chunk(uint8_t slot, uint8_t seq, const uint8_t *data, int len)
{
    (void)seq;
    if (!s_prog.active || s_prog.slot != slot) return ESP_ERR_INVALID_STATE;
    if (s_prog.got + len > s_prog.total_len || s_prog.got + len > ESPIR_RAW_MAX_BYTES)
        return ESP_ERR_INVALID_SIZE;
    memcpy(s_prog.buf + s_prog.got, data, len);
    s_prog.got += len;
    return ESP_OK;
}

esp_err_t espir_store_program_commit(uint8_t slot)
{
    if (!s_prog.active || s_prog.slot != slot) return ESP_ERR_INVALID_STATE;
    if (s_prog.got != s_prog.total_len) { s_prog.active = false; return ESP_ERR_INVALID_SIZE; }
    esp_err_t err = write_record(slot, s_prog.kind, s_prog.carrier_khz, s_prog.buf, s_prog.got);
    s_prog.active = false;
    return err;
}
