// Implémentation des helpers purs du protocole CAN (voir can_protocol.h).
#include "can_protocol.h"

static bool value_is_7bit(uint8_t value) {
    return value <= 127u;
}

static bool request_id_is_valid(uint8_t request_id) {
    return request_id >= CAN_REQUEST_ID_MIN && request_id <= CAN_REQUEST_ID_MAX;
}

void can_put_u16_le(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8u);
}

uint16_t can_get_u16_le(const uint8_t *in) {
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8u));
}

uint16_t can_crc16_ccitt(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;

    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8u);
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

uint16_t can_uid_tag(const uint8_t uid[CAN_UID_SIZE]) {
    return can_crc16_ccitt(uid, CAN_UID_SIZE);
}

uint32_t can_announce_delay_us(uint16_t uid_tag) {
    return (uint32_t)(uid_tag % CAN_ANNOUNCE_JITTER_SLOTS) * CAN_ANNOUNCE_SLOT_US;
}

bool can_id_is_forbidden(uint32_t id) {
    return id >= CAN_ID_FORBIDDEN_FIRST;
}

uint8_t can_next_request_id(uint8_t previous) {
    if (previous < CAN_REQUEST_ID_MIN || previous >= CAN_REQUEST_ID_MAX) {
        return CAN_REQUEST_ID_MIN;
    }
    return (uint8_t)(previous + 1u);
}

bool can_selftest_bits_are_valid(uint16_t bits) {
    return (bits & ~CAN_OCCUPATION_BITS_MASK) == 0u;
}

bool can_selftest_status_is_valid(can_selftest_status_t status) {
    return status == CAN_SELFTEST_STATUS_OK || status == CAN_SELFTEST_STATUS_REFUSED_BUSY ||
           status == CAN_SELFTEST_STATUS_ABORTED ||
           status == CAN_SELFTEST_STATUS_UNCALIBRATED;
}

bool can_solenoid_command_is_valid(can_solenoid_command_kind_t command) {
    return command == CAN_SOLENOID_COMMAND_OFF || command == CAN_SOLENOID_COMMAND_ON ||
           command == CAN_SOLENOID_COMMAND_PUSH_ON;
}

can_proto_result_t can_encode_solenoid_command(uint8_t out[CAN_DLC_SOLENOID_COMMAND],
                                               const can_solenoid_command_t *command) {
    if (out == NULL || command == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (command->channel >= CAN_IPROPI_CHANNEL_COUNT ||
        !can_solenoid_command_is_valid(command->command) || !value_is_7bit(command->velocity)) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], command->uid_tag);
    out[2] = command->channel;
    out[3] = (uint8_t)command->command;
    out[4] = command->velocity;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_solenoid_command(const uint8_t *data, uint8_t dlc,
                                               can_solenoid_command_t *command) {
    if (data == NULL || command == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_SOLENOID_COMMAND) {
        return CAN_PROTO_BAD_DLC;
    }
    command->uid_tag = can_get_u16_le(&data[0]);
    command->channel = data[2];
    command->command = (can_solenoid_command_kind_t)data[3];
    command->velocity = data[4];
    if (command->channel >= CAN_IPROPI_CHANNEL_COUNT ||
        !can_solenoid_command_is_valid(command->command) || !value_is_7bit(command->velocity)) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

static can_proto_result_t decode_request_common(const uint8_t *data, uint8_t dlc,
                                                uint8_t expected_dlc, uint8_t *proto_version,
                                                uint8_t *request_id) {
    if (data == NULL || proto_version == NULL || request_id == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != expected_dlc) {
        return CAN_PROTO_BAD_DLC;
    }
    if (!request_id_is_valid(data[1])) {
        return CAN_PROTO_BAD_VALUE;
    }
    *proto_version = data[0];
    *request_id = data[1];
    return CAN_PROTO_OK;
}

static can_proto_result_t encode_request_common(uint8_t *out, uint8_t expected_dlc,
                                                uint8_t proto_version, uint8_t request_id) {
    (void)expected_dlc;
    if (out == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!request_id_is_valid(request_id)) {
        return CAN_PROTO_BAD_VALUE;
    }
    out[0] = proto_version;
    out[1] = request_id;
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_uid_request(uint8_t out[CAN_DLC_UID_REQUEST],
                                          const can_uid_request_t *request) {
    if (request == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    return encode_request_common(out, CAN_DLC_UID_REQUEST, request->proto_version,
                                 request->request_id);
}

can_proto_result_t can_decode_uid_request(const uint8_t *data, uint8_t dlc,
                                          can_uid_request_t *request) {
    if (request == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    return decode_request_common(data, dlc, CAN_DLC_UID_REQUEST, &request->proto_version,
                                 &request->request_id);
}

can_proto_result_t can_encode_ipropi_selftest_request(
    uint8_t out[CAN_DLC_IPROPI_SELFTEST_REQUEST], const can_ipropi_selftest_request_t *request) {
    if (out == NULL || request == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!request_id_is_valid(request->request_id)) {
        return CAN_PROTO_BAD_VALUE;
    }

    out[0] = request->proto_version;
    out[1] = (uint8_t)(request->request_id |
                       (request->calibration_mode ? CAN_REQUEST_ID_FLAG_CALIBRATION : 0u));
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_ipropi_selftest_request(
    const uint8_t *data, uint8_t dlc, can_ipropi_selftest_request_t *request) {
    uint8_t request_id;

    if (data == NULL || request == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_IPROPI_SELFTEST_REQUEST) {
        return CAN_PROTO_BAD_DLC;
    }

    request_id = (uint8_t)(data[1] & CAN_REQUEST_ID_VALUE_MASK);
    if (!request_id_is_valid(request_id)) {
        return CAN_PROTO_BAD_VALUE;
    }

    request->proto_version = data[0];
    request->request_id = request_id;
    request->calibration_mode = (data[1] & CAN_REQUEST_ID_FLAG_CALIBRATION) != 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_ipropi_selftest_trigger(
    uint8_t out[CAN_DLC_IPROPI_SELFTEST_TRIGGER], const can_ipropi_selftest_trigger_t *trigger) {
    if (out == NULL || trigger == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    out[0] = trigger->proto_version;
    out[1] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_ipropi_selftest_trigger(
    const uint8_t *data, uint8_t dlc, can_ipropi_selftest_trigger_t *trigger) {
    if (data == NULL || trigger == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_IPROPI_SELFTEST_TRIGGER) {
        return CAN_PROTO_BAD_DLC;
    }
    trigger->proto_version = data[0];
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_announce_uid(uint8_t out[CAN_DLC_ANNOUNCE_UID],
                                           const uint8_t uid[CAN_UID_SIZE]) {
    if (out == NULL || uid == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    for (uint8_t i = 0u; i < CAN_UID_SIZE; ++i) {
        out[i] = uid[i];
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_announce_uid(const uint8_t *data, uint8_t dlc,
                                           uint8_t uid[CAN_UID_SIZE]) {
    if (data == NULL || uid == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_ANNOUNCE_UID) {
        return CAN_PROTO_BAD_DLC;
    }
    for (uint8_t i = 0u; i < CAN_UID_SIZE; ++i) {
        uid[i] = data[i];
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_announce_info(uint8_t out[CAN_DLC_ANNOUNCE_INFO],
                                            const can_announce_info_t *info) {
    if (out == NULL || info == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!value_is_7bit(info->fw_major) || !value_is_7bit(info->fw_minor) ||
        !value_is_7bit(info->fw_patch) || info->reply_to > CAN_REQUEST_ID_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], info->uid_tag);
    out[2] = info->proto_version;
    out[3] = info->fw_major;
    out[4] = info->fw_minor;
    out[5] = info->fw_patch;
    out[6] = info->reply_to;
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_announce_info(const uint8_t *data, uint8_t dlc,
                                            can_announce_info_t *info) {
    if (data == NULL || info == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_ANNOUNCE_INFO) {
        return CAN_PROTO_BAD_DLC;
    }
    if (!value_is_7bit(data[3]) || !value_is_7bit(data[4]) || !value_is_7bit(data[5]) ||
        data[6] > CAN_REQUEST_ID_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }
    info->uid_tag = can_get_u16_le(&data[0]);
    info->proto_version = data[2];
    info->fw_major = data[3];
    info->fw_minor = data[4];
    info->fw_patch = data[5];
    info->reply_to = data[6];
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_announce_status(uint8_t out[CAN_DLC_ANNOUNCE_STATUS],
                                              const can_announce_status_t *status) {
    if (out == NULL || status == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!can_selftest_bits_are_valid(status->occupation_bits) ||
        !can_selftest_bits_are_valid(status->fault_bits) ||
        !can_selftest_status_is_valid(status->test_status) ||
        status->reply_to < CAN_REQUEST_ID_MIN || status->reply_to > CAN_REQUEST_ID_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }

    can_put_u16_le(&out[0], status->uid_tag);
    can_put_u16_le(&out[2], status->occupation_bits);
    out[4] = status->reply_to;
    out[5] = (uint8_t)status->test_status;
    can_put_u16_le(&out[6], status->fault_bits);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_announce_status(const uint8_t *data, uint8_t dlc,
                                              can_announce_status_t *status) {
    if (data == NULL || status == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_ANNOUNCE_STATUS) {
        return CAN_PROTO_BAD_DLC;
    }

    status->uid_tag = can_get_u16_le(&data[0]);
    status->occupation_bits = can_get_u16_le(&data[2]);
    status->reply_to = data[4];
    status->test_status = (can_selftest_status_t)data[5];
    status->fault_bits = can_get_u16_le(&data[6]);

    if (!can_selftest_bits_are_valid(status->occupation_bits) ||
        !can_selftest_bits_are_valid(status->fault_bits) ||
        !can_selftest_status_is_valid(status->test_status) || !request_id_is_valid(status->reply_to)) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_ipropi_raw(uint8_t out[CAN_DLC_IPROPI_RAW],
                                         const can_ipropi_raw_t *raw) {
    if (out == NULL || raw == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!request_id_is_valid(raw->reply_to) || raw->channel >= CAN_IPROPI_CHANNEL_COUNT ||
        raw->idle_avg > CAN_ADC_12BIT_MAX || raw->probe_avg > CAN_ADC_12BIT_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }

    can_put_u16_le(&out[0], raw->uid_tag);
    out[2] = raw->reply_to;
    out[3] = raw->channel;
    can_put_u16_le(&out[4], raw->idle_avg);
    can_put_u16_le(&out[6], raw->probe_avg);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_ipropi_raw(const uint8_t *data, uint8_t dlc,
                                         can_ipropi_raw_t *raw) {
    if (data == NULL || raw == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_IPROPI_RAW) {
        return CAN_PROTO_BAD_DLC;
    }

    raw->uid_tag = can_get_u16_le(&data[0]);
    raw->reply_to = data[2];
    raw->channel = data[3];
    raw->idle_avg = can_get_u16_le(&data[4]);
    raw->probe_avg = can_get_u16_le(&data[6]);

    if (!request_id_is_valid(raw->reply_to) || raw->channel >= CAN_IPROPI_CHANNEL_COUNT ||
        raw->idle_avg > CAN_ADC_12BIT_MAX || raw->probe_avg > CAN_ADC_12BIT_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_ipropi_ohm(uint8_t out[CAN_DLC_IPROPI_OHM],
                                         const can_ipropi_ohm_t *ohm) {
    if (out == NULL || ohm == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!request_id_is_valid(ohm->reply_to) || ohm->channel >= CAN_IPROPI_CHANNEL_COUNT) {
        return CAN_PROTO_BAD_VALUE;
    }

    can_put_u16_le(&out[0], ohm->uid_tag);
    out[2] = ohm->reply_to;
    out[3] = ohm->channel;
    can_put_u16_le(&out[4], ohm->ohm_milliohm);
    can_put_u16_le(&out[6], ohm->i100_ma);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_ipropi_ohm(const uint8_t *data, uint8_t dlc,
                                         can_ipropi_ohm_t *ohm) {
    if (data == NULL || ohm == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_IPROPI_OHM) {
        return CAN_PROTO_BAD_DLC;
    }

    ohm->uid_tag = can_get_u16_le(&data[0]);
    ohm->reply_to = data[2];
    ohm->channel = data[3];
    ohm->ohm_milliohm = can_get_u16_le(&data[4]);
    ohm->i100_ma = can_get_u16_le(&data[6]);

    // Les valeurs 16 bits sont toutes acceptées : `CAN_IPROPI_VALUE_INVALID`
    // signale un canal non calculable (pas une trame invalide), ADR-0009 §6.
    if (!request_id_is_valid(ohm->reply_to) || ohm->channel >= CAN_IPROPI_CHANNEL_COUNT) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}
