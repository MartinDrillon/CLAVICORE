#ifndef SYSEX_UID_H
#define SYSEX_UID_H

// Encodage des messages SysEx Clavicore (ADR-0003 §6).
// C pur, sans dépendance au SDK : testable sur PC (tests/host/).
//
// Format : F0 7D 43 01 <type> <données...> F7
//   7D = ID non commercial / éducatif, 43 = 'C', 01 = version du format SysEx.

#include <stddef.h>
#include <stdint.h>

#define SYSEX_START 0xF0u
#define SYSEX_END 0xF7u
#define SYSEX_MANUFACTURER_ID 0x7Du
#define SYSEX_MAGIC 0x43u
#define SYSEX_FORMAT_VERSION 0x01u

#define SYSEX_TYPE_UID_REPORT 0x01u
#define SYSEX_TYPE_STATUS 0x02u

#define SYSEX_UID_REPORT_LEN 29u
#define SYSEX_STATUS_LEN 8u
#define SYSEX_MAX_LEN 32u  // limite de midi_send_sysex (ADR-0003 §6)

#define SYSEX_ORIGIN_REQUEST 0x00u
#define SYSEX_ORIGIN_BOOT 0x01u

typedef enum {
    SYSEX_STATUS_NO_RESPONSE = 0x01,
    SYSEX_STATUS_PROTO_MISMATCH = 0x02,
    SYSEX_STATUS_INCOMPLETE = 0x03,
    SYSEX_STATUS_CAN_TX_FAIL = 0x04,
    SYSEX_STATUS_RX_OVERRUN = 0x05,
    SYSEX_STATUS_BAD_FRAME = 0x06,
    SYSEX_STATUS_AMBIGUOUS_TAG = 0x07,
    SYSEX_STATUS_BUSY = 0x08,
    SYSEX_STATUS_UID_UNKNOWN = 0x10,
    SYSEX_STATUS_TABLE_INVALID = 0x11,
    SYSEX_STATUS_SEED_DIFFERS = 0x12,  // Phase 4, non émis en Phase 3
} sysex_status_code_t;

typedef struct {
    uint8_t origin;      // SYSEX_ORIGIN_REQUEST ou SYSEX_ORIGIN_BOOT
    uint8_t request_id;  // 1..127, 0 pour une annonce de boot
    uint8_t uid[8];      // ordre id[0]..id[7]
    uint8_t fw_major;    // 0..127
    uint8_t fw_minor;
    uint8_t fw_patch;
    uint8_t proto_version;  // 0..127
    uint8_t position;       // 0..4, 0x7F si inconnu
} sysex_uid_report_t;

// UID_REPORT : 16 nibbles (un par octet, poids fort en premier).
// Retourne le nombre d'octets écrits (SYSEX_UID_REPORT_LEN), ou 0 si un champ est
// hors plage 7 bits, si un pointeur est nul ou si `capacity` est insuffisante.
size_t sysex_encode_uid_report(uint8_t *out, size_t capacity, const sysex_uid_report_t *report);

// STATUS : F0 7D 43 01 02 <code> <detail> F7. Retourne SYSEX_STATUS_LEN, ou 0
// (code ou detail > 127, pointeur nul, capacité insuffisante).
size_t sysex_encode_status(uint8_t *out, size_t capacity, uint8_t code, uint8_t detail);

#endif
