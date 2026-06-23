#include "espir_code.h"
#include <string.h>

/* NEC protocol timings (microseconds). */
#define NEC_HDR_MARK    9000
#define NEC_HDR_SPACE   4500
#define NEC_BIT_MARK    560
#define NEC_ONE_SPACE   1690
#define NEC_ZERO_SPACE  560
#define NEC_SPACE_MID   ((NEC_ONE_SPACE + NEC_ZERO_SPACE) / 2)  /* ~1125 */
#define NEC_SYMBOLS     (2 + 32 * 2 + 1)  /* header + 32 bits + stop = 67 */

/* Match within +/-30%. */
static inline bool match(uint16_t v, uint16_t target)
{
    return v >= (uint16_t)(target * 7 / 10) && v <= (uint16_t)(target * 13 / 10);
}

bool espir_nec_decode(const uint16_t *s, int n, uint8_t out4[ESPIR_NEC_BLOB_BYTES])
{
    if (n < NEC_SYMBOLS - 1) return false;            /* tolerate a missing trailing stop */
    if (!match(s[0], NEC_HDR_MARK) || !match(s[1], NEC_HDR_SPACE)) return false;

    uint32_t bits = 0;
    for (int i = 0; i < 32; i++) {
        uint16_t mark = s[2 + i * 2];
        uint16_t space = s[3 + i * 2];
        if (!match(mark, NEC_BIT_MARK)) return false;
        if (space > NEC_SPACE_MID) bits |= (uint32_t)1 << i;   /* LSB first */
    }
    out4[0] = bits & 0xff;
    out4[1] = (bits >> 8) & 0xff;
    out4[2] = (bits >> 16) & 0xff;
    out4[3] = (bits >> 24) & 0xff;
    return true;
}

int espir_nec_encode(const uint8_t in4[ESPIR_NEC_BLOB_BYTES], uint16_t *o, int max)
{
    int k = 0;
#define PUT(x) do { if (k >= max) return -1; o[k++] = (uint16_t)(x); } while (0)
    PUT(NEC_HDR_MARK);
    PUT(NEC_HDR_SPACE);
    for (int b = 0; b < 32; b++) {
        int bit = (in4[b / 8] >> (b % 8)) & 1;
        PUT(NEC_BIT_MARK);
        PUT(bit ? NEC_ONE_SPACE : NEC_ZERO_SPACE);
    }
    PUT(NEC_BIT_MARK);  /* stop */
#undef PUT
    return k;
}

int espir_code_to_blob(const espir_code_t *c, uint8_t *buf, int max)
{
    if (c->kind == ESPIR_KIND_NEC) {
        if (max < ESPIR_NEC_BLOB_BYTES) return -1;
        memcpy(buf, c->nec, ESPIR_NEC_BLOB_BYTES);
        return ESPIR_NEC_BLOB_BYTES;
    }
    int len = c->n_symbols * 2;
    if (len > max) return -1;
    for (int i = 0; i < c->n_symbols; i++) {
        buf[i * 2]     = c->symbols[i] & 0xff;
        buf[i * 2 + 1] = c->symbols[i] >> 8;
    }
    return len;
}

bool espir_code_from_blob(espir_code_t *c, espir_kind_t kind, uint16_t carrier_khz,
                          const uint8_t *buf, int len)
{
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    c->carrier_khz = carrier_khz ? carrier_khz : ESPIR_CARRIER_DEFAULT_KHZ;

    if (kind == ESPIR_KIND_NEC) {
        if (len < ESPIR_NEC_BLOB_BYTES) return false;
        memcpy(c->nec, buf, ESPIR_NEC_BLOB_BYTES);
        return true;
    }
    if (len < 2 || len > ESPIR_RAW_MAX_BYTES || (len & 1)) return false;
    c->n_symbols = len / 2;
    for (int i = 0; i < c->n_symbols; i++) {
        c->symbols[i] = (uint16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8));
    }
    return true;
}

void espir_code_try_compact(espir_code_t *c)
{
    if (c->kind != ESPIR_KIND_RAW) return;
    uint8_t four[ESPIR_NEC_BLOB_BYTES];
    if (espir_nec_decode(c->symbols, c->n_symbols, four)) {
        memcpy(c->nec, four, ESPIR_NEC_BLOB_BYTES);
        c->kind = ESPIR_KIND_NEC;
    }
}
