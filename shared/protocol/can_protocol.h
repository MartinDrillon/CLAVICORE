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
// Écoute IPROPI et mode REC (ADR-0032).
#define CAN_ID_LISTEN_PULSE 0x110u         // Moteur -> Clavier
#define CAN_ID_LISTEN_CONTROL 0x404u       // Clavier -> diffusion
#define CAN_ID_LISTEN_PARAM 0x405u         // Clavier -> diffusion
#define CAN_ID_LISTEN_NOISE_QUERY 0x406u   // Clavier -> diffusion
#define CAN_ID_LISTEN_STATUS 0x426u        // Moteur -> Clavier
#define CAN_ID_LISTEN_NOISE_REPORT 0x427u  // Moteur -> Clavier
#define CAN_ID_LISTEN_PULSE_TIMING 0x428u  // Moteur -> Clavier
#define CAN_ID_LISTEN_TRACE 0x429u         // Moteur -> Clavier

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
#define CAN_DLC_LISTEN_PULSE 8u
#define CAN_DLC_LISTEN_CONTROL 8u
#define CAN_DLC_LISTEN_PARAM 8u
#define CAN_DLC_LISTEN_NOISE_QUERY 6u
#define CAN_DLC_LISTEN_STATUS 8u
#define CAN_DLC_LISTEN_NOISE_REPORT 8u
#define CAN_DLC_LISTEN_PULSE_TIMING 8u
#define CAN_DLC_LISTEN_TRACE 8u

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
    CAN_CARD_EVENT_MANUAL_TEST_STARTED = 0,  // test manuel du canal démarré
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
    // ADR-0024 : ventilation des refus du pilote, sans changer les IDs 0–12.
    CAN_CARD_COUNTER_REJECT_SLEEP = 13,
    CAN_CARD_COUNTER_REJECT_REST = 14,
    CAN_CARD_COUNTER_REJECT_BUSY = 15,
    CAN_CARD_COUNTER_REJECT_FAULT = 16,
    CAN_CARD_COUNTER_REJECT_OTHER = 17,
    CAN_CARD_COUNTER_COUNT = 18,
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

// --- Écoute IPROPI et mode REC (ADR-0032) ----------------------------------

// Champs 12 bits et drapeaux (octets de contrôle).
#define CAN_LISTEN_ADC_MASK 0x0FFFu  // base, crête, min, max
#define CAN_LISTEN_CHANNEL_MASK 0x0Fu
#define CAN_LISTEN_PULSE_FLAG_SPLIT 0x10u      // bit 4 : coupure de trille
#define CAN_LISTEN_PULSE_FLAG_SATURATED 0x20u  // bit 5 : durée > 0xFFFF x 100 µs
#define CAN_LISTEN_NOISE_FLAG_TRUNCATED 0x01u  // fenêtre > 256 ms
#define CAN_LISTEN_TIMING_SATURATED 0xFFFFu    // 100 µs -> 6,55 s
#define CAN_LISTEN_TRACE_FRAME_SAMPLES 4u
#define CAN_LISTEN_TRACE_SAMPLES 4096u

// Mode porté par LISTEN_CONTROL[1] (0 OFF, 1 ÉCOUTE, 2 TRACE, 3 STREAM,
// 4 STREAM_OFF). STREAM_OFF revient au balayage sans quitter l'écoute.
typedef enum {
    CAN_LISTEN_MODE_OFF = 0,
    CAN_LISTEN_MODE_LISTEN = 1,
    CAN_LISTEN_MODE_TRACE = 2,
    CAN_LISTEN_MODE_STREAM = 3,
    CAN_LISTEN_MODE_STREAM_OFF = 4,
} can_listen_mode_t;

// État porté par LISTEN_STATUS[2]. Aucun état n'est silencieux : une écoute
// qui ne peut pas tourner est toujours nommée.
typedef enum {
    CAN_LISTEN_STATE_IDLE = 0,              // éteinte, Core 1 parqué
    CAN_LISTEN_STATE_LISTENING = 1,         // balayage 12 canaux
    CAN_LISTEN_STATE_TRACE = 2,             // capture d'un canal en cours
    CAN_LISTEN_STATE_STREAM = 3,            // flux d'un canal pendant le balayage
    CAN_LISTEN_STATE_PARAM_MISMATCH = 4,    // CRC ou validation du jeu refusé
    CAN_LISTEN_STATE_BUSY_SELFTEST = 5,     // LISTEN_CONTROL reçu pendant 0x401
    CAN_LISTEN_STATE_UNAVAILABLE_SLEEP = 6, // nSLEEP bas
    CAN_LISTEN_STATE_TRACE_TIMEOUT = 7,     // trace_timeout_ms écoulé
    CAN_LISTEN_STATE_FAULT = 8,             // FAULT actif sur au moins un canal
} can_listen_state_t;

// LISTEN_PULSE (0x110, DLC 8) — une impulsion = appui OU relâchement (IPROPI
// mesure |i|, le sens n'est pas lisible).
typedef struct {
    uint16_t uid_tag;
    uint8_t channel;  // 0..11
    uint8_t seq;      // recopie dans LISTEN_PULSE_TIMING
    uint16_t base;    // ligne de base ADC au début de l'impulsion (0..4095)
    uint16_t peak;    // maximum de l'impulsion (0..4095)
    bool split;       // fin par coupure de trille
    bool saturated;   // durée saturée (voir LISTEN_PULSE_TIMING)
} can_listen_pulse_t;

// LISTEN_CONTROL (0x404, DLC 8) — Clavier vers diffusion.
// `param_crc16` = listen_params_crc16() du jeu envoyé avant l'ordre.
typedef struct {
    uint8_t proto_version;
    can_listen_mode_t mode;
    uint16_t param_crc16;
    uint16_t target_uid_tag;  // TRACE/STREAM ; 0 en OFF/LISTEN
    uint8_t channel;          // 0..11 en TRACE/STREAM ; 0 sinon
} can_listen_control_t;

// LISTEN_PARAM (0x405, DLC 8) — un champ, valeur u32 little-endian.
// `param_id` = listen_param_id_t (listen_params.h) ; le codec le laisse opaque
// pour que le Moteur puisse nommer un id refusé.
typedef struct {
    uint8_t proto_version;
    uint8_t param_id;
    uint32_t value;
} can_listen_param_t;

// LISTEN_NOISE_QUERY (0x406, DLC 6) — Clavier vers diffusion.
typedef struct {
    uint16_t target_uid_tag;
    uint8_t channel;
    uint8_t request_id;    // 1..127, recopié dans le rapport
    uint16_t lookback_ms;  // <= 256, sinon le rapport est marqué tronqué
} can_listen_noise_query_t;

// LISTEN_STATUS (0x426, DLC 8) — état réel de la carte.
typedef struct {
    uint16_t uid_tag;
    can_listen_state_t state;
    uint8_t refused_param_id;  // 0 sauf refus de paramètre
    uint16_t sweep_period_us;  // période mesurée d'un balayage des 12 canaux
    uint8_t overflows;         // anneaux pleins, saturant à 255
} can_listen_status_t;

// LISTEN_NOISE_REPORT (0x427, DLC 8) — bruit du voisin demandé.
typedef struct {
    uint16_t uid_tag;
    uint8_t channel;
    uint8_t request_id;
    uint16_t min;   // 0..4095
    uint16_t max;   // 0..4095
    bool truncated; // fenêtre demandée > 256 ms
} can_listen_noise_report_t;

// LISTEN_PULSE_TIMING (0x428, DLC 8) — durée du même `seq`, unité 100 µs,
// saturée à 0xFFFF (6,55 s) ; `saturated` de LISTEN_PULSE est posé alors.
typedef struct {
    uint16_t uid_tag;
    uint8_t seq;
    uint16_t t_peak_100us;      // t_peak - t0
    uint16_t t_end_peak_100us;  // t_end - t_peak
} can_listen_pulse_timing_t;

// LISTEN_TRACE (0x429, DLC 8) — 4 échantillons 12 bits. Utilisée pour `trace`
// ET `stream`. Pas d'uid_tag (ADR-0032 §11) : le Clavier l'attribue à sa cible.
typedef struct {
    uint16_t seq;
    uint16_t samples[CAN_LISTEN_TRACE_FRAME_SAMPLES];  // 0..4095
} can_listen_trace_t;

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
bool can_listen_mode_is_valid(can_listen_mode_t mode);
bool can_listen_state_is_valid(can_listen_state_t state);
bool can_listen_channel_is_valid(uint8_t channel);  // < CAN_IPROPI_CHANNEL_COUNT

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

// LISTEN_PULSE (0x110, DLC 8) — ADR-0032 §9.
can_proto_result_t can_encode_listen_pulse(uint8_t out[CAN_DLC_LISTEN_PULSE],
                                           const can_listen_pulse_t *pulse);
can_proto_result_t can_decode_listen_pulse(const uint8_t *data, uint8_t dlc,
                                           can_listen_pulse_t *pulse);

// LISTEN_CONTROL (0x404, DLC 8).
can_proto_result_t can_encode_listen_control(uint8_t out[CAN_DLC_LISTEN_CONTROL],
                                             const can_listen_control_t *control);
can_proto_result_t can_decode_listen_control(const uint8_t *data, uint8_t dlc,
                                             can_listen_control_t *control);

// LISTEN_PARAM (0x405, DLC 8).
can_proto_result_t can_encode_listen_param(uint8_t out[CAN_DLC_LISTEN_PARAM],
                                           const can_listen_param_t *param);
can_proto_result_t can_decode_listen_param(const uint8_t *data, uint8_t dlc,
                                           can_listen_param_t *param);

// LISTEN_NOISE_QUERY (0x406, DLC 6).
can_proto_result_t can_encode_listen_noise_query(uint8_t out[CAN_DLC_LISTEN_NOISE_QUERY],
                                                 const can_listen_noise_query_t *query);
can_proto_result_t can_decode_listen_noise_query(const uint8_t *data, uint8_t dlc,
                                                 can_listen_noise_query_t *query);

// LISTEN_STATUS (0x426, DLC 8).
can_proto_result_t can_encode_listen_status(uint8_t out[CAN_DLC_LISTEN_STATUS],
                                            const can_listen_status_t *status);
can_proto_result_t can_decode_listen_status(const uint8_t *data, uint8_t dlc,
                                            can_listen_status_t *status);

// LISTEN_NOISE_REPORT (0x427, DLC 8).
can_proto_result_t can_encode_listen_noise_report(uint8_t out[CAN_DLC_LISTEN_NOISE_REPORT],
                                                  const can_listen_noise_report_t *report);
can_proto_result_t can_decode_listen_noise_report(const uint8_t *data, uint8_t dlc,
                                                  can_listen_noise_report_t *report);

// LISTEN_PULSE_TIMING (0x428, DLC 8).
can_proto_result_t can_encode_listen_pulse_timing(uint8_t out[CAN_DLC_LISTEN_PULSE_TIMING],
                                                  const can_listen_pulse_timing_t *timing);
can_proto_result_t can_decode_listen_pulse_timing(const uint8_t *data, uint8_t dlc,
                                                  can_listen_pulse_timing_t *timing);

// LISTEN_TRACE (0x429, DLC 8).
can_proto_result_t can_encode_listen_trace(uint8_t out[CAN_DLC_LISTEN_TRACE],
                                           const can_listen_trace_t *trace);
can_proto_result_t can_decode_listen_trace(const uint8_t *data, uint8_t dlc,
                                           can_listen_trace_t *trace);

#endif  // CLAVICORE_CAN_PROTOCOL_H
