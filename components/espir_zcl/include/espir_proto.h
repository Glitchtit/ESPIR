/*
 * espir_proto.h — ESPIR device<->host contract.
 *
 * Custom ZCL cluster 0xFC00. This file is the single source of truth for cluster,
 * attribute, and command IDs, the learn-status / kind enums, and the code-blob wire
 * format. It is pure macros/enums (no ESP-IDF or Zigbee SDK dependency) so it compiles
 * in any C/C++ translation unit.
 *
 * NOTE: z2m/espir.js MUST mirror every value in this file. Change one, change the other.
 */
#ifndef ESPIR_PROTO_H
#define ESPIR_PROTO_H

#include <stdint.h>

/* ---- Cluster ------------------------------------------------------------- */
/* Private cluster in the 0xFC00-0xFFFF manufacturer range. We deliberately use NO
 * manufacturer-specific framing (manufacturer code unset) to keep the Z2M converter
 * simple — it is treated as a plain custom cluster. */
#define ESPIR_CLUSTER_ID            0xFC00

/* Clusters in the 0xFC00-0xFFFF range are manufacturer-specific; Zigbee2MQTT/herdsman can
 * only resolve them for INBOUND frames (reports/read-responses) when they carry a
 * manufacturer code. The device registers/reports its attributes with this code and the
 * Z2M converter declares the same `manufacturerCode`. Any value works as long as both match. */
#define ESPIR_MANUF_CODE            0x1037

/* ---- Attributes (server side) -------------------------------------------- */
#define ESPIR_ATTR_SLOT_COUNT       0x0000  /* u8,  ro            */
#define ESPIR_ATTR_ACTIVE_LEARN     0x0001  /* u8,  rw (0xFF idle)*/
#define ESPIR_ATTR_LEARN_STATUS     0x0002  /* enum8, ro, reportable */
#define ESPIR_ATTR_LAST_SLOT        0x0003  /* u8,  ro            */
#define ESPIR_ATTR_LAST_CODE        0x0004  /* octstr, ro (code blob) */
#define ESPIR_ATTR_LAST_KIND        0x0005  /* enum8, ro          */
#define ESPIR_ATTR_FW_ROLE          0x0006  /* enum8, ro          */
#define ESPIR_ATTR_LAST_CARRIER     0x0007  /* u16, ro (kHz) — carrier of last learned code */

/* ---- Commands (client -> server) ----------------------------------------- */
#define ESPIR_CMD_LEARN             0x00    /* {slot:u8}                                   */
#define ESPIR_CMD_SEND              0x01    /* {slot:u8}                                   */
#define ESPIR_CMD_CLEAR            0x02    /* {slot:u8}                                   */
#define ESPIR_CMD_PROGRAM           0x03    /* {slot:u8, kind:u8, carrier_khz:u16, code:octstr} */
#define ESPIR_CMD_PROGRAM_BEGIN     0x04    /* {slot:u8, kind:u8, carrier_khz:u16, total_len:u16} */
#define ESPIR_CMD_PROGRAM_CHUNK     0x05    /* {slot:u8, seq:u8, data:octstr}              */
#define ESPIR_CMD_PROGRAM_COMMIT    0x06    /* {slot:u8}                                   */
#define ESPIR_CMD_COPY_TO           0x07    /* {slot:u8, ieee:u64} master->slave copy one slot   */
#define ESPIR_CMD_COPY_ALL          0x08    /* {ieee:u64} master->slave copy every non-empty slot */

/* ---- Enums --------------------------------------------------------------- */
typedef enum {
    ESPIR_LEARN_IDLE     = 0,
    ESPIR_LEARN_WAITING  = 1,
    ESPIR_LEARN_CAPTURED = 2,
    ESPIR_LEARN_FAILED   = 3,
} espir_learn_status_t;

typedef enum {
    ESPIR_KIND_RAW = 0,   /* code blob = u16 LE durations (us), mark,space,... starting mark */
    ESPIR_KIND_NEC = 1,   /* code blob = address(u16 LE) + command(u16 LE) = 4 bytes         */
} espir_kind_t;

typedef enum {
    ESPIR_ROLE_MASTER = 0,
    ESPIR_ROLE_SLAVE  = 1,
} espir_role_t;

/* ---- Sentinels / limits -------------------------------------------------- */
#define ESPIR_SLOT_IDLE             0xFF    /* active_learn_slot value meaning "not learning" */
#define ESPIR_RAW_MAX_SYMBOLS       512     /* max mark/space entries in a raw code            */
#define ESPIR_RAW_MAX_BYTES         (ESPIR_RAW_MAX_SYMBOLS * 2)
#define ESPIR_NEC_BLOB_BYTES        4
#define ESPIR_CARRIER_DEFAULT_KHZ   38

#endif /* ESPIR_PROTO_H */
