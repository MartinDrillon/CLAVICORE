#include "diag_log.h"

#include <stdarg.h>
#include <stdio.h>

static bool status_supports_bitmap(diag_status_code_t code) {
    return code == DIAG_STATUS_CHANNEL_DIVERGENCE || code == DIAG_STATUS_BOARD_REFUSED_BUSY ||
           code == DIAG_STATUS_FAULT_DURING_TEST;
}

static int write_line(char *out, size_t capacity, const char *fmt, ...) {
    va_list args;
    int written;

    if (out == NULL || capacity == 0u || fmt == NULL) {
        return -1;
    }

    va_start(args, fmt);
    written = vsnprintf(out, capacity, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= capacity) {
        if (capacity > 0u) {
            out[0] = '\0';
        }
        return -1;
    }
    return written;
}

static void format_uid_hex(char out[17], const uint8_t uid[CAN_UID_SIZE]) {
    static const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0u; i < CAN_UID_SIZE; ++i) {
        out[i * 2u] = hex[(uid[i] >> 4u) & 0x0Fu];
        out[i * 2u + 1u] = hex[uid[i] & 0x0Fu];
    }
    out[16] = '\0';
}

const char *diag_status_code_name(diag_status_code_t code) {
    switch (code) {
    case DIAG_STATUS_NO_RESPONSE: return "NO_RESPONSE";
    case DIAG_STATUS_PROTO_MISMATCH: return "PROTO_MISMATCH";
    case DIAG_STATUS_INCOMPLETE: return "INCOMPLETE";
    case DIAG_STATUS_CAN_TX_FAIL: return "CAN_TX_FAIL";
    case DIAG_STATUS_RX_OVERRUN: return "RX_OVERRUN";
    case DIAG_STATUS_BAD_FRAME: return "BAD_FRAME";
    case DIAG_STATUS_AMBIGUOUS_TAG: return "AMBIGUOUS_TAG";
    case DIAG_STATUS_BUSY: return "BUSY";
    case DIAG_STATUS_UID_UNKNOWN: return "UID_UNKNOWN";
    case DIAG_STATUS_TABLE_INVALID: return "TABLE_INVALID";
    case DIAG_STATUS_SEED_DIFFERS: return "SEED_DIFFERS";
    case DIAG_STATUS_SELFTEST_STARTED: return "SELFTEST_STARTED";
    case DIAG_STATUS_SELFTEST_REFUSED_PLAYING: return "SELFTEST_REFUSED_PLAYING";
    case DIAG_STATUS_BOARD_MISSING: return "BOARD_MISSING";
    case DIAG_STATUS_TABLE_WRITTEN: return "TABLE_WRITTEN";
    case DIAG_STATUS_TABLE_NOT_WRITTEN_INCOMPLETE: return "TABLE_NOT_WRITTEN_INCOMPLETE";
    case DIAG_STATUS_TABLE_DIFFERS: return "TABLE_DIFFERS";
    case DIAG_STATUS_TABLE_PARTIAL: return "TABLE_PARTIAL";
    case DIAG_STATUS_TABLE_ERROR_FINAL: return "TABLE_ERROR_FINAL";
    case DIAG_STATUS_FLASH_TABLE_INVALID: return "FLASH_TABLE_INVALID";
    case DIAG_STATUS_UID_NOT_IN_TABLE: return "UID_NOT_IN_TABLE";
    case DIAG_STATUS_CHANNEL_DIVERGENCE: return "CHANNEL_DIVERGENCE";
    case DIAG_STATUS_DUPLICATE_TAG: return "DUPLICATE_TAG";
    case DIAG_STATUS_SELFTEST_UNCALIBRATED: return "SELFTEST_UNCALIBRATED";
    case DIAG_STATUS_BOARD_REFUSED_BUSY: return "BOARD_REFUSED_BUSY";
    case DIAG_STATUS_FAULT_DURING_TEST: return "FAULT_DURING_TEST";
    case DIAG_STATUS_MAINTENANCE_BUSY: return "MAINTENANCE_BUSY";
    case DIAG_STATUS_MAINTENANCE_NO_TABLE: return "MAINTENANCE_NO_TABLE";
    default: return NULL;
    }
}

const char *diag_selftest_status_name(can_selftest_status_t status) {
    switch (status) {
    case CAN_SELFTEST_STATUS_OK: return "OK";
    case CAN_SELFTEST_STATUS_REFUSED_BUSY: return "REFUSED_BUSY";
    case CAN_SELFTEST_STATUS_ABORTED: return "ABORTED";
    case CAN_SELFTEST_STATUS_UNCALIBRATED: return "UNCALIBRATED";
    default: return NULL;
    }
}

const char *diag_note_dropped_reason_name(diag_note_dropped_reason_t reason) {
    switch (reason) {
    case DIAG_NOTE_DROPPED_OUT_OF_RANGE: return "out_of_range";
    case DIAG_NOTE_DROPPED_TABLE_INVALID: return "table_invalid";
    case DIAG_NOTE_DROPPED_NOTE_UNASSIGNED: return "note_unassigned";
    case DIAG_NOTE_DROPPED_BOARD_OFFLINE: return "board_offline";
    case DIAG_NOTE_DROPPED_SELFTEST_ACTIVE: return "selftest_active";
    case DIAG_NOTE_DROPPED_TX_FULL: return "tx_full";
    case DIAG_NOTE_DROPPED_PROTOCOL: return "protocol";
    default: return NULL;
    }
}

const char *diag_card_state_name(diag_card_state_t state) {
    switch (state) {
    case DIAG_CARD_STATE_OK: return "OK";
    case DIAG_CARD_STATE_MISSING: return "MISSING";
    case DIAG_CARD_STATE_NOT_OK: return "NOT_OK";
    default: return NULL;
    }
}

size_t diag_format_uid_line(char *out, size_t capacity, const diag_uid_report_t *report) {
    char uid_hex[17];
    int written;

    if (report == NULL) {
        return 0u;
    }

    format_uid_hex(uid_hex, report->uid);
    written = write_line(out, capacity, "[UID] pos=%u uid=%s fw=%u.%u.%u proto=%u reply_to=%u",
                         (unsigned)report->position, uid_hex, (unsigned)report->fw_major,
                         (unsigned)report->fw_minor, (unsigned)report->fw_patch,
                         (unsigned)report->proto_version, (unsigned)report->reply_to);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_status_line(char *out, size_t capacity, diag_status_code_t code) {
    const char *name = diag_status_code_name(code);
    int written;

    if (name == NULL) {
        return 0u;
    }

    written = write_line(out, capacity, "[STATUS] code=%s", name);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_status_position_line(char *out, size_t capacity, diag_status_code_t code,
                                        uint8_t position) {
    const char *name = diag_status_code_name(code);
    int written;

    if (name == NULL) {
        return 0u;
    }

    written = write_line(out, capacity, "[STATUS] code=%s pos=%u", name, (unsigned)position);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_status_bitmap_line(char *out, size_t capacity, diag_status_code_t code,
                                      uint8_t position, uint16_t bitmap) {
    const char *name = diag_status_code_name(code);
    int written;

    if (name == NULL || !status_supports_bitmap(code) || !can_selftest_bits_are_valid(bitmap)) {
        return 0u;
    }

    written = write_line(out, capacity, "[STATUS] code=%s pos=%u bitmap=0x%03X", name,
                         (unsigned)position, (unsigned)bitmap);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_selftest_line(char *out, size_t capacity,
                                 const diag_selftest_report_t *report) {
    const char *status_name;
    int written;

    if (report == NULL || !can_selftest_bits_are_valid(report->occupation_bits) ||
        !can_selftest_bits_are_valid(report->fault_bits)) {
        return 0u;
    }

    status_name = diag_selftest_status_name(report->test_status);
    if (status_name == NULL) {
        return 0u;
    }

    written = write_line(out, capacity,
                         "[SELFTEST] req=%u pos=%u status=%s bitmap=0x%03X faults=0x%03X tag=%04X",
                         (unsigned)report->request_id, (unsigned)report->position, status_name,
                         (unsigned)report->occupation_bits, (unsigned)report->fault_bits,
                         (unsigned)report->uid_tag);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_selftest_raw_line(char *out, size_t capacity,
                                     const diag_selftest_raw_report_t *report) {
    int written;

    if (report == NULL || report->channel >= CAN_IPROPI_CHANNEL_COUNT ||
        report->idle_avg > CAN_ADC_12BIT_MAX || report->probe_avg > CAN_ADC_12BIT_MAX) {
        return 0u;
    }

    written = write_line(out, capacity, "[SELFTEST_RAW] req=%u pos=%u ch=%u idle=%u probe=%u",
                         (unsigned)report->request_id, (unsigned)report->position,
                         (unsigned)report->channel, (unsigned)report->idle_avg,
                         (unsigned)report->probe_avg);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_selftest_raw_delta_line(char *out, size_t capacity,
                                           const diag_selftest_raw_report_t *report) {
    const int delta = (int)report->probe_avg - (int)report->idle_avg;
    int written;

    if (report == NULL || report->channel >= CAN_IPROPI_CHANNEL_COUNT ||
        report->idle_avg > CAN_ADC_12BIT_MAX || report->probe_avg > CAN_ADC_12BIT_MAX) {
        return 0u;
    }

    written = write_line(out, capacity, "[SELFTEST_RAW] req=%u pos=%u ch=%u delta=%d",
                         (unsigned)report->request_id, (unsigned)report->position,
                         (unsigned)report->channel, delta);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_boot_line(char *out, size_t capacity, const char *table_state, uint8_t cards) {
    int written;

    if (table_state == NULL || table_state[0] == '\0') {
        return 0u;
    }
    written = write_line(out, capacity, "[BOOT] table=%s cards=%u", table_state,
                         (unsigned)cards);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_help_line(char *out, size_t capacity) {
    const int written = write_line(
        out, capacity,
        "[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash");
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_trigger_line(char *out, size_t capacity, uint8_t request_id) {
    const int written = write_line(
        out, capacity,
        "[TRIGGER] bouton Moteur recu -> auto-test lance (req=%u)", (unsigned)request_id);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_trigger_refused_line(char *out, size_t capacity, const char *reason) {
    int written;

    if (reason == NULL || reason[0] == '\0') {
        return 0u;
    }
    written = write_line(out, capacity, "[TRIGGER] bouton Moteur recu -> refus: %s", reason);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_table_loaded_line(char *out, size_t capacity, bool loaded,
                                     const char *detail) {
    int written;

    if (detail == NULL) {
        detail = "";
    }
    if (loaded) {
        written = write_line(out, capacity, "[TABLE] loaded%s%s", detail[0] ? " " : "", detail);
    } else {
        written = write_line(out, capacity, "[TABLE] invalid%s%s", detail[0] ? " " : "", detail);
    }
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_table_entry_line(char *out, size_t capacity, const diag_table_entry_t *entry) {
    char uid_hex[17];
    int written;

    if (entry == NULL || !can_selftest_bits_are_valid(entry->bitmap)) {
        return 0u;
    }
    format_uid_hex(uid_hex, entry->uid);
    if (entry->has_keys) {
        written = write_line(out, capacity,
                             "[TABLE] pos=%u uid=%s bitmap=0x%03X keys=%u-%u",
                             (unsigned)entry->position, uid_hex, (unsigned)entry->bitmap,
                             (unsigned)entry->first_key, (unsigned)entry->last_key);
    } else {
        written = write_line(out, capacity, "[TABLE] pos=%u uid=%s bitmap=0x%03X keys=none",
                             (unsigned)entry->position, uid_hex, (unsigned)entry->bitmap);
    }
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_table_diff_line(char *out, size_t capacity, uint8_t position,
                                   uint16_t old_bitmap, uint16_t new_bitmap) {
    int written;

    if (!can_selftest_bits_are_valid(old_bitmap) || !can_selftest_bits_are_valid(new_bitmap)) {
        return 0u;
    }
    written = write_line(out, capacity, "[TABLE] diff pos=%u old=0x%03X new=0x%03X",
                         (unsigned)position, (unsigned)old_bitmap, (unsigned)new_bitmap);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_diff_line(char *out, size_t capacity, uint8_t position, uint16_t old_bitmap,
                             uint16_t new_bitmap) {
    int written;

    if (!can_selftest_bits_are_valid(old_bitmap) || !can_selftest_bits_are_valid(new_bitmap)) {
        return 0u;
    }
    written = write_line(out, capacity, "[DIFF] pos=%u ancien=0x%03X nouveau=0x%03X",
                         (unsigned)position, (unsigned)old_bitmap, (unsigned)new_bitmap);
    return (written < 0) ? 0u : (size_t)written;
}

bool diag_format_note_name_fr(char *out, size_t capacity, uint8_t midi_note) {
    static const char *const note_names[] = {
        "DO", "DO#", "RE", "RE#", "MI", "FA", "FA#", "SOL", "SOL#", "LA", "LA#", "SI",
    };
    const uint8_t semitone = midi_note % 12u;
    const uint8_t octave = (uint8_t)(midi_note / 12u);
    int written;

    if (out == NULL || capacity == 0u || midi_note < COMMISSIONING_MIDI_NOTE_MIN ||
        midi_note > COMMISSIONING_MIDI_NOTE_MAX || octave < 2u) {
        return false;
    }

    written = snprintf(out, capacity, "%s%u(%u)", note_names[semitone], (unsigned)(octave - 2u),
                       (unsigned)midi_note);
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return false;
    }
    return true;
}

size_t diag_format_card_line(char *out, size_t capacity, const diag_card_report_t *report) {
    char uid_hex[17];
    const char *state_name;
    const char *channels_text;
    const char *keys_text;
    const char *notes_text;
    int written;

    if (report == NULL || !can_selftest_bits_are_valid(report->bitmap)) {
        return 0u;
    }

    state_name = diag_card_state_name(report->state);
    if (state_name == NULL) {
        return 0u;
    }
    channels_text = (report->channels_text != NULL && report->channels_text[0] != '\0')
                        ? report->channels_text
                        : "-";
    keys_text =
        (report->keys_text != NULL && report->keys_text[0] != '\0') ? report->keys_text : "-";
    notes_text =
        (report->notes_text != NULL && report->notes_text[0] != '\0') ? report->notes_text : "-";

    format_uid_hex(uid_hex, report->uid);
    written = write_line(
        out, capacity,
        "[CARD] pos=%u node=%u uid=%s state=%s bitmap=0x%03X channels=%s keys=%s notes=%s",
        (unsigned)report->position, (unsigned)report->node, uid_hex, state_name,
        (unsigned)report->bitmap, channels_text, keys_text, notes_text);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_card_unknown_line(char *out, size_t capacity,
                                     const uint8_t uid[CAN_UID_SIZE]) {
    char uid_hex[17];
    int written;

    if (uid == NULL) {
        return 0u;
    }

    format_uid_hex(uid_hex, uid);
    written = write_line(out, capacity, "[CARD_UNKNOWN] uid=%s", uid_hex);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_summary_line(char *out, size_t capacity,
                                const diag_summary_report_t *report) {
    const char *missing_keys_text;
    const char *table_text;
    int written;

    if (report == NULL || report->keys_ok > COMMISSIONING_KEY_COUNT) {
        return 0u;
    }

    missing_keys_text = (report->missing_keys_text != NULL && report->missing_keys_text[0] != '\0')
                            ? report->missing_keys_text
                            : "-";
    table_text = (report->table_text != NULL && report->table_text[0] != '\0')
                     ? report->table_text
                     : "NOT_WRITTEN(?)";

    written = write_line(
        out, capacity,
        "[SUMMARY] cards_ok=%u/%u channels=%u keys=%u/%u missing_keys=%s table=%s",
        (unsigned)report->cards_ok, (unsigned)report->cards_total,
        (unsigned)report->channels_ok, (unsigned)report->keys_ok,
        (unsigned)COMMISSIONING_KEY_COUNT, missing_keys_text, table_text);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_note_line(char *out, size_t capacity, uint8_t midi_note, uint8_t key,
                             uint8_t position, uint8_t channel) {
    int written;

    if (channel >= CAN_IPROPI_CHANNEL_COUNT) {
        return 0u;
    }
    written = write_line(out, capacity, "[NOTE] midi=%u key=%u pos=%u ch=%u",
                         (unsigned)midi_note, (unsigned)key, (unsigned)position,
                         (unsigned)channel);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_mode_line(char *out, size_t capacity, bool maintenance_enabled) {
    const int written = write_line(out, capacity, "[MODE] maintenance=%s",
                                   maintenance_enabled ? "ON" : "OFF");
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_push_line(char *out, size_t capacity, uint8_t midi_note, uint8_t key,
                             uint8_t position, uint8_t channel, diag_push_event_t event) {
    const char *event_name;
    int written;

    if (channel >= CAN_IPROPI_CHANNEL_COUNT) {
        return 0u;
    }
    switch (event) {
    case DIAG_PUSH_EVENT_START:
        event_name = "start";
        break;
    case DIAG_PUSH_EVENT_STOP:
        event_name = "stop";
        break;
    case DIAG_PUSH_EVENT_TIMEOUT:
        event_name = "timeout";
        break;
    default:
        return 0u;
    }

    written = write_line(out, capacity, "[PUSH] midi=%u key=%u pos=%u ch=%u %s",
                         (unsigned)midi_note, (unsigned)key, (unsigned)position,
                         (unsigned)channel, event_name);
    return (written < 0) ? 0u : (size_t)written;
}

size_t diag_format_note_dropped_line(char *out, size_t capacity, uint8_t midi_note,
                                     diag_note_dropped_reason_t reason) {
    const char *name = diag_note_dropped_reason_name(reason);
    int written;

    if (name == NULL) {
        return 0u;
    }
    written = write_line(out, capacity, "[NOTE_DROPPED] midi=%u reason=%s", (unsigned)midi_note,
                         name);
    return (written < 0) ? 0u : (size_t)written;
}
