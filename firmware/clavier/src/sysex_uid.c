#include "sysex_uid.h"

static size_t write_header(uint8_t *out, uint8_t type) {
    out[0] = SYSEX_START;
    out[1] = SYSEX_MANUFACTURER_ID;
    out[2] = SYSEX_MAGIC;
    out[3] = SYSEX_FORMAT_VERSION;
    out[4] = type;
    return 5u;
}

size_t sysex_encode_uid_report(uint8_t *out, size_t capacity, const sysex_uid_report_t *report) {
    if (out == NULL || report == NULL || capacity < SYSEX_UID_REPORT_LEN) {
        return 0u;
    }
    if (report->origin > SYSEX_ORIGIN_BOOT || report->request_id > 127u ||
        report->fw_major > 127u || report->fw_minor > 127u || report->fw_patch > 127u ||
        report->proto_version > 127u || report->position > 127u) {
        return 0u;
    }

    size_t n = write_header(out, SYSEX_TYPE_UID_REPORT);
    out[n++] = report->origin;
    out[n++] = report->request_id;
    for (uint8_t i = 0u; i < 8u; ++i) {
        out[n++] = (uint8_t)(report->uid[i] >> 4u);   // nibble de poids fort d'abord
        out[n++] = (uint8_t)(report->uid[i] & 0x0Fu);
    }
    out[n++] = report->fw_major;
    out[n++] = report->fw_minor;
    out[n++] = report->fw_patch;
    out[n++] = report->proto_version;
    out[n++] = report->position;
    out[n++] = SYSEX_END;
    return (n == SYSEX_UID_REPORT_LEN) ? n : 0u;
}

size_t sysex_encode_status(uint8_t *out, size_t capacity, uint8_t code, uint8_t detail) {
    if (out == NULL || capacity < SYSEX_STATUS_LEN || code > 127u || detail > 127u) {
        return 0u;
    }

    size_t n = write_header(out, SYSEX_TYPE_STATUS);
    out[n++] = code;
    out[n++] = detail;
    out[n++] = SYSEX_END;
    return (n == SYSEX_STATUS_LEN) ? n : 0u;
}
