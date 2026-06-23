/*
 * espir_code.h — in-memory IR code model + NEC codec + blob (de)serialization.
 * Pure C, no ESP-IDF dependency beyond stdint/stdbool. Shared by espir_ir, espir_store,
 * and the app layer.
 */
#ifndef ESPIR_CODE_H
#define ESPIR_CODE_H

#include <stdint.h>
#include <stdbool.h>
#include "espir_proto.h"

/* One IR code, in memory. For NEC, `nec` holds the 4 raw frame bytes and `symbols` is
 * unused; for RAW, `symbols` holds the mark/space envelope (microseconds, mark first). */
typedef struct {
    espir_kind_t kind;
    uint16_t carrier_khz;
    uint16_t n_symbols;                       /* RAW: number of mark/space durations */
    uint16_t symbols[ESPIR_RAW_MAX_SYMBOLS];  /* RAW durations in us, mark, space, ... */
    uint8_t  nec[ESPIR_NEC_BLOB_BYTES];       /* NEC: addr, ~addr, cmd, ~cmd (as captured) */
} espir_code_t;

/* NEC. decode returns true and fills out4 (the 4 frame bytes) if the envelope is NEC.
 * encode writes the mark/space envelope for the 4 bytes and returns the symbol count
 * (or -1 if it would overflow `max`). */
bool espir_nec_decode(const uint16_t *sym, int n, uint8_t out4[ESPIR_NEC_BLOB_BYTES]);
int  espir_nec_encode(const uint8_t in4[ESPIR_NEC_BLOB_BYTES], uint16_t *sym, int max);

/* Blob = the on-wire / NVS payload (see espir_proto.h). Returns blob length or -1. */
int  espir_code_to_blob(const espir_code_t *c, uint8_t *buf, int max);
bool espir_code_from_blob(espir_code_t *c, espir_kind_t kind, uint16_t carrier_khz,
                          const uint8_t *buf, int len);

/* If a RAW capture decodes as NEC, rewrite it in place to the compact NEC form. */
void espir_code_try_compact(espir_code_t *c);

#endif /* ESPIR_CODE_H */
