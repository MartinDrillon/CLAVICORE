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

size_t diag_format_selftest_line(char *out, size_t capacity, const diag_selftest_report_t *report) {
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
