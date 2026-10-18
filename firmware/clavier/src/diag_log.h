#ifndef DIAG_LOG_H
#define DIAG_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "can_protocol.h"

#define DIAG_LOG_LINE_MAX 96u

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
} diag_status_code_t;

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

const char *diag_status_code_name(diag_status_code_t code);
const char *diag_selftest_status_name(can_selftest_status_t status);

size_t diag_format_uid_line(char *out, size_t capacity, const diag_uid_report_t *report);
size_t diag_format_status_line(char *out, size_t capacity, diag_status_code_t code);
size_t diag_format_status_position_line(char *out, size_t capacity, diag_status_code_t code,
                                        uint8_t position);
size_t diag_format_status_bitmap_line(char *out, size_t capacity, diag_status_code_t code,
                                      uint8_t position, uint16_t bitmap);
size_t diag_format_selftest_line(char *out, size_t capacity, const diag_selftest_report_t *report);

#endif
