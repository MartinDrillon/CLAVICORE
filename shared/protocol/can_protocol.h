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
#define CAN_ID_IPROPI_OHM 0x601u
#define CAN_ID_CONFIG_READ 0x500u
#define CAN_ID_CONFIG_WRITE 0x501u
// Transfert du profil d'activation des solénoïdes (ADR-0013, ADR-0014).
#define CAN_ID_PROFILE_BEGIN 0x420u
#define CAN_ID_PROFILE_CHUNK 0x421u
#define CAN_ID_PROFILE_COMMIT 0x422u
#define CAN_ID_PROFILE_STATUS 0x423u

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
#define CAN_DLC_IPROPI_OHM 8u
#define CAN_DLC_PROFILE_BEGIN 8u
#define CAN_DLC_PROFILE_CHUNK 8u
#define CAN_DLC_PROFILE_COMMIT 8u
#define CAN_DLC_PROFILE_STATUS 8u

// --- Événements et compteurs de carte (ADR-0018, ADR-0020) ------------------

// CARD_EVENT (ID 0x424) : compte rendu d'une action locale du Moteur.
#define CAN_ID_CARD_EVENT 0x424u
#define CAN_DLC_CARD_EVENT 8u
// CARD_STATS_REQUEST / CARD_STATS (ADR-0020) : compteurs lus à la demande.
#define CAN_ID_CARD_STATS_REQUEST 0x403u
#define CAN_DLC_CARD_STATS_REQUEST 4u
#define CAN_ID_CARD_STATS 0x425u
#define CAN_DLC_CARD_STATS 8u

// Valeur de `channel` quand l'événement ne concerne aucun canal.
#define CAN_CARD_EVENT_CHANNEL_NONE 0xFFu

typedef enum {
    CAN_CARD_EVENT_BUTTON_SHORT_PRESS = 0,  // appui court : test manuel du canal
    CAN_CARD_EVENT_TRIGGER_PENDING = 1,     // appui long consommé, 0x402 en attente
    CAN_CARD_EVENT_TRIGGER_SENT = 2,        // 0x402 transmis par la carte
    CAN_CARD_EVENT_TRIGGER_BLOCKED = 3,     // un canal empêche l'émission
    CAN_CARD_EVENT_CHANNEL_FAULT = 4,       // défaut verrouillé hors auto-test
    CAN_CARD_EVENT_CHANNEL_FAULT_CLEARED = 5,
    CAN_CARD_EVENT_COMMAND_IGNORED = 6,     // commande reçue et non exécutée
    CAN_CARD_EVENT_SELFTEST_ABORTED = 7,    // mesure d'occupation non concluante
} can_card_event_t;

// Pour COMMAND_IGNORED, l'octet `state` porte la raison (ADR-0020 §3).
typedef enum {
    CAN_CARD_IGNORE_TAG = 1,
    CAN_CARD_IGNORE_CHANNEL = 2,
    CAN_CARD_IGNORE_SELFTEST = 3,
    CAN_CARD_IGNORE_UNOCCUPIED = 4,
    CAN_CARD_IGNORE_DRIVER = 5,
} can_card_ignore_reason_t;

typedef struct {
    uint8_t proto_version;
    can_card_event_t event;
    uint8_t channel;
    uint8_t state;
    uint8_t fault;
    uint16_t uid_tag;
    uint8_t reserved;
} can_card_event_msg_t;

// Compteurs lisibles à la demande (ADR-0020 §2.3).
typedef enum {
    CAN_CARD_COUNTER_CAN_ERRORS = 0,
    CAN_CARD_COUNTER_TX_FAIL = 1,
    CAN_CARD_COUNTER_RX_OVERRUN = 2,
    CAN_CARD_COUNTER_SELFTEST_BLOCKED = 3,
    CAN_CARD_COUNTER_IGNORED_TAG = 4,
    CAN_CARD_COUNTER_BAD_CHANNEL = 5,
    CAN_CARD_COUNTER_PROFILE_STATUS_LOST = 6,
    CAN_CARD_COUNTER_PROFILE_COMMITS_REJECTED = 7,
    CAN_CARD_COUNTER_PROFILE_COMMITS_OK = 8,
    CAN_CARD_COUNTER_RX_IGNORED = 9,
    CAN_CARD_COUNTER_UNOCCUPIED = 10,
    CAN_CARD_COUNTER_REJECTED = 11,
    CAN_CARD_COUNTER_COMMANDS_OK = 12,
    CAN_CARD_COUNTER_COUNT = 13,
} can_card_counter_t;

typedef struct {
    uint8_t proto_version;
    can_card_counter_t counter_id;
    uint16_t reply_to;
} can_card_stats_request_t;

typedef struct {
    uint8_t proto_version;
    can_card_counter_t counter_id;
    uint16_t uid_tag;
    uint32_t value;
} can_card_stats_t;

// --- Transfert de profil (ADR-0013, ADR-0014) -------------------------------

// Octets de payload par PROFILE_CHUNK : [transfer_id][seq][data[6]].
#define CAN_PROFILE_CHUNK_SIZE 6u
// Longueur maximale admise d'un payload de profil (marge pour les versions futures).
#define CAN_PROFILE_PAYLOAD_MAX 128u
// `transfer_id` 0 = annonce de boot (PROFILE_STATUS non sollicitée par un transfert).
#define CAN_PROFILE_TRANSFER_ID_BOOT 0u
#define CAN_PROFILE_TRANSFER_ID_MIN 1u
#define CAN_PROFILE_TRANSFER_ID_MAX 255u
// `flags` de PROFILE_BEGIN : bit 0 = profil global (une valeur pour tous les canaux).
#define CAN_PROFILE_FLAG_GLOBAL 0x01u

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
// Valeur « non calculable » transmise sur 16 bits (ADR-0009 §6) : résistance
// ohmique ou courant estimé indisponible pour ce canal.
#define CAN_IPROPI_VALUE_INVALID 0xFFFFu
// Plafond transmis sur 16 bits : `0xFFFF` reste réservé à « invalide ».
#define CAN_IPROPI_VALUE_MAX 0xFFFEu

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

// Résultat d'un PROFILE_COMMIT, codé sur les bits 0..2 de l'octet 3 de
// PROFILE_STATUS (ADR-0013 §3).
typedef enum {
    CAN_PROFILE_RESULT_OK = 0,
    CAN_PROFILE_RESULT_NO_TRANSFER = 1,
    CAN_PROFILE_RESULT_BAD_SEQUENCE = 2,
    CAN_PROFILE_RESULT_BAD_CRC = 3,
    CAN_PROFILE_RESULT_INVALID_FIELD = 4,
    CAN_PROFILE_RESULT_BUSY = 5,
    CAN_PROFILE_RESULT_FLASH_ERROR = 6,
    CAN_PROFILE_RESULT_UNSUPPORTED_VERSION = 7,
} can_profile_result_t;

// Origine du profil actif d'une carte Moteur, codée sur les bits 3..4 de
// l'octet 3 de PROFILE_STATUS.
typedef enum {
    CAN_PROFILE_SOURCE_DEFAULTS = 0,  // valeurs de repli compilées
    CAN_PROFILE_SOURCE_NVM = 1,       // enregistrement flash valide
} can_profile_source_t;

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

// Diagnostic de résistance estimée (ADR-0009 §5c, §5d).
// `ohm_milliohm` et `i100_ma` valent CAN_IPROPI_VALUE_INVALID si non calculables.
typedef struct {
    uint16_t uid_tag;
    uint8_t reply_to;
    uint8_t channel;
    uint16_t ohm_milliohm;
    uint16_t i100_ma;
} can_ipropi_ohm_t;

typedef struct {
    uint16_t uid_tag;
    uint8_t channel;
    can_solenoid_command_kind_t command;
    uint8_t velocity;
} can_solenoid_command_t;

// PROFILE_BEGIN (ID 0x420, DLC 8) — Clavier vers diffusion.
typedef struct {
    uint8_t proto_version;
    uint8_t format_version;
    uint8_t transfer_id;
    uint8_t flags;
    uint16_t payload_len;
    uint8_t chunk_size;
    uint8_t reserved;
} can_profile_begin_t;

// PROFILE_CHUNK (ID 0x421, DLC 8) — Clavier vers diffusion.
// `seq` croissant depuis 0, sans trou ; `data` = CAN_PROFILE_CHUNK_SIZE octets.
typedef struct {
    uint8_t transfer_id;
    uint8_t seq;
    uint8_t data[CAN_PROFILE_CHUNK_SIZE];
} can_profile_chunk_t;

// PROFILE_COMMIT (ID 0x422, DLC 8) — Clavier vers diffusion.
typedef struct {
    uint8_t proto_version;
    uint8_t format_version;
    uint8_t transfer_id;
    uint8_t reserved;
    uint32_t crc32;
} can_profile_commit_t;

// PROFILE_STATUS (ID 0x423, DLC 8) — Moteur vers Clavier.
// `transfer_id` = 0 pour l'annonce de boot, sinon recopie du transfert.
// `profile_tag32` = CRC32 du payload canonique actif (voir solenoid_profile.h).
typedef struct {
    uint16_t uid_tag;
    uint8_t transfer_id;
    can_profile_result_t result;
    can_profile_source_t source;
    uint32_t profile_tag32;
} can_profile_status_t;

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

// IPROPI_OHM (ID 0x601, DLC 8) — diagnostic, ADR-0009 §6.
can_proto_result_t can_encode_ipropi_ohm(uint8_t out[CAN_DLC_IPROPI_OHM],
                                         const can_ipropi_ohm_t *ohm);
can_proto_result_t can_decode_ipropi_ohm(const uint8_t *data, uint8_t dlc,
                                         can_ipropi_ohm_t *ohm);

// PROFILE_BEGIN (ID 0x420, DLC 8) — ADR-0013 §3.
can_proto_result_t can_encode_profile_begin(uint8_t out[CAN_DLC_PROFILE_BEGIN],
                                            const can_profile_begin_t *begin);
can_proto_result_t can_decode_profile_begin(const uint8_t *data, uint8_t dlc,
                                            can_profile_begin_t *begin);

// PROFILE_CHUNK (ID 0x421, DLC 8) — ADR-0013 §3.
can_proto_result_t can_encode_profile_chunk(uint8_t out[CAN_DLC_PROFILE_CHUNK],
                                            const can_profile_chunk_t *chunk);
can_proto_result_t can_decode_profile_chunk(const uint8_t *data, uint8_t dlc,
                                            can_profile_chunk_t *chunk);

// PROFILE_COMMIT (ID 0x422, DLC 8) — ADR-0013 §3.
can_proto_result_t can_encode_profile_commit(uint8_t out[CAN_DLC_PROFILE_COMMIT],
                                             const can_profile_commit_t *commit);
can_proto_result_t can_decode_profile_commit(const uint8_t *data, uint8_t dlc,
                                             can_profile_commit_t *commit);

// PROFILE_STATUS (ID 0x423, DLC 8) — ADR-0013 §3.
// Octet 3 = `result` (bits 0..2) | `source` (bits 3..4), bits 5..7 réservés à 0.
can_proto_result_t can_encode_profile_status(uint8_t out[CAN_DLC_PROFILE_STATUS],
                                             const can_profile_status_t *status);
can_proto_result_t can_decode_profile_status(const uint8_t *data, uint8_t dlc,
                                             can_profile_status_t *status);

// CARD_EVENT (ID 0x424, DLC 8) — ADR-0018. Octet 3 = `state`, sauf pour
// COMMAND_IGNORED où il porte la raison (`can_card_ignore_reason_t`).
can_proto_result_t can_encode_card_event(uint8_t out[CAN_DLC_CARD_EVENT],
                                         const can_card_event_msg_t *event);
can_proto_result_t can_decode_card_event(const uint8_t *data, uint8_t dlc,
                                         can_card_event_msg_t *event);

// CARD_STATS_REQUEST (ID 0x403, DLC 4) / CARD_STATS (ID 0x425, DLC 8) — ADR-0020.
can_proto_result_t can_encode_card_stats_request(uint8_t out[CAN_DLC_CARD_STATS_REQUEST],
                                                 const can_card_stats_request_t *request);
can_proto_result_t can_decode_card_stats_request(const uint8_t *data, uint8_t dlc,
                                                 can_card_stats_request_t *request);
can_proto_result_t can_encode_card_stats(uint8_t out[CAN_DLC_CARD_STATS],
                                         const can_card_stats_t *stats);
can_proto_result_t can_decode_card_stats(const uint8_t *data, uint8_t dlc,
                                         can_card_stats_t *stats);

#endif  // CLAVICORE_CAN_PROTOCOL_H
