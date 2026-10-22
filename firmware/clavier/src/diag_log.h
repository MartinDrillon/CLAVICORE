#ifndef DIAG_LOG_H
#define DIAG_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "can_protocol.h"
#include "commissioning_table.h"

#define DIAG_LOG_LINE_MAX 256u
#define DIAG_NOTE_NAME_MAX 16u

typedef enum {
    DIAG_STATUS_NO_RESPONSE = 0x01,
    DIAG_STATUS_PROTO_MISMATCH = 0x02,
    DIAG_STATUS_INCOMPLETE = 0x03,
    DIAG_STATUS_CAN_TX_FAIL = 0x04,
    DIAG_STATUS_RX_OVERRUN = 0x05,
    DIAG_STATUS_BAD_FRAME = 0x06,
    DIAG_STATUS_AMBIGUOUS_TAG = 0x07,
    DIAG_STATUS_BUSY = 0x08,
    DIAG_STATUS_UID_UNKNOWN = 0x10,
    DIAG_STATUS_TABLE_INVALID = 0x11,
    DIAG_STATUS_SEED_DIFFERS = 0x12,
    DIAG_STATUS_SELFTEST_STARTED = 0x20,
    DIAG_STATUS_SELFTEST_REFUSED_PLAYING = 0x21,
    DIAG_STATUS_BOARD_MISSING = 0x22,
    DIAG_STATUS_TABLE_WRITTEN = 0x23,
    DIAG_STATUS_TABLE_NOT_WRITTEN_INCOMPLETE = 0x24,
    DIAG_STATUS_TABLE_DIFFERS = 0x25,
    DIAG_STATUS_TABLE_PARTIAL = 0x26,
    DIAG_STATUS_TABLE_ERROR_FINAL = 0x27,
    DIAG_STATUS_FLASH_TABLE_INVALID = 0x28,
    DIAG_STATUS_UID_NOT_IN_TABLE = 0x29,
    DIAG_STATUS_CHANNEL_DIVERGENCE = 0x2A,
    DIAG_STATUS_DUPLICATE_TAG = 0x2B,
    DIAG_STATUS_SELFTEST_UNCALIBRATED = 0x2C,
    DIAG_STATUS_BOARD_REFUSED_BUSY = 0x2D,
    DIAG_STATUS_FAULT_DURING_TEST = 0x2E,
    DIAG_STATUS_MAINTENANCE_BUSY = 0x2F,
    DIAG_STATUS_MAINTENANCE_NO_TABLE = 0x30,
    DIAG_STATUS_TABLE_PROVISIONAL = 0x31,
    // ADR-0013 : synchronisation du profil d'activation des solénoïdes.
    DIAG_STATUS_PROFILE_SYNC_FAILED = 0x32,
    DIAG_STATUS_PROFILE_DIVERGENT = 0x33,
    DIAG_STATUS_PROFILE_FALLBACK_IN_USE = 0x34,
    // ADR-0016 : un PROFILE_STATUS reçu ne peut être attribué à aucune carte de
    // `uid_position.h` (jamais jeté en silence).
    DIAG_STATUS_PROFILE_ACK_UNATTRIBUTED = 0x35,
    // ADR-0016 : carte présente dont aucun PROFILE_STATUS de boot n'a été vu
    // depuis le dernier scan — firmware Moteur antérieur à ADR-0013.
    DIAG_STATUS_PROFILE_UNSUPPORTED = 0x36,
    // ADR-0017 : annonce de boot spontanée hors scan de boot = redémarrage.
    DIAG_STATUS_CARD_REBOOTED = 0x37,
} diag_status_code_t;

typedef enum {
    DIAG_PUSH_EVENT_START = 0,
    DIAG_PUSH_EVENT_STOP,
    DIAG_PUSH_EVENT_TIMEOUT,
} diag_push_event_t;

typedef struct {
    uint8_t reply_to;
    uint8_t uid[CAN_UID_SIZE];
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;
    uint8_t proto_version;
    uint8_t position;
} diag_uid_report_t;

typedef struct {
    uint8_t request_id;
    uint8_t position;
    can_selftest_status_t test_status;
    uint16_t occupation_bits;
    uint16_t fault_bits;
    uint16_t uid_tag;
} diag_selftest_report_t;

typedef struct {
    uint8_t request_id;
    uint8_t position;
    uint8_t channel;
    uint16_t idle_avg;
    uint16_t probe_avg;
    // Diagnostic ADR-0009 : CAN_IPROPI_VALUE_INVALID si non calculable.
    uint16_t ohm_milliohm;
    uint16_t i100_ma;
} diag_selftest_raw_report_t;

// État persisté d'une table (ADR-0009 §3, §4).
typedef struct {
    commissioning_table_state_t state;
    uint8_t flags;
    uint8_t cards;
    uint16_t keys;
} diag_table_state_t;

// Synthèse ohmique d'une carte (ADR-0009 §5c) : calculée par le Clavier sur les
// canaux occupés, à partir des trames IPROPI_OHM.
typedef struct {
    uint8_t position;
    uint8_t count;
    uint16_t ohm_min_milliohm;
    uint16_t ohm_max_milliohm;
    uint8_t spread_percent;
    uint16_t i100_min_ma;
    uint16_t i100_max_ma;
    bool warn_i100_over_3a;
} diag_ohm_summary_t;

typedef struct {
    uint8_t position;
    uint8_t uid[CAN_UID_SIZE];
    uint16_t bitmap;
    uint8_t first_key;
    uint8_t last_key;
    bool has_keys;
} diag_table_entry_t;

typedef enum {
    DIAG_CARD_STATE_OK = 0,
    DIAG_CARD_STATE_MISSING,
    DIAG_CARD_STATE_NOT_OK,
    // ADR-0017 décision 4 : annonce de boot spontanée hors scan de boot.
    DIAG_CARD_STATE_REBOOT,
} diag_card_state_t;

typedef struct {
    uint8_t position;
    uint8_t node;
    uint8_t uid[CAN_UID_SIZE];
    diag_card_state_t state;
    uint16_t bitmap;
    const char *channels_text;
    const char *keys_text;
    const char *notes_text;
} diag_card_report_t;

typedef struct {
    uint8_t cards_ok;
    uint8_t cards_total;
    uint8_t channels_ok;
    uint8_t keys_ok;
    const char *missing_keys_text;
    const char *table_text;
} diag_summary_report_t;

// Identité du firmware pour la ligne de boot (ADR-0017 décision 2). `build_id`
// peut être NULL (affiché comme « inconnu »).
typedef struct {
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;
    uint8_t proto_version;
    const char *build_id;
    const char *table_state;
    uint8_t cards;
} diag_boot_report_t;

typedef enum {
    DIAG_NOTE_DROPPED_OUT_OF_RANGE = 0,
    DIAG_NOTE_DROPPED_TABLE_INVALID,
    DIAG_NOTE_DROPPED_NOTE_UNASSIGNED,
    DIAG_NOTE_DROPPED_BOARD_OFFLINE,
    DIAG_NOTE_DROPPED_SELFTEST_ACTIVE,
    DIAG_NOTE_DROPPED_TX_FULL,
    DIAG_NOTE_DROPPED_PROTOCOL,
} diag_note_dropped_reason_t;

const char *diag_status_code_name(diag_status_code_t code);
const char *diag_selftest_status_name(can_selftest_status_t status);
const char *diag_note_dropped_reason_name(diag_note_dropped_reason_t reason);
const char *diag_card_state_name(diag_card_state_t state);

size_t diag_format_uid_line(char *out, size_t capacity, const diag_uid_report_t *report);
size_t diag_format_status_line(char *out, size_t capacity, diag_status_code_t code);
size_t diag_format_status_position_line(char *out, size_t capacity, diag_status_code_t code,
                                        uint8_t position);
size_t diag_format_status_bitmap_line(char *out, size_t capacity, diag_status_code_t code,
                                      uint8_t position, uint16_t bitmap);
size_t diag_format_selftest_line(char *out, size_t capacity, const diag_selftest_report_t *report);
size_t diag_format_selftest_raw_line(char *out, size_t capacity,
                                     const diag_selftest_raw_report_t *report);
size_t diag_format_selftest_raw_delta_line(char *out, size_t capacity,
                                           const diag_selftest_raw_report_t *report);
size_t diag_format_selftest_ohm_line(char *out, size_t capacity,
                                     const diag_ohm_summary_t *summary);
size_t diag_format_boot_line(char *out, size_t capacity, const diag_boot_report_t *report);
size_t diag_format_help_line(char *out, size_t capacity);
// `source` : « console » ou « bouton » (ADR-0017 décision 5). Jamais NULL.
size_t diag_format_trigger_line(char *out, size_t capacity, const char *source, uint8_t request_id);
size_t diag_format_trigger_refused_line(char *out, size_t capacity, const char *source,
                                        const char *reason);
size_t diag_format_table_loaded_line(char *out, size_t capacity, bool loaded,
                                     const char *detail);
size_t diag_format_table_state_line(char *out, size_t capacity, const diag_table_state_t *state);
size_t diag_format_map_line(char *out, size_t capacity, uint8_t position, uint8_t channel,
                            uint8_t key, uint8_t midi_note);
size_t diag_format_table_entry_line(char *out, size_t capacity, const diag_table_entry_t *entry);
size_t diag_format_table_diff_line(char *out, size_t capacity, uint8_t position, uint16_t old_bitmap,
                                   uint16_t new_bitmap);
size_t diag_format_diff_line(char *out, size_t capacity, uint8_t position, uint16_t old_bitmap,
                             uint16_t new_bitmap);
bool diag_format_note_name_fr(char *out, size_t capacity, uint8_t midi_note);
size_t diag_format_card_line(char *out, size_t capacity, const diag_card_report_t *report);
size_t diag_format_card_unknown_line(char *out, size_t capacity,
                                     const uint8_t uid[CAN_UID_SIZE]);
size_t diag_format_summary_line(char *out, size_t capacity,
                                const diag_summary_report_t *report);
size_t diag_format_note_line(char *out, size_t capacity, uint8_t midi_note, uint8_t key,
                             uint8_t position, uint8_t channel);
size_t diag_format_mode_line(char *out, size_t capacity, bool maintenance_enabled);
size_t diag_format_push_line(char *out, size_t capacity, uint8_t midi_note, uint8_t key,
                             uint8_t position, uint8_t channel, diag_push_event_t event);
size_t diag_format_note_dropped_line(char *out, size_t capacity, uint8_t midi_note,
                                     diag_note_dropped_reason_t reason);

#endif
