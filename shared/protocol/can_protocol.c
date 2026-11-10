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

bool can_listen_mode_is_valid(can_listen_mode_t mode) {
    // Énumération non signée : ne comparer que la borne haute évite -Wtype-limits.
    return (unsigned)mode <= (unsigned)CAN_LISTEN_MODE_STREAM_OFF;
}

bool can_listen_state_is_valid(can_listen_state_t state) {
    return (unsigned)state <= (unsigned)CAN_LISTEN_STATE_FAULT;
}

bool can_listen_channel_is_valid(uint8_t channel) {
    return channel < CAN_IPROPI_CHANNEL_COUNT;
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

// --- Transfert du profil d'activation (0x420-0x423, ADR-0013) ----------------

static bool profile_result_is_valid(can_profile_result_t result) {
    // `CAN_PROFILE_RESULT_OK` vaut 0 : la borne basse est implicite (énumération
    // non signée), ne comparer que la borne haute évite -Wtype-limits.
    return result <= CAN_PROFILE_RESULT_UNSUPPORTED_VERSION;
}

static bool profile_source_is_valid(can_profile_source_t source) {
    return source == CAN_PROFILE_SOURCE_DEFAULTS || source == CAN_PROFILE_SOURCE_NVM;
}

can_proto_result_t can_encode_profile_begin(uint8_t out[CAN_DLC_PROFILE_BEGIN],
                                            const can_profile_begin_t *begin) {
    if (out == NULL || begin == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (begin->format_version == 0u ||
        begin->payload_len == 0u || begin->payload_len > CAN_PROFILE_PAYLOAD_MAX ||
        begin->chunk_size != CAN_PROFILE_CHUNK_SIZE ||
        (begin->flags & (uint8_t)~CAN_PROFILE_FLAG_GLOBAL) != 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    out[0] = begin->proto_version;
    out[1] = begin->format_version;
    out[2] = begin->transfer_id;
    out[3] = begin->flags;
    can_put_u16_le(&out[4], begin->payload_len);
    out[6] = begin->chunk_size;
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_profile_begin(const uint8_t *data, uint8_t dlc,
                                            can_profile_begin_t *begin) {
    if (data == NULL || begin == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_PROFILE_BEGIN) {
        return CAN_PROTO_BAD_DLC;
    }
    begin->proto_version = data[0];
    begin->format_version = data[1];
    begin->transfer_id = data[2];
    begin->flags = data[3];
    begin->payload_len = can_get_u16_le(&data[4]);
    begin->chunk_size = data[6];
    begin->reserved = data[7];
    if (begin->format_version == 0u ||
        begin->payload_len == 0u || begin->payload_len > CAN_PROFILE_PAYLOAD_MAX ||
        begin->chunk_size != CAN_PROFILE_CHUNK_SIZE ||
        (begin->flags & (uint8_t)~CAN_PROFILE_FLAG_GLOBAL) != 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_profile_chunk(uint8_t out[CAN_DLC_PROFILE_CHUNK],
                                            const can_profile_chunk_t *chunk) {
    if (out == NULL || chunk == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    out[0] = chunk->transfer_id;
    out[1] = chunk->seq;
    for (uint8_t i = 0u; i < CAN_PROFILE_CHUNK_SIZE; ++i) {
        out[2u + i] = chunk->data[i];
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_profile_chunk(const uint8_t *data, uint8_t dlc,
                                            can_profile_chunk_t *chunk) {
    if (data == NULL || chunk == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_PROFILE_CHUNK) {
        return CAN_PROTO_BAD_DLC;
    }
    chunk->transfer_id = data[0];
    chunk->seq = data[1];
    for (uint8_t i = 0u; i < CAN_PROFILE_CHUNK_SIZE; ++i) {
        chunk->data[i] = data[2u + i];
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_profile_commit(uint8_t out[CAN_DLC_PROFILE_COMMIT],
                                             const can_profile_commit_t *commit) {
    if (out == NULL || commit == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (commit->format_version == 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    out[0] = commit->proto_version;
    out[1] = commit->format_version;
    out[2] = commit->transfer_id;
    out[3] = 0u;
    out[4] = (uint8_t)(commit->crc32 & 0xFFu);
    out[5] = (uint8_t)((commit->crc32 >> 8u) & 0xFFu);
    out[6] = (uint8_t)((commit->crc32 >> 16u) & 0xFFu);
    out[7] = (uint8_t)((commit->crc32 >> 24u) & 0xFFu);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_profile_commit(const uint8_t *data, uint8_t dlc,
                                             can_profile_commit_t *commit) {
    if (data == NULL || commit == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_PROFILE_COMMIT) {
        return CAN_PROTO_BAD_DLC;
    }
    commit->proto_version = data[0];
    commit->format_version = data[1];
    commit->transfer_id = data[2];
    commit->reserved = data[3];
    commit->crc32 = (uint32_t)data[4] | ((uint32_t)data[5] << 8u) |
                    ((uint32_t)data[6] << 16u) | ((uint32_t)data[7] << 24u);
    if (commit->format_version == 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_profile_status(uint8_t out[CAN_DLC_PROFILE_STATUS],
                                             const can_profile_status_t *status) {
    if (out == NULL || status == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!profile_result_is_valid(status->result) || !profile_source_is_valid(status->source)) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], status->uid_tag);
    out[2] = status->transfer_id;
    out[3] = (uint8_t)(((uint8_t)status->result & 0x07u) |
                       (((uint8_t)status->source & 0x03u) << 3u));
    out[4] = (uint8_t)(status->profile_tag32 & 0xFFu);
    out[5] = (uint8_t)((status->profile_tag32 >> 8u) & 0xFFu);
    out[6] = (uint8_t)((status->profile_tag32 >> 16u) & 0xFFu);
    out[7] = (uint8_t)((status->profile_tag32 >> 24u) & 0xFFu);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_profile_status(const uint8_t *data, uint8_t dlc,
                                             can_profile_status_t *status) {
    if (data == NULL || status == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_PROFILE_STATUS) {
        return CAN_PROTO_BAD_DLC;
    }
    status->uid_tag = can_get_u16_le(&data[0]);
    status->transfer_id = data[2];
    status->result = (can_profile_result_t)(data[3] & 0x07u);
    status->source = (can_profile_source_t)((data[3] >> 3u) & 0x03u);
    status->profile_tag32 = (uint32_t)data[4] | ((uint32_t)data[5] << 8u) |
                            ((uint32_t)data[6] << 16u) | ((uint32_t)data[7] << 24u);
    if (!profile_result_is_valid(status->result) || !profile_source_is_valid(status->source)) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

// --- CARD_EVENT (ID 0x424, DLC 8) — ADR-0018 -------------------------------

static bool card_event_is_valid(can_card_event_t event) {
    return (unsigned)event <= (unsigned)CAN_CARD_EVENT_SELFTEST_ABORTED;
}

static bool card_event_channel_is_valid(uint8_t channel) {
    return channel < CAN_IPROPI_CHANNEL_COUNT || channel == CAN_CARD_EVENT_CHANNEL_NONE;
}

can_proto_result_t can_encode_card_event(uint8_t out[CAN_DLC_CARD_EVENT],
                                         const can_card_event_msg_t *event) {
    if (out == NULL || event == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!card_event_is_valid(event->event) || !card_event_channel_is_valid(event->channel) ||
        event->reserved != 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    out[0] = event->proto_version;
    out[1] = (uint8_t)event->event;
    out[2] = event->channel;
    out[3] = event->state;
    out[4] = event->fault;
    can_put_u16_le(&out[5], event->uid_tag);
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_card_event(const uint8_t *data, uint8_t dlc,
                                         can_card_event_msg_t *event) {
    if (data == NULL || event == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_CARD_EVENT) {
        return CAN_PROTO_BAD_DLC;
    }
    event->proto_version = data[0];
    event->event = (can_card_event_t)data[1];
    event->channel = data[2];
    event->state = data[3];
    event->fault = data[4];
    event->uid_tag = can_get_u16_le(&data[5]);
    event->reserved = data[7];
    if (!card_event_is_valid(event->event) || !card_event_channel_is_valid(event->channel) ||
        event->reserved != 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

// --- CARD_STATS_REQUEST (0x403) / CARD_STATS (0x425) — ADR-0020 ------------

static bool card_counter_is_valid(can_card_counter_t counter) {
    return (unsigned)counter < (unsigned)CAN_CARD_COUNTER_COUNT;
}

can_proto_result_t can_encode_card_stats_request(uint8_t out[CAN_DLC_CARD_STATS_REQUEST],
                                                 const can_card_stats_request_t *request) {
    if (out == NULL || request == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!card_counter_is_valid(request->counter_id) || request->reply_to == 0u ||
        request->reply_to > CAN_REQUEST_ID_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }
    out[0] = request->proto_version;
    out[1] = (uint8_t)request->counter_id;
    can_put_u16_le(&out[2], request->reply_to);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_card_stats_request(const uint8_t *data, uint8_t dlc,
                                                 can_card_stats_request_t *request) {
    if (data == NULL || request == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_CARD_STATS_REQUEST) {
        return CAN_PROTO_BAD_DLC;
    }
    request->proto_version = data[0];
    request->counter_id = (can_card_counter_t)data[1];
    request->reply_to = can_get_u16_le(&data[2]);
    if (!card_counter_is_valid(request->counter_id) || request->reply_to == 0u ||
        request->reply_to > CAN_REQUEST_ID_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_card_stats(uint8_t out[CAN_DLC_CARD_STATS],
                                         const can_card_stats_t *stats) {
    if (out == NULL || stats == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!card_counter_is_valid(stats->counter_id)) {
        return CAN_PROTO_BAD_VALUE;
    }
    out[0] = stats->proto_version;
    out[1] = (uint8_t)stats->counter_id;
    can_put_u16_le(&out[2], stats->uid_tag);
    out[4] = (uint8_t)(stats->value & 0xFFu);
    out[5] = (uint8_t)((stats->value >> 8u) & 0xFFu);
    out[6] = (uint8_t)((stats->value >> 16u) & 0xFFu);
    out[7] = (uint8_t)((stats->value >> 24u) & 0xFFu);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_card_stats(const uint8_t *data, uint8_t dlc,
                                         can_card_stats_t *stats) {
    if (data == NULL || stats == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_CARD_STATS) {
        return CAN_PROTO_BAD_DLC;
    }
    stats->proto_version = data[0];
    stats->counter_id = (can_card_counter_t)data[1];
    stats->uid_tag = can_get_u16_le(&data[2]);
    stats->value = (uint32_t)data[4] | ((uint32_t)data[5] << 8u) |
                   ((uint32_t)data[6] << 16u) | ((uint32_t)data[7] << 24u);
    if (!card_counter_is_valid(stats->counter_id)) {
        return CAN_PROTO_BAD_VALUE;
    }
    return CAN_PROTO_OK;
}


// --- Écoute IPROPI et mode REC (0x110, 0x404-0x406, 0x426-0x429) — ADR-0032 --

static bool listen_target_is_required(can_listen_mode_t mode) {
    return mode == CAN_LISTEN_MODE_TRACE || mode == CAN_LISTEN_MODE_STREAM;
}

can_proto_result_t can_encode_listen_pulse(uint8_t out[CAN_DLC_LISTEN_PULSE],
                                           const can_listen_pulse_t *pulse) {
    if (out == NULL || pulse == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!can_listen_channel_is_valid(pulse->channel) || pulse->base > CAN_ADC_12BIT_MAX ||
        pulse->peak > CAN_ADC_12BIT_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], pulse->uid_tag);
    out[2] = (uint8_t)((pulse->channel & CAN_LISTEN_CHANNEL_MASK) |
                       (pulse->split ? CAN_LISTEN_PULSE_FLAG_SPLIT : 0u) |
                       (pulse->saturated ? CAN_LISTEN_PULSE_FLAG_SATURATED : 0u));
    out[3] = pulse->seq;
    out[4] = (uint8_t)(pulse->base & 0xFFu);
    out[5] = (uint8_t)(((pulse->base >> 8u) & 0x0Fu) | ((pulse->peak & 0x0Fu) << 4u));
    out[6] = (uint8_t)((pulse->peak >> 4u) & 0xFFu);
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_pulse(const uint8_t *data, uint8_t dlc,
                                           can_listen_pulse_t *pulse) {
    uint16_t base;
    uint16_t peak;
    uint8_t channel;

    if (data == NULL || pulse == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_PULSE) {
        return CAN_PROTO_BAD_DLC;
    }
    channel = (uint8_t)(data[2] & CAN_LISTEN_CHANNEL_MASK);
    base = (uint16_t)((uint16_t)data[4] | ((uint16_t)(data[5] & 0x0Fu) << 8u));
    peak = (uint16_t)(((uint16_t)(data[5] >> 4u) & 0x0Fu) | ((uint16_t)data[6] << 4u));
    if (!can_listen_channel_is_valid(channel)) {
        return CAN_PROTO_BAD_VALUE;
    }
    pulse->uid_tag = can_get_u16_le(&data[0]);
    pulse->channel = channel;
    pulse->seq = data[3];
    pulse->base = base;
    pulse->peak = peak;
    pulse->split = (data[2] & CAN_LISTEN_PULSE_FLAG_SPLIT) != 0u;
    pulse->saturated = (data[2] & CAN_LISTEN_PULSE_FLAG_SATURATED) != 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_listen_control(uint8_t out[CAN_DLC_LISTEN_CONTROL],
                                             const can_listen_control_t *control) {
    if (out == NULL || control == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!can_listen_mode_is_valid(control->mode) ||
        !can_listen_channel_is_valid(control->channel) ||
        (listen_target_is_required(control->mode) && control->target_uid_tag == 0u)) {
        return CAN_PROTO_BAD_VALUE;
    }
    out[0] = control->proto_version;
    out[1] = (uint8_t)control->mode;
    can_put_u16_le(&out[2], control->param_crc16);
    can_put_u16_le(&out[4], control->target_uid_tag);
    out[6] = control->channel;
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_control(const uint8_t *data, uint8_t dlc,
                                             can_listen_control_t *control) {
    can_listen_mode_t mode;

    if (data == NULL || control == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_CONTROL) {
        return CAN_PROTO_BAD_DLC;
    }
    mode = (can_listen_mode_t)data[1];
    if (!can_listen_mode_is_valid(mode) || !can_listen_channel_is_valid(data[6]) ||
        (listen_target_is_required(mode) && can_get_u16_le(&data[4]) == 0u)) {
        return CAN_PROTO_BAD_VALUE;
    }
    control->proto_version = data[0];
    control->mode = mode;
    control->param_crc16 = can_get_u16_le(&data[2]);
    control->target_uid_tag = can_get_u16_le(&data[4]);
    control->channel = data[6];
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_listen_param(uint8_t out[CAN_DLC_LISTEN_PARAM],
                                           const can_listen_param_t *param) {
    if (out == NULL || param == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    // `param_id` reste opaque : le Moteur doit pouvoir nommer un id refusé.
    out[0] = param->proto_version;
    out[1] = param->param_id;
    out[2] = (uint8_t)(param->value & 0xFFu);
    out[3] = (uint8_t)((param->value >> 8u) & 0xFFu);
    out[4] = (uint8_t)((param->value >> 16u) & 0xFFu);
    out[5] = (uint8_t)((param->value >> 24u) & 0xFFu);
    out[6] = 0u;
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_param(const uint8_t *data, uint8_t dlc,
                                           can_listen_param_t *param) {
    if (data == NULL || param == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_PARAM) {
        return CAN_PROTO_BAD_DLC;
    }
    param->proto_version = data[0];
    param->param_id = data[1];
    param->value = (uint32_t)data[2] | ((uint32_t)data[3] << 8u) |
                   ((uint32_t)data[4] << 16u) | ((uint32_t)data[5] << 24u);
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_listen_noise_query(uint8_t out[CAN_DLC_LISTEN_NOISE_QUERY],
                                                 const can_listen_noise_query_t *query) {
    if (out == NULL || query == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!can_listen_channel_is_valid(query->channel) || query->lookback_ms == 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], query->target_uid_tag);
    out[2] = query->channel;
    out[3] = query->request_id;
    can_put_u16_le(&out[4], query->lookback_ms);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_noise_query(const uint8_t *data, uint8_t dlc,
                                                 can_listen_noise_query_t *query) {
    uint16_t lookback_ms;

    if (data == NULL || query == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_NOISE_QUERY) {
        return CAN_PROTO_BAD_DLC;
    }
    lookback_ms = can_get_u16_le(&data[4]);
    if (!can_listen_channel_is_valid(data[2]) || lookback_ms == 0u) {
        return CAN_PROTO_BAD_VALUE;
    }
    query->target_uid_tag = can_get_u16_le(&data[0]);
    query->channel = data[2];
    query->request_id = data[3];
    query->lookback_ms = lookback_ms;
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_listen_status(uint8_t out[CAN_DLC_LISTEN_STATUS],
                                            const can_listen_status_t *status) {
    if (out == NULL || status == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!can_listen_state_is_valid(status->state)) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], status->uid_tag);
    out[2] = (uint8_t)status->state;
    out[3] = status->refused_param_id;
    can_put_u16_le(&out[4], status->sweep_period_us);
    out[6] = status->overflows;
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_status(const uint8_t *data, uint8_t dlc,
                                            can_listen_status_t *status) {
    if (data == NULL || status == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_STATUS) {
        return CAN_PROTO_BAD_DLC;
    }
    if (!can_listen_state_is_valid((can_listen_state_t)data[2])) {
        return CAN_PROTO_BAD_VALUE;
    }
    status->uid_tag = can_get_u16_le(&data[0]);
    status->state = (can_listen_state_t)data[2];
    status->refused_param_id = data[3];
    status->sweep_period_us = can_get_u16_le(&data[4]);
    status->overflows = data[6];
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_listen_noise_report(uint8_t out[CAN_DLC_LISTEN_NOISE_REPORT],
                                                  const can_listen_noise_report_t *report) {
    if (out == NULL || report == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (!can_listen_channel_is_valid(report->channel) || report->min > CAN_ADC_12BIT_MAX ||
        report->max > CAN_ADC_12BIT_MAX || report->min > report->max) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], report->uid_tag);
    out[2] = report->channel;
    out[3] = report->request_id;
    out[4] = (uint8_t)(report->min & 0xFFu);
    out[5] = (uint8_t)(((report->min >> 8u) & 0x0Fu) | ((report->max & 0x0Fu) << 4u));
    out[6] = (uint8_t)((report->max >> 4u) & 0xFFu);
    out[7] = (uint8_t)(report->truncated ? CAN_LISTEN_NOISE_FLAG_TRUNCATED : 0u);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_noise_report(const uint8_t *data, uint8_t dlc,
                                                  can_listen_noise_report_t *report) {
    uint16_t min;
    uint16_t max;

    if (data == NULL || report == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_NOISE_REPORT) {
        return CAN_PROTO_BAD_DLC;
    }
    min = (uint16_t)((uint16_t)data[4] | ((uint16_t)(data[5] & 0x0Fu) << 8u));
    max = (uint16_t)(((uint16_t)(data[5] >> 4u) & 0x0Fu) | ((uint16_t)data[6] << 4u));
    if (!can_listen_channel_is_valid(data[2]) || min > max) {
        return CAN_PROTO_BAD_VALUE;
    }
    report->uid_tag = can_get_u16_le(&data[0]);
    report->channel = data[2];
    report->request_id = data[3];
    report->min = min;
    report->max = max;
    report->truncated = (data[7] & CAN_LISTEN_NOISE_FLAG_TRUNCATED) != 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_listen_pulse_timing(uint8_t out[CAN_DLC_LISTEN_PULSE_TIMING],
                                                  const can_listen_pulse_timing_t *timing) {
    if (out == NULL || timing == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    can_put_u16_le(&out[0], timing->uid_tag);
    out[2] = timing->seq;
    can_put_u16_le(&out[3], timing->t_peak_100us);
    can_put_u16_le(&out[5], timing->t_end_peak_100us);
    out[7] = 0u;
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_pulse_timing(const uint8_t *data, uint8_t dlc,
                                                  can_listen_pulse_timing_t *timing) {
    if (data == NULL || timing == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_PULSE_TIMING) {
        return CAN_PROTO_BAD_DLC;
    }
    timing->uid_tag = can_get_u16_le(&data[0]);
    timing->seq = data[2];
    timing->t_peak_100us = can_get_u16_le(&data[3]);
    timing->t_end_peak_100us = can_get_u16_le(&data[5]);
    return CAN_PROTO_OK;
}

can_proto_result_t can_encode_listen_trace(uint8_t out[CAN_DLC_LISTEN_TRACE],
                                           const can_listen_trace_t *trace) {
    uint16_t s0;
    uint16_t s1;
    uint16_t s2;
    uint16_t s3;

    if (out == NULL || trace == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    s0 = trace->samples[0];
    s1 = trace->samples[1];
    s2 = trace->samples[2];
    s3 = trace->samples[3];
    if (s0 > CAN_ADC_12BIT_MAX || s1 > CAN_ADC_12BIT_MAX || s2 > CAN_ADC_12BIT_MAX ||
        s3 > CAN_ADC_12BIT_MAX) {
        return CAN_PROTO_BAD_VALUE;
    }
    can_put_u16_le(&out[0], trace->seq);
    out[2] = (uint8_t)(s0 & 0xFFu);
    out[3] = (uint8_t)(((s0 >> 8u) & 0x0Fu) | ((s1 & 0x0Fu) << 4u));
    out[4] = (uint8_t)((s1 >> 4u) & 0xFFu);
    out[5] = (uint8_t)(s2 & 0xFFu);
    out[6] = (uint8_t)(((s2 >> 8u) & 0x0Fu) | ((s3 & 0x0Fu) << 4u));
    out[7] = (uint8_t)((s3 >> 4u) & 0xFFu);
    return CAN_PROTO_OK;
}

can_proto_result_t can_decode_listen_trace(const uint8_t *data, uint8_t dlc,
                                           can_listen_trace_t *trace) {
    if (data == NULL || trace == NULL) {
        return CAN_PROTO_BAD_ARG;
    }
    if (dlc != CAN_DLC_LISTEN_TRACE) {
        return CAN_PROTO_BAD_DLC;
    }
    trace->seq = can_get_u16_le(&data[0]);
    trace->samples[0] =
        (uint16_t)((uint16_t)data[2] | ((uint16_t)(data[3] & 0x0Fu) << 8u));
    trace->samples[1] =
        (uint16_t)(((uint16_t)(data[3] >> 4u) & 0x0Fu) | ((uint16_t)data[4] << 4u));
    trace->samples[2] =
        (uint16_t)((uint16_t)data[5] | ((uint16_t)(data[6] & 0x0Fu) << 8u));
    trace->samples[3] =
        (uint16_t)(((uint16_t)(data[6] >> 4u) & 0x0Fu) | ((uint16_t)data[7] << 4u));
    return CAN_PROTO_OK;
}
