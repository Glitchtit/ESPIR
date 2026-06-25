/*
 * espir_store.h — persist learned IR codes in NVS, one per slot.
 *
 * Each slot holds a record: [kind:u8][carrier_khz:u16 LE][blob...], where blob is the
 * code payload defined in espir_proto.h. The chunked program API lets the host write a
 * code that is too large for a single ZCL frame (program_begin -> program_chunk* ->
 * program_commit); program_single is the one-frame path.
 */
#ifndef ESPIR_STORE_H
#define ESPIR_STORE_H

#include "esp_err.h"
#include "espir_code.h"

esp_err_t espir_store_init(void);

esp_err_t espir_store_save(uint8_t slot, const espir_code_t *c);
esp_err_t espir_store_load(uint8_t slot, espir_code_t *c);
esp_err_t espir_store_clear(uint8_t slot);
int       espir_store_count(void);          /* number of slots (Kconfig) */
bool      espir_store_occupied(uint8_t slot); /* true if the slot holds a code (cheap existence check) */

esp_err_t espir_store_program_single(uint8_t slot, espir_kind_t kind, uint16_t carrier_khz,
                                     const uint8_t *blob, int len);
esp_err_t espir_store_program_begin(uint8_t slot, espir_kind_t kind, uint16_t carrier_khz,
                                    uint16_t total_len);
esp_err_t espir_store_program_chunk(uint8_t slot, uint8_t seq, const uint8_t *data, int len);
esp_err_t espir_store_program_commit(uint8_t slot);

#endif /* ESPIR_STORE_H */
