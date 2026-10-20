// Protocole CAN Clavicore — SOURCE UNIQUE (ADR-0003, ADR-0004).
//
// C pur, sans dépendance au SDK ni au matériel : l'encodage et le décodage sont
// testables sur PC (tests/host/). La conversion vers `struct can2040_msg` reste
// dans can_link.c de chaque carte.
//
// Conventions (ADR-0003 §1) :
//  - identifiants 11 bits standard uniquement (trames étendues / RTR : rejetées
//    et comptées par can_link.c) ;
//  - entiers multi-octets en little-endian ;
//  - DLC strict : tout DLC inattendu est rejeté (CAN_PROTO_BAD_DLC), jamais
//    interprété ;
//  - octets réservés : émis à 0, ignorés en réception.
#ifndef CLAVICORE_CAN_PROTOCOL_H
#define CLAVICORE_CAN_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Constantes globales ---------------------------------------------------

#define CAN_BITRATE_HZ 500000u
#define CAN_PROTO_VERSION 1u

// --- Plan d'identifiants (priorité CAN : le plus petit gagne) --------------

#define CAN_ID_EMERGENCY_STOP 0x001u
#define CAN_ID_FAULT_REPORT 0x010u
#define CAN_ID_SOLENOID_COMMAND 0x100u
#define CAN_ID_BOARD_ASSIGN 0x200u
#define CAN_ID_UID_REQUEST 0x400u
#define CAN_ID_IPROPI_SELFTEST_REQUEST 0x401u
#define CAN_ID_IPROPI_SELFTEST_TRIGGER 0x402u
#define CAN_ID_BOARD_ANNOUNCE_UID 0x410u
#define CAN_ID_BOARD_ANNOUNCE_INFO 0x411u
#define CAN_ID_BOARD_ANNOUNCE_STATUS 0x412u
#define CAN_ID_IPROPI_RAW 0x600u
#define CAN_ID_CONFIG_READ 0x500u
#define CAN_ID_CONFIG_WRITE 0x501u

#define CAN_ID_FORBIDDEN_FIRST 0x7F0u
#define CAN_ID_MAX_STANDARD 0x7FFu

// --- DLC stricts -----------------------------------------------------------

#define CAN_DLC_UID_REQUEST 2u
#define CAN_DLC_SOLENOID_COMMAND 5u
#define CAN_DLC_IPROPI_SELFTEST_REQUEST 2u
#define CAN_DLC_IPROPI_SELFTEST_TRIGGER 2u
#define CAN_DLC_ANNOUNCE_UID 8u
#define CAN_DLC_ANNOUNCE_INFO 8u
#define CAN_DLC_ANNOUNCE_STATUS 8u
#define CAN_DLC_IPROPI_RAW 8u

// --- Requêtes, délais, jitter (ADR-0003 §4) --------------------------------

#define CAN_UID_SIZE 8u

#define CAN_REQUEST_ID_MIN 1u
#define CAN_REQUEST_ID_MAX 127u
#define CAN_REPLY_TO_BOOT 0u
#define CAN_REQUEST_ID_VALUE_MASK 0x7Fu
#define CAN_REQUEST_ID_FLAG_CALIBRATION 0x80u

#define CAN_UID_REQUEST_TIMEOUT_MS 500u
#define CAN_ANNOUNCE_PAIR_TIMEOUT_MS 20u
#define CAN_ANNOUNCE_JITTER_SLOTS 128u
#define CAN_ANNOUNCE_SLOT_US 1000u

#define CAN_FIELD_UNKNOWN 0x7Fu

// --- Auto-test IPROPI ------------------------------------------------------

#define CAN_OCCUPATION_BITS_MASK 0x0FFFu
#define CAN_FAULT_BITS_MASK 0x0FFFu
#define CAN_IPROPI_CHANNEL_COUNT 12u
#define CAN_ADC_12BIT_MAX 4095u

typedef enum {
    CAN_SELFTEST_STATUS_OK = 0,
    CAN_SELFTEST_STATUS_REFUSED_BUSY = 1,
    CAN_SELFTEST_STATUS_ABORTED = 2,
    CAN_SELFTEST_STATUS_UNCALIBRATED = 3,
} can_selftest_status_t;

typedef enum {
    CAN_SOLENOID_COMMAND_OFF = 0,
    CAN_SOLENOID_COMMAND_ON = 1,
    CAN_SOLENOID_COMMAND_PUSH_ON = 2,
} can_solenoid_command_kind_t;

// --- Types -----------------------------------------------------------------

typedef enum {
    CAN_PROTO_OK = 0,
    CAN_PROTO_BAD_ARG,
    CAN_PROTO_BAD_DLC,
    CAN_PROTO_BAD_VALUE,
} can_proto_result_t;

typedef struct {
    uint8_t proto_version;
    uint8_t request_id;
} can_uid_request_t;

typedef struct {
    uint8_t proto_version;
    uint8_t request_id;
    bool calibration_mode;
} can_ipropi_selftest_request_t;

typedef struct {
    uint8_t proto_version;
} can_ipropi_selftest_trigger_t;

typedef struct {
    uint16_t uid_tag;
    uint8_t proto_version;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;
    uint8_t reply_to;
} can_announce_info_t;

typedef struct {
    uint16_t uid_tag;
    uint16_t occupation_bits;
    uint8_t reply_to;
    can_selftest_status_t test_status;
    uint16_t fault_bits;
} can_announce_status_t;

typedef struct {
    uint16_t uid_tag;
    uint8_t reply_to;
    uint8_t channel;
    uint16_t idle_avg;
    uint16_t probe_avg;
} can_ipropi_raw_t;

typedef struct {
    uint16_t uid_tag;
    uint8_t channel;
    can_solenoid_command_kind_t command;
    uint8_t velocity;
} can_solenoid_command_t;

// --- Helpers purs ----------------------------------------------------------

void can_put_u16_le(uint8_t *out, uint16_t value);
uint16_t can_get_u16_le(const uint8_t *in);

uint16_t can_crc16_ccitt(const uint8_t *data, size_t length);
uint16_t can_uid_tag(const uint8_t uid[CAN_UID_SIZE]);
uint32_t can_announce_delay_us(uint16_t uid_tag);
bool can_id_is_forbidden(uint32_t id);
uint8_t can_next_request_id(uint8_t previous);
bool can_selftest_bits_are_valid(uint16_t bits);
bool can_selftest_status_is_valid(can_selftest_status_t status);
bool can_solenoid_command_is_valid(can_solenoid_command_kind_t command);

// SOLENOID_COMMAND (ID 0x100, DLC 5).
can_proto_result_t can_encode_solenoid_command(uint8_t out[CAN_DLC_SOLENOID_COMMAND],
                                               const can_solenoid_command_t *command);
can_proto_result_t can_decode_solenoid_command(const uint8_t *data, uint8_t dlc,
                                               can_solenoid_command_t *command);

// UID_REQUEST (ID 0x400, DLC 2).
can_proto_result_t can_encode_uid_request(uint8_t out[CAN_DLC_UID_REQUEST],
                                          const can_uid_request_t *request);
can_proto_result_t can_decode_uid_request(const uint8_t *data, uint8_t dlc,
                                          can_uid_request_t *request);

// IPROPI_SELFTEST_REQUEST (ID 0x401, DLC 2).
can_proto_result_t can_encode_ipropi_selftest_request(
    uint8_t out[CAN_DLC_IPROPI_SELFTEST_REQUEST],
    const can_ipropi_selftest_request_t *request);
can_proto_result_t can_decode_ipropi_selftest_request(
    const uint8_t *data, uint8_t dlc, can_ipropi_selftest_request_t *request);

// IPROPI_SELFTEST_TRIGGER (ID 0x402, DLC 2).
can_proto_result_t can_encode_ipropi_selftest_trigger(
    uint8_t out[CAN_DLC_IPROPI_SELFTEST_TRIGGER],
    const can_ipropi_selftest_trigger_t *trigger);
can_proto_result_t can_decode_ipropi_selftest_trigger(
    const uint8_t *data, uint8_t dlc, can_ipropi_selftest_trigger_t *trigger);

// BOARD_ANNOUNCE trame A (ID 0x410, DLC 8) : l'UID brut.
can_proto_result_t can_encode_announce_uid(uint8_t out[CAN_DLC_ANNOUNCE_UID],
                                           const uint8_t uid[CAN_UID_SIZE]);
can_proto_result_t can_decode_announce_uid(const uint8_t *data, uint8_t dlc,
                                           uint8_t uid[CAN_UID_SIZE]);

// BOARD_ANNOUNCE trame B (ID 0x411, DLC 8).
can_proto_result_t can_encode_announce_info(uint8_t out[CAN_DLC_ANNOUNCE_INFO],
                                            const can_announce_info_t *info);
can_proto_result_t can_decode_announce_info(const uint8_t *data, uint8_t dlc,
                                            can_announce_info_t *info);

// BOARD_ANNOUNCE trame C (ID 0x412, DLC 8).
can_proto_result_t can_encode_announce_status(uint8_t out[CAN_DLC_ANNOUNCE_STATUS],
                                              const can_announce_status_t *status);
can_proto_result_t can_decode_announce_status(const uint8_t *data, uint8_t dlc,
                                              can_announce_status_t *status);

// IPROPI_RAW (ID 0x600, DLC 8).
can_proto_result_t can_encode_ipropi_raw(uint8_t out[CAN_DLC_IPROPI_RAW],
                                         const can_ipropi_raw_t *raw);
can_proto_result_t can_decode_ipropi_raw(const uint8_t *data, uint8_t dlc,
                                         can_ipropi_raw_t *raw);

#endif  // CLAVICORE_CAN_PROTOCOL_H
