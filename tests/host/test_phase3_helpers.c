// Tests unitaires hôte des helpers purs : can_protocol, commissioning_table,
// uid_position_format, diag_log et ipropi_ohm (math Moteur d'ADR-0009).
#include <stdio.h>
#include <string.h>

#include "can_protocol.h"
#include "commissioning_table.h"
#include "diag_log.h"
#include "ipropi_ohm.h"
#include "profile_tuning.h"
#include "solenoid_profile.h"
#include "uid_position_format.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("ECHEC %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                    \
    } while (0)

static const uint8_t UID_A[8] = {0xE6, 0x60, 0x58, 0x38, 0x83, 0x12, 0x34, 0x56};
static const uint8_t UID_B[8] = {0xAA, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
static const uint8_t UID_C[8] = {0x11, 0x22, 0x33, 0x44, 0x88, 0x99, 0xAB, 0xCD};
static const uint8_t UID_D[8] = {0x01, 0x02, 0x03, 0x04, 0x99, 0x88, 0x77, 0x66};
static const uint8_t UID_E[8] = {0x0A, 0x0B, 0x0C, 0x0D, 0xEE, 0xDD, 0xCC, 0xBB};

static commissioning_entry_t make_entry(const uint8_t uid[8], uint8_t position, uint16_t bitmap) {
    commissioning_entry_t entry = {0};
    memcpy(entry.identity.uid.id, uid, 8u);
    entry.identity.position = position;
    entry.occupation_bitmap = bitmap;
    entry.reserved = 0u;
    return entry;
}

static void test_crc_and_ids(void) {
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint8_t b[2];

    CHECK(can_crc16_ccitt(check, sizeof(check)) == 0x29B1u);
    CHECK(can_uid_tag(UID_A) == can_crc16_ccitt(UID_A, 8u));
    CHECK(can_announce_delay_us(0x29B1u) == 49u * 1000u);
    can_put_u16_le(b, 0x1234u);
    CHECK(b[0] == 0x34u && b[1] == 0x12u);
    CHECK(can_get_u16_le(b) == 0x1234u);
    CHECK(can_next_request_id(127u) == 1u);
    CHECK(can_id_is_forbidden(0x7F0u));
}

static void test_solenoid_command_codec(void) {
    uint8_t data[CAN_DLC_SOLENOID_COMMAND];
    can_solenoid_command_t cmd = {0x29B1u, 11u, CAN_SOLENOID_COMMAND_ON, 100u};
    can_solenoid_command_t out = {0};

    CHECK(can_encode_solenoid_command(data, &cmd) == CAN_PROTO_OK);
    CHECK(data[0] == 0xB1u && data[1] == 0x29u && data[2] == 11u && data[3] == 1u &&
          data[4] == 100u);
    CHECK(can_decode_solenoid_command(data, CAN_DLC_SOLENOID_COMMAND, &out) == CAN_PROTO_OK);
    CHECK(out.uid_tag == cmd.uid_tag && out.channel == cmd.channel &&
          out.command == cmd.command && out.velocity == cmd.velocity);
    data[2] = 12u;
    CHECK(can_decode_solenoid_command(data, CAN_DLC_SOLENOID_COMMAND, &out) ==
          CAN_PROTO_BAD_VALUE);
    data[2] = 11u;
    data[3] = 2u;
    CHECK(can_decode_solenoid_command(data, CAN_DLC_SOLENOID_COMMAND, &out) == CAN_PROTO_OK);
    CHECK(out.command == CAN_SOLENOID_COMMAND_PUSH_ON);
    data[3] = 3u;
    CHECK(can_decode_solenoid_command(data, CAN_DLC_SOLENOID_COMMAND, &out) ==
          CAN_PROTO_BAD_VALUE);
    CHECK(can_decode_solenoid_command(data, 4u, &out) == CAN_PROTO_BAD_DLC);
}

static void test_uid_request_and_announce(void) {
    uint8_t uid_req[CAN_DLC_UID_REQUEST];
    uint8_t status_buf[CAN_DLC_ANNOUNCE_STATUS];
    uint8_t raw_buf[CAN_DLC_IPROPI_RAW];
    can_uid_request_t request = {CAN_PROTO_VERSION, 5u};
    can_uid_request_t request_out = {0};
    can_announce_status_t status = {0x29B1u, 0x0A5u, 7u, CAN_SELFTEST_STATUS_OK, 0x012u};
    can_announce_status_t status_out = {0};
    can_ipropi_raw_t raw = {0x29B1u, 7u, 3u, 12u, 345u};
    can_ipropi_raw_t raw_out = {0};

    CHECK(can_encode_uid_request(uid_req, &request) == CAN_PROTO_OK);
    CHECK(can_decode_uid_request(uid_req, CAN_DLC_UID_REQUEST, &request_out) == CAN_PROTO_OK);
    CHECK(request_out.request_id == 5u);
    CHECK(can_encode_announce_status(status_buf, &status) == CAN_PROTO_OK);
    CHECK(can_decode_announce_status(status_buf, CAN_DLC_ANNOUNCE_STATUS, &status_out) ==
          CAN_PROTO_OK);
    CHECK(status_out.occupation_bits == 0x0A5u && status_out.fault_bits == 0x012u);
    CHECK(can_encode_ipropi_raw(raw_buf, &raw) == CAN_PROTO_OK);
    CHECK(can_decode_ipropi_raw(raw_buf, CAN_DLC_IPROPI_RAW, &raw_out) == CAN_PROTO_OK);
    CHECK(raw_out.channel == 3u && raw_out.probe_avg == 345u);
}

static void test_uid_position_table(void) {
    uid_position_entry_t table[UID_POSITION_COUNT];
    clavicore_uid64_t uid = {{0}};
    uint8_t pos = 0xAAu;

    memset(table, 0, sizeof(table));
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        table[i].position = UID_POSITION_EMPTY;
    }

    memcpy(uid.id, UID_A, 8u);
    CHECK(uid_position_table_is_valid(table, UID_POSITION_COUNT));
    CHECK(uid_position_lookup(table, UID_POSITION_COUNT, &uid, &pos) == UID_POSITION_NOT_FOUND);

    memcpy(table[1].uid.id, UID_A, 8u);
    table[1].position = 3u;
    CHECK(uid_position_table_is_valid(table, UID_POSITION_COUNT));
    CHECK(uid_position_lookup(table, UID_POSITION_COUNT, &uid, &pos) == UID_POSITION_FOUND);
    CHECK(pos == 3u);
}

static void test_commissioning_assignment_examples(void) {
    commissioning_runtime_t runtime;
    commissioning_note_target_t target;
    commissioning_entry_t single_hole[] = {make_entry(UID_A, 4u, 0x00Bu)};
    commissioning_entry_t single_sparse[] = {make_entry(UID_A, 4u, 0x841u)};
    commissioning_entry_t two_boards[] = {
        make_entry(UID_A, 4u, 0x00Bu),
        make_entry(UID_B, 3u, 0x003u),
    };

    // ADR-0010 : ancrage grave. Une carte seule reçoit le bloc grave quel que soit
    // son numéro de position (le décalage est signalé côté Clavier, pas corrigé).
    CHECK(commissioning_build_runtime(single_hole, 1u, &runtime) == COMMISSIONING_OK);
    CHECK(runtime.partial);
    CHECK(runtime.position_first_key[4] == 1u && runtime.position_last_key[4] == 3u);
    CHECK(commissioning_lookup_midi_note(&runtime, 36u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.channel == 0u && target.key == 1u);
    CHECK(commissioning_lookup_midi_note(&runtime, 37u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 1u && target.key == 2u);
    CHECK(commissioning_lookup_midi_note(&runtime, 38u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 3u && target.key == 3u);
    CHECK(commissioning_lookup_midi_note(&runtime, 39u, &target) == COMMISSIONING_NOTE_NOT_FOUND);

    // Trous de bitmap : les canaux occupés croissants montent dans le bloc.
    CHECK(commissioning_build_runtime(single_sparse, 1u, &runtime) == COMMISSIONING_OK);
    CHECK(commissioning_lookup_midi_note(&runtime, 36u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 0u && target.key == 1u);
    CHECK(commissioning_lookup_midi_note(&runtime, 37u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 6u && target.key == 2u);
    CHECK(commissioning_lookup_midi_note(&runtime, 38u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 11u && target.key == 3u);

    // Deux cartes : la position la plus basse prend le bloc grave.
    CHECK(commissioning_build_runtime(two_boards, 2u, &runtime) == COMMISSIONING_OK);
    CHECK(runtime.position_first_key[3] == 1u && runtime.position_last_key[3] == 2u);
    CHECK(runtime.position_first_key[4] == 3u && runtime.position_last_key[4] == 5u);
    CHECK(commissioning_lookup_midi_note(&runtime, 36u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 3u && target.channel == 0u && target.key == 1u);
    CHECK(commissioning_lookup_midi_note(&runtime, 37u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 3u && target.channel == 1u && target.key == 2u);
    CHECK(commissioning_lookup_midi_note(&runtime, 38u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.channel == 0u && target.key == 3u);
    CHECK(commissioning_lookup_midi_note(&runtime, 39u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.channel == 1u && target.key == 4u);
    CHECK(commissioning_lookup_midi_note(&runtime, 40u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.channel == 3u && target.key == 5u);
}

static void test_commissioning_totals_and_duplicates(void) {
    commissioning_runtime_t runtime;
    commissioning_entry_t partial[] = {
        make_entry(UID_A, 4u, 0x001u),
        make_entry(UID_B, 3u, 0x003u),
    };
    commissioning_entry_t too_many[] = {
        make_entry(UID_A, 4u, 0xFFFu),
        make_entry(UID_B, 3u, 0xFFFu),
        make_entry(UID_C, 2u, 0xFFFu),
        make_entry(UID_D, 1u, 0xFFFu),
        make_entry(UID_E, 0u, 0xFFFu),
    };
    commissioning_entry_t duplicate_tag[] = {
        make_entry(UID_A, 4u, 0x001u),
        make_entry(UID_A, 3u, 0x002u),
    };

    CHECK(commissioning_build_runtime(partial, 2u, &runtime) == COMMISSIONING_OK);
    CHECK(runtime.total_occupied == 3u && runtime.partial);
    CHECK(commissioning_build_runtime(too_many, 5u, &runtime) == COMMISSIONING_TOO_MANY_KEYS);
    CHECK(commissioning_build_runtime(duplicate_tag, 2u, &runtime) ==
          COMMISSIONING_DUPLICATE_UID);
}

static void test_commissioning_five_boards_total_49(void) {
    commissioning_runtime_t runtime;
    commissioning_note_target_t target;
    commissioning_entry_t full[] = {
        make_entry(UID_E, 0u, 0x1FFu),
        make_entry(UID_D, 1u, 0x3FFu),
        make_entry(UID_C, 2u, 0x3FFu),
        make_entry(UID_B, 3u, 0x3FFu),
        make_entry(UID_A, 4u, 0x3FFu),
    };

    CHECK(commissioning_build_runtime(full, 5u, &runtime) == COMMISSIONING_OK);
    CHECK(!runtime.partial && runtime.total_occupied == 49u);
    // Positions 0..4 contiguës : chaque carte garde son bloc (ADR-0010 §3).
    // pos 0 = 9 canaux (0x1FF) -> touches 1..9 ; pos 1..4 = 10 canaux chacune ->
    // 10..19, 20..29, 30..39, 40..49.
    static const uint8_t expected_first[UID_POSITION_COUNT] = {1u, 10u, 20u, 30u, 40u};
    static const uint8_t expected_last[UID_POSITION_COUNT] = {9u, 19u, 29u, 39u, 49u};
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        CHECK(runtime.position_present[position]);
        CHECK(runtime.position_first_key[position] == expected_first[position]);
        CHECK(runtime.position_last_key[position] == expected_last[position]);
    }
    CHECK(commissioning_lookup_midi_note(&runtime, 36u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 0u && target.key == 1u);
    CHECK(commissioning_lookup_midi_note(&runtime, 44u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 0u && target.channel == 8u && target.key == 9u);
    CHECK(commissioning_lookup_midi_note(&runtime, 45u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 1u && target.channel == 0u && target.key == 10u);
    CHECK(commissioning_lookup_midi_note(&runtime, 84u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.key == 49u);
}

// ADR-0010 §3 : ancrage grave et contiguïté depuis la position 0.
static void test_commissioning_anchor_from_position_zero(void) {
    commissioning_runtime_t runtime;
    commissioning_note_target_t target;
    commissioning_entry_t hole[] = {
        make_entry(UID_A, 0u, 0x001u),
        make_entry(UID_B, 2u, 0x001u),
    };

    // Trou de position : le remplissage reste compact et croissant ; la position 0
    // conserve la touche 1 (DO2) et rien ne sort de 1..49.
    CHECK(commissioning_build_runtime(hole, 2u, &runtime) == COMMISSIONING_OK);
    CHECK(runtime.total_occupied == 2u && runtime.partial);
    CHECK(runtime.position_first_key[0] == 1u && runtime.position_last_key[0] == 1u);
    CHECK(runtime.position_first_key[2] == 2u && runtime.position_last_key[2] == 2u);
    CHECK(!runtime.position_present[1]);
    CHECK(commissioning_lookup_midi_note(&runtime, 36u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 0u && target.channel == 0u && target.key == 1u);
    CHECK(commissioning_lookup_midi_note(&runtime, 37u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 2u && target.channel == 0u && target.key == 2u);
    CHECK(commissioning_lookup_midi_note(&runtime, 38u, &target) == COMMISSIONING_NOTE_NOT_FOUND);
}

static void put_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8u) & 0xFFu);
    out[2] = (uint8_t)((value >> 16u) & 0xFFu);
    out[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void test_commissioning_flash_crc_and_header(void) {
    commissioning_entry_t decoded[UID_POSITION_COUNT];
    commissioning_table_state_t state = COMMISSIONING_TABLE_STATE_PROVISIONAL;
    uint8_t sector[COMMISSIONING_FLASH_SECTOR_SIZE];
    uint8_t flags = 0xFFu;
    uint16_t count = 0u;
    commissioning_entry_t entries[] = {
        make_entry(UID_A, 4u, 0x00Bu),
        make_entry(UID_B, 3u, 0x003u),
    };

    CHECK(COMMISSIONING_FLASH_FORMAT_VERSION == 2u);
    CHECK(COMMISSIONING_FLASH_HEADER_SIZE == 16u);
    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u,
                                     COMMISSIONING_TABLE_STATE_FINAL, 0u) == COMMISSIONING_OK);
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_OK);
    CHECK(count == 2u);
    CHECK(state == COMMISSIONING_TABLE_STATE_FINAL && flags == 0u);
    CHECK(sector[12] == 0u && sector[13] == 0u);
    CHECK(commissioning_entry_equal(&decoded[0], &entries[0]));
    CHECK(commissioning_entry_equal(&decoded[1], &entries[1]));

    // CRC : un octet d'enregistrement corrompu est rejeté (enregistrements à
    // partir de l'octet 16 en v2).
    sector[COMMISSIONING_FLASH_HEADER_SIZE] ^= 0x01u;
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_BAD_CRC);
    sector[COMMISSIONING_FLASH_HEADER_SIZE] ^= 0x01u;

    sector[0] = 0u;
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_BAD_MAGIC);
    sector[0] = (uint8_t)'C';
    sector[1] = (uint8_t)'L';
    sector[2] = (uint8_t)'V';
    sector[3] = (uint8_t)'C';
    can_put_u16_le(&sector[4], 0xFFu);
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_BAD_VERSION);

    // Arguments obligatoires : l'état ne doit jamais être ignoré silencieusement.
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     NULL, &flags) == COMMISSIONING_BAD_ARG);
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, NULL) == COMMISSIONING_BAD_ARG);
}

static void test_commissioning_flash_state_and_legacy(void) {
    commissioning_entry_t decoded[UID_POSITION_COUNT];
    commissioning_table_state_t state = COMMISSIONING_TABLE_STATE_FINAL;
    uint8_t sector[COMMISSIONING_FLASH_SECTOR_SIZE];
    uint8_t legacy[COMMISSIONING_FLASH_SECTOR_SIZE];
    uint8_t flags = 0u;
    uint16_t count = 0u;
    commissioning_entry_t entries[] = {
        make_entry(UID_A, 4u, 0x00Bu),
        make_entry(UID_B, 3u, 0x003u),
    };
    const uint8_t provisory_flags =
        (uint8_t)(COMMISSIONING_FLAG_MISSING_BOARD | COMMISSIONING_FLAG_NOT_49_KEYS |
                  COMMISSIONING_FLAG_ANCHOR_SHIFT);

    // Aller-retour d'une table PROVISIONAL avec motifs.
    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u,
                                     COMMISSIONING_TABLE_STATE_PROVISIONAL,
                                     provisory_flags) == COMMISSIONING_OK);
    CHECK(sector[12] == (uint8_t)COMMISSIONING_TABLE_STATE_PROVISIONAL);
    CHECK(sector[13] == provisory_flags);
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_OK);
    CHECK(state == COMMISSIONING_TABLE_STATE_PROVISIONAL);
    CHECK(flags == provisory_flags);
    CHECK(commissioning_table_state_from_flags(0u) == COMMISSIONING_TABLE_STATE_FINAL);
    CHECK(commissioning_table_state_from_flags(provisory_flags) ==
          COMMISSIONING_TABLE_STATE_PROVISIONAL);

    // Cohérence état / motifs exigée à l'encodage.
    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u,
                                     COMMISSIONING_TABLE_STATE_FINAL,
                                     provisory_flags) == COMMISSIONING_BAD_STATE);
    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u,
                                     COMMISSIONING_TABLE_STATE_PROVISIONAL,
                                     0u) == COMMISSIONING_BAD_STATE);
    // 0x40 est désormais un motif valide (`ANCHOR_SHIFT`, ADR-0010) ; tout bit
    // hors `COMMISSIONING_FLAG_ALL` reste refusé.
    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u,
                                     COMMISSIONING_TABLE_STATE_PROVISIONAL,
                                     0x40u) == COMMISSIONING_OK);
    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u,
                                     COMMISSIONING_TABLE_STATE_FINAL,
                                     0x80u) == COMMISSIONING_BAD_STATE);

    // Motif incohérent en flash : refusé à la lecture.
    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u,
                                     COMMISSIONING_TABLE_STATE_FINAL, 0u) == COMMISSIONING_OK);
    sector[13] = COMMISSIONING_FLAG_MISSING_BOARD;
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_BAD_STATE);
    sector[12] = 0x7Fu;
    sector[13] = 0u;
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_BAD_STATE);

    // Table v1 (en-tête 12 octets, enregistrements à l'octet 12) : lisible et
    // interprétée FINAL / aucun motif (ADR-0009 §8).
    memset(legacy, 0xFF, sizeof(legacy));
    put_u32_le(&legacy[0], COMMISSIONING_FLASH_MAGIC);
    can_put_u16_le(&legacy[4], COMMISSIONING_FLASH_FORMAT_VERSION_LEGACY);
    can_put_u16_le(&legacy[6], 1u);
    memcpy(&legacy[12], UID_A, 8u);
    legacy[20] = 4u;
    can_put_u16_le(&legacy[21], 0x00Bu);
    can_put_u16_le(&legacy[23], 0u);
    put_u32_le(&legacy[8], commissioning_crc32(&legacy[12], COMMISSIONING_FLASH_ENTRY_SIZE));
    state = COMMISSIONING_TABLE_STATE_PROVISIONAL;
    flags = 0xFFu;
    CHECK(commissioning_flash_decode(legacy, sizeof(legacy), decoded, UID_POSITION_COUNT, &count,
                                     &state, &flags) == COMMISSIONING_OK);
    CHECK(count == 1u);
    CHECK(state == COMMISSIONING_TABLE_STATE_FINAL && flags == 0u);
    CHECK(commissioning_entry_equal(&decoded[0], &entries[0]));
}

static void test_ipropi_ohm_math(void) {
    const ipropi_ohm_config_t config = {
        .vm_millivolt = 24000u,
        .vref_millivolt = 3300u,
        .ipropi_ua_per_a = 450u,
        .ipropi_resistor_ohm = 2400u,
        .rdson_milliohm = 240u,
        .duty_permil = 200u,
    };
    ipropi_ohm_result_t result;

    // Repères de l'ADR-0009 (IMP-011), delta = probe - idle.
    result = ipropi_ohm_compute(&config, 1242u, 212u, 400u);
    CHECK(result.valid);
    CHECK(result.ohm_milliohm == 6005u);
    CHECK(result.i100_ma == 3843u);

    result = ipropi_ohm_compute(&config, 1000u, 200u, 400u);
    CHECK(result.valid && result.ohm_milliohm == 7801u && result.i100_ma == 2984u);

    result = ipropi_ohm_compute(&config, 1400u, 200u, 400u);
    CHECK(result.valid && result.ohm_milliohm == 5120u && result.i100_ma == 4477u);

    // Seuil d'occupation : pile au seuil => valide.
    result = ipropi_ohm_compute(&config, 600u, 200u, 400u);
    CHECK(result.valid && result.ohm_milliohm == 15842u && result.i100_ma == 1492u);

    // Canal vide ou incohérent : invalide, jamais 0 ohm.
    result = ipropi_ohm_compute(&config, 599u, 200u, 400u);
    CHECK(!result.valid && result.ohm_milliohm == CAN_IPROPI_VALUE_INVALID &&
          result.i100_ma == CAN_IPROPI_VALUE_INVALID);
    result = ipropi_ohm_compute(&config, 200u, 200u, 400u);
    CHECK(!result.valid && result.ohm_milliohm == CAN_IPROPI_VALUE_INVALID);
    result = ipropi_ohm_compute(&config, 100u, 200u, 400u);
    CHECK(!result.valid);

    // Configuration incomplète : invalide, sans division par zéro.
    result = ipropi_ohm_compute(NULL, 1242u, 212u, 400u);
    CHECK(!result.valid);
    result = ipropi_ohm_compute(&(ipropi_ohm_config_t){.vm_millivolt = 24000u,
                                                       .vref_millivolt = 3300u,
                                                       .ipropi_ua_per_a = 450u,
                                                       .ipropi_resistor_ohm = 2400u,
                                                       .rdson_milliohm = 240u,
                                                       .duty_permil = 0u},
                                1242u, 212u, 400u);
    CHECK(!result.valid);
}

static void test_ipropi_ohm_codec(void) {
    uint8_t data[CAN_DLC_IPROPI_OHM];
    can_ipropi_ohm_t ohm = {0x29B1u, 42u, 3u, 6005u, 3846u};
    can_ipropi_ohm_t out = {0};

    CHECK(CAN_ID_IPROPI_OHM == 0x601u);
    CHECK(CAN_DLC_IPROPI_OHM == 8u);
    CHECK(can_encode_ipropi_ohm(data, &ohm) == CAN_PROTO_OK);
    CHECK(data[0] == 0xB1u && data[1] == 0x29u && data[2] == 42u && data[3] == 3u);
    CHECK(can_get_u16_le(&data[4]) == 6005u && can_get_u16_le(&data[6]) == 3846u);
    CHECK(can_decode_ipropi_ohm(data, CAN_DLC_IPROPI_OHM, &out) == CAN_PROTO_OK);
    CHECK(out.uid_tag == 0x29B1u && out.reply_to == 42u && out.channel == 3u);
    CHECK(out.ohm_milliohm == 6005u && out.i100_ma == 3846u);

    // Valeur invalide : transportée telle quelle, pas une erreur de trame.
    ohm.ohm_milliohm = CAN_IPROPI_VALUE_INVALID;
    ohm.i100_ma = CAN_IPROPI_VALUE_INVALID;
    CHECK(can_encode_ipropi_ohm(data, &ohm) == CAN_PROTO_OK);
    CHECK(can_decode_ipropi_ohm(data, CAN_DLC_IPROPI_OHM, &out) == CAN_PROTO_OK);
    CHECK(out.ohm_milliohm == CAN_IPROPI_VALUE_INVALID &&
          out.i100_ma == CAN_IPROPI_VALUE_INVALID);

    // DLC strict, canal hors plage, reply_to hors plage.
    CHECK(can_decode_ipropi_ohm(data, 7u, &out) == CAN_PROTO_BAD_DLC);
    ohm.channel = CAN_IPROPI_CHANNEL_COUNT;
    CHECK(can_encode_ipropi_ohm(data, &ohm) == CAN_PROTO_BAD_VALUE);
    ohm.channel = 3u;
    ohm.reply_to = 0u;
    CHECK(can_encode_ipropi_ohm(data, &ohm) == CAN_PROTO_BAD_VALUE);
    CHECK(can_encode_ipropi_ohm(data, &ohm) == CAN_PROTO_BAD_VALUE);
    ohm.reply_to = 42u;
    CHECK(can_encode_ipropi_ohm(data, &ohm) == CAN_PROTO_OK);
    data[3] = CAN_IPROPI_CHANNEL_COUNT;
    CHECK(can_decode_ipropi_ohm(data, CAN_DLC_IPROPI_OHM, &out) == CAN_PROTO_BAD_VALUE);
}

static void test_diag_lines(void) {
    char out[DIAG_LOG_LINE_MAX];
    diag_uid_report_t uid = {0u, {0}, 0u, 1u, 0u, 1u, 0x7Fu};
    diag_table_entry_t table = {4u, {0}, 0x00Bu, 1u, 3u, true};
    diag_selftest_report_t report = {42u, 4u, CAN_SELFTEST_STATUS_OK, 0x0A5u, 0x003u, 0xF29Bu};
    diag_selftest_raw_report_t raw = {42u, 4u, 3u, 12u, 345u, 6005u, 3846u};
    diag_card_report_t card = {4u, 6u, {0}, DIAG_CARD_STATE_OK, 0x00Bu,
                               "0,1,3", "1,2,3", "DO2(36),DO#2(37),RE2(38)"};
    diag_summary_report_t summary = {1u, 1u, 3u, 3u, "4-49", "WRITTEN"};
    diag_boot_report_t boot = {0u, 1u, 0u, 1u, "2026-10-22 12:00:00", "loaded", 2u};

    memcpy(uid.uid, UID_A, 8u);
    memcpy(table.uid, UID_A, 8u);
    memcpy(card.uid, UID_A, 8u);
    CHECK(diag_format_uid_line(out, sizeof(out), &uid) ==
          strlen("[UID] pos=127 uid=E660583883123456 fw=0.1.0 proto=1 reply_to=0"));
    boot.fw_major = 0u;
    boot.fw_minor = 1u;
    boot.fw_patch = 0u;
    boot.proto_version = 1u;
    boot.build_id = "2026-10-22 12:00:00";
    boot.table_state = "loaded";
    boot.cards = 2u;
    CHECK(diag_format_boot_line(out, sizeof(out), &boot) ==
          strlen("[BOOT] fw=0.1.0 proto=1 build=2026-10-22 12:00:00 table=loaded cards=2"));
    CHECK(strcmp(out, "[BOOT] fw=0.1.0 proto=1 build=2026-10-22 12:00:00 table=loaded cards=2") ==
          0);
    boot.build_id = NULL;
    CHECK(diag_format_boot_line(out, sizeof(out), &boot) != 0u);
    CHECK(strcmp(out, "[BOOT] fw=0.1.0 proto=1 build=inconnu table=loaded cards=2") == 0);
    CHECK(diag_format_help_line(out, sizeof(out)) ==
          strlen("[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash"));
    CHECK(diag_format_trigger_line(out, sizeof(out), "bouton", 42u) ==
          strlen("[TRIGGER] source=bouton req=42 -> auto-test lance"));
    CHECK(strcmp(out, "[TRIGGER] source=bouton req=42 -> auto-test lance") == 0);
    CHECK(diag_format_trigger_line(out, sizeof(out), "console", 7u) ==
          strlen("[TRIGGER] source=console req=7 -> auto-test lance"));
    CHECK(strcmp(out, "[TRIGGER] source=console req=7 -> auto-test lance") == 0);
    CHECK(diag_format_trigger_refused_line(out, sizeof(out), "console", "jeu en cours") ==
          strlen("[TRIGGER] source=console -> refus: jeu en cours"));
    CHECK(strcmp(out, "[TRIGGER] source=console -> refus: jeu en cours") == 0);
    CHECK(diag_format_table_loaded_line(out, sizeof(out), true, "count=2") ==
          strlen("[TABLE] loaded count=2"));
    CHECK(strcmp(out, "[TABLE] loaded count=2") == 0);
    CHECK(diag_format_table_entry_line(out, sizeof(out), &table) ==
          strlen("[TABLE] pos=4 uid=E660583883123456 bitmap=0x00B keys=1-3"));
    CHECK(strcmp(out, "[TABLE] pos=4 uid=E660583883123456 bitmap=0x00B keys=1-3") == 0);
    CHECK(diag_format_diff_line(out, sizeof(out), 4u, 0x003u, 0x00Bu) ==
          strlen("[DIFF] pos=4 ancien=0x003 nouveau=0x00B"));
    CHECK(strcmp(out, "[DIFF] pos=4 ancien=0x003 nouveau=0x00B") == 0);
    CHECK(diag_format_note_name_fr(out, sizeof(out), 82u));
    CHECK(strcmp(out, "LA#5(82)") == 0);
    // Bornes de la plage routable 36..84 et convention d'octave (ADR-0008).
    CHECK(diag_format_note_name_fr(out, sizeof(out), 36u));
    CHECK(strcmp(out, "DO2(36)") == 0);
    CHECK(diag_format_note_name_fr(out, sizeof(out), 60u));
    CHECK(strcmp(out, "DO4(60)") == 0);
    CHECK(diag_format_note_name_fr(out, sizeof(out), 84u));
    CHECK(strcmp(out, "DO6(84)") == 0);
    CHECK(!diag_format_note_name_fr(out, sizeof(out), 35u));
    CHECK(diag_format_card_line(out, sizeof(out), &card) ==
          strlen("[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00B channels=0,1,3 keys=1,2,3 notes=DO2(36),DO#2(37),RE2(38)"));
    CHECK(strcmp(out, "[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00B channels=0,1,3 keys=1,2,3 notes=DO2(36),DO#2(37),RE2(38)") == 0);
    CHECK(diag_format_card_unknown_line(out, sizeof(out), UID_B) ==
          strlen("[CARD_UNKNOWN] uid=AA10203040506070"));
    CHECK(strcmp(out, "[CARD_UNKNOWN] uid=AA10203040506070") == 0);
    CHECK(diag_format_summary_line(out, sizeof(out), &summary) ==
          strlen("[SUMMARY] cards_ok=1/1 channels=3 keys=3/49 missing_keys=4-49 table=WRITTEN"));
    CHECK(strcmp(out, "[SUMMARY] cards_ok=1/1 channels=3 keys=3/49 missing_keys=4-49 table=WRITTEN") == 0);
    CHECK(diag_format_note_line(out, sizeof(out), 84u, 49u, 4u, 3u) ==
          strlen("[NOTE] midi=84 key=49 pos=4 ch=3"));
    CHECK(strcmp(out, "[NOTE] midi=84 key=49 pos=4 ch=3") == 0);
    CHECK(diag_format_mode_line(out, sizeof(out), true) ==
          strlen("[MODE] maintenance=ON"));
    CHECK(strcmp(out, "[MODE] maintenance=ON") == 0);
    CHECK(diag_format_push_line(out, sizeof(out), 84u, 49u, 4u, 3u, DIAG_PUSH_EVENT_TIMEOUT) ==
          strlen("[PUSH] midi=84 key=49 pos=4 ch=3 timeout"));
    CHECK(strcmp(out, "[PUSH] midi=84 key=49 pos=4 ch=3 timeout") == 0);
    CHECK(diag_format_status_line(out, sizeof(out), DIAG_STATUS_MAINTENANCE_BUSY) ==
          strlen("[STATUS] code=MAINTENANCE_BUSY"));
    CHECK(strcmp(out, "[STATUS] code=MAINTENANCE_BUSY") == 0);
    CHECK(diag_format_note_dropped_line(out, sizeof(out), 12u,
                                        DIAG_NOTE_DROPPED_TABLE_INVALID) ==
          strlen("[NOTE_DROPPED] midi=12 reason=table_invalid"));
    CHECK(strcmp(out, "[NOTE_DROPPED] midi=12 reason=table_invalid") == 0);
    CHECK(diag_format_selftest_line(out, sizeof(out), &report) ==
          strlen("[SELFTEST] req=42 pos=4 status=OK bitmap=0x0A5 faults=0x003 tag=F29B"));
    CHECK(diag_format_selftest_raw_line(out, sizeof(out), &raw) ==
          strlen("[SELFTEST_RAW] req=42 pos=4 ch=3 idle=12 probe=345"));
    CHECK(strcmp(out, "[SELFTEST_RAW] req=42 pos=4 ch=3 idle=12 probe=345") == 0);
    CHECK(diag_format_selftest_raw_delta_line(out, sizeof(out), &raw) ==
          strlen("[SELFTEST_RAW] req=42 pos=4 ch=3 delta=333 ohm_est=6.00 i100=3.84"));
    CHECK(strcmp(out, "[SELFTEST_RAW] req=42 pos=4 ch=3 delta=333 ohm_est=6.00 i100=3.84") == 0);

    raw.ohm_milliohm = CAN_IPROPI_VALUE_INVALID;
    raw.i100_ma = CAN_IPROPI_VALUE_INVALID;
    CHECK(diag_format_selftest_raw_delta_line(out, sizeof(out), &raw) ==
          strlen("[SELFTEST_RAW] req=42 pos=4 ch=3 delta=333 ohm_est=-- i100=--"));
    CHECK(strcmp(out, "[SELFTEST_RAW] req=42 pos=4 ch=3 delta=333 ohm_est=-- i100=--") == 0);

    CHECK(diag_format_status_line(out, sizeof(out), DIAG_STATUS_TABLE_PROVISIONAL) ==
          strlen("[STATUS] code=TABLE_PROVISIONAL"));
    CHECK(strcmp(out, "[STATUS] code=TABLE_PROVISIONAL") == 0);

    diag_table_state_t table_state = {COMMISSIONING_TABLE_STATE_PROVISIONAL,
                                      (uint8_t)(COMMISSIONING_FLAG_MISSING_BOARD |
                                                COMMISSIONING_FLAG_NOT_49_KEYS),
                                      1u, 3u};
    CHECK(diag_format_table_state_line(out, sizeof(out), &table_state) ==
          strlen("[TABLE] loaded state=PROVISIONAL flags=0x11 cards=1 keys=3"));
    CHECK(strcmp(out, "[TABLE] loaded state=PROVISIONAL flags=0x11 cards=1 keys=3") == 0);
    // État incohérent avec les motifs : refusé, jamais écrit au journal.
    table_state.state = COMMISSIONING_TABLE_STATE_FINAL;
    CHECK(diag_format_table_state_line(out, sizeof(out), &table_state) == 0u);
    table_state.flags = 0u;
    CHECK(diag_format_table_state_line(out, sizeof(out), &table_state) ==
          strlen("[TABLE] loaded state=FINAL flags=0x00 cards=1 keys=3"));

    CHECK(diag_format_map_line(out, sizeof(out), 0u, 0u, 1u, 36u) ==
          strlen("[MAP] pos=0 ch=0 key=1 midi=36 note=DO2(36)"));
    CHECK(strcmp(out, "[MAP] pos=0 ch=0 key=1 midi=36 note=DO2(36)") == 0);
    CHECK(diag_format_map_line(out, sizeof(out), 4u, CAN_IPROPI_CHANNEL_COUNT, 49u, 84u) == 0u);
    CHECK(diag_format_map_line(out, sizeof(out), 4u, 3u, 49u, 35u) == 0u);

    diag_ohm_summary_t ohm_summary = {4u, 3u, 5020u, 6110u, 10u, 3770u, 4550u, false};
    CHECK(diag_format_selftest_ohm_line(out, sizeof(out), &ohm_summary) ==
          strlen("[SELFTEST_OHM] pos=4 n=3 ohm_min=5.02 ohm_max=6.11 spread=10% i100_min=3.77 "
                 "i100_max=4.55"));
    CHECK(strcmp(out, "[SELFTEST_OHM] pos=4 n=3 ohm_min=5.02 ohm_max=6.11 spread=10% "
                      "i100_min=3.77 i100_max=4.55") == 0);
    ohm_summary.warn_i100_over_3a = true;
    CHECK(diag_format_selftest_ohm_line(out, sizeof(out), &ohm_summary) ==
          strlen("[SELFTEST_OHM] pos=4 n=3 ohm_min=5.02 ohm_max=6.11 spread=10% i100_min=3.77 "
                 "i100_max=4.55 warn=i100>3.0A"));
    CHECK(strcmp(out, "[SELFTEST_OHM] pos=4 n=3 ohm_min=5.02 ohm_max=6.11 spread=10% "
                      "i100_min=3.77 i100_max=4.55 warn=i100>3.0A") == 0);
    ohm_summary.count = 0u;
    CHECK(diag_format_selftest_ohm_line(out, sizeof(out), &ohm_summary) == 0u);
}

static void print_example_case_a(void) {
    char line[DIAG_LOG_LINE_MAX];
    diag_card_report_t card = {4u, 6u, {0}, DIAG_CARD_STATE_OK, 0x00Bu,
                               "0,1,3", "1,2,3", "DO2(36),DO#2(37),RE2(38)"};
    diag_summary_report_t summary = {1u, 1u, 3u, 3u, "4-49", "WRITTEN"};

    memcpy(card.uid, UID_A, 8u);

    puts("EXEMPLE_CASE_A_BEGIN");
    (void)diag_format_boot_line(
        line, sizeof(line),
        &(diag_boot_report_t){0u, 1u, 0u, 1u, "2026-10-22 12:00:00", "absent", 0u});
    puts(line);
    (void)diag_format_trigger_line(line, sizeof(line), "bouton", 42u);
    puts(line);
    (void)diag_format_selftest_line(
        line, sizeof(line),
        &(diag_selftest_report_t){42u, 4u, CAN_SELFTEST_STATUS_OK, 0x00Bu, 0x000u, 0x29B1u});
    puts(line);
    (void)diag_format_card_line(line, sizeof(line), &card);
    puts(line);
    (void)diag_format_summary_line(line, sizeof(line), &summary);
    puts(line);
    puts("EXEMPLE_CASE_A_END");
}

static solenoid_profile_t make_profile(void) {
    solenoid_profile_t p = {0};

    p.pwm_freq_hz = 20000u;
    p.impact_duration_us = 800u;
    p.impact_duty_pct = 100u;
    p.impact_max_us = 2000u;
    p.pull_duration_us = 15000u;
    p.pull_duty_pct = 100u;
    p.pull_max_us = 50000u;
    p.hold_duty_pct = 25u;
    p.hold_duty_max_pct = 25u;
    p.hold_max_ms = 20000u;
    p.release_duration_us = 5000u;
    p.release_duty_pct = 50u;
    p.probe_duration_us = 5000u;
    p.probe_duty_pct = 20u;
    p.probe_max_us = 10000u;
    p.probe_duty_max_pct = 20u;
    p.maint_duty_pct = 50u;
    p.maint_duty_max_pct = 100u;
    p.maint_max_ms = 20000u;
    p.maint_min_rest_ms = 2000u;
    p.min_rest_ms = 10u;
    p.dir_safety_delay_us = 200u;
    p.wake_delay_us = 2000u;
    p.dir_level_pull = 0u;
    p.dir_level_release = 1u;
    p.dir_level_maint = 1u;
    p.duty_max_pct = 100u;
    return p;
}

static void test_solenoid_profile_codec(void) {
    const solenoid_profile_t profile = make_profile();
    uint8_t wire[SOLENOID_PROFILE_WIRE_SIZE];
    uint8_t wire2[SOLENOID_PROFILE_WIRE_SIZE];
    solenoid_profile_t decoded = {0};
    solenoid_profile_t bad;

    CHECK(SOLENOID_PROFILE_WIRE_SIZE == 66u);
    CHECK(solenoid_profile_validate(&profile) == SOLENOID_PROFILE_OK);
    CHECK(solenoid_profile_encode(&profile, wire, sizeof(wire)) == SOLENOID_PROFILE_WIRE_SIZE);
    CHECK(wire[0] == SOLENOID_PROFILE_FORMAT_VERSION);
    CHECK(wire[1] == 0x20u && wire[2] == 0x4Eu);  // 20000 = 0x00004E20
    CHECK(solenoid_profile_decode(wire, sizeof(wire), &decoded));
    CHECK(solenoid_profile_encode(&decoded, wire2, sizeof(wire2)) == SOLENOID_PROFILE_WIRE_SIZE);
    CHECK(memcmp(wire, wire2, sizeof(wire)) == 0);
    CHECK(solenoid_profile_tag(&profile) == solenoid_profile_crc32(wire, sizeof(wire)));

    CHECK(!solenoid_profile_decode(wire, sizeof(wire) - 1u, &decoded));
    wire[0] = 99u;  // version inconnue
    CHECK(!solenoid_profile_decode(wire, sizeof(wire), &decoded));
    wire[0] = SOLENOID_PROFILE_FORMAT_VERSION;
    CHECK(solenoid_profile_encode(&profile, wire, 4u) == 0u);

    CHECK(solenoid_profile_validate(NULL) == SOLENOID_PROFILE_ERR_NULL);
    bad = profile;
    bad.pull_duty_pct = 101u;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_DUTY);
    bad = profile;
    bad.hold_duty_pct = 60u;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_DUTY);
    bad = profile;
    bad.pull_duration_us = bad.pull_max_us + 1u;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_CONSISTENCY);
    bad = profile;
    bad.dir_level_pull = bad.dir_level_release;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_DIR);
    bad = profile;
    bad.pwm_freq_hz = 0u;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_PWM);
    bad = profile;
    bad.hold_max_ms = SOLENOID_PROFILE_DURATION_MS_MAX + 1u;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_DURATION);

    // ADR-0019 : contrôles structurels de la phase IMPACT.
    bad = profile;
    bad.impact_duty_pct = 101u;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_DUTY);
    bad = profile;
    bad.impact_duration_us = bad.impact_max_us + 1u;
    CHECK(solenoid_profile_validate(&bad) == SOLENOID_PROFILE_ERR_CONSISTENCY);
}

static void test_solenoid_profile_codecs(void) {
    uint8_t data[8];
    can_profile_begin_t begin = {CAN_PROTO_VERSION, SOLENOID_PROFILE_FORMAT_VERSION, 3u,
                                 CAN_PROFILE_FLAG_GLOBAL, 66u, CAN_PROFILE_CHUNK_SIZE, 0u};
    can_profile_begin_t begin_out = {0};
    can_profile_chunk_t chunk = {3u, 0u, {1u, 2u, 3u, 4u, 5u, 6u}};
    can_profile_chunk_t chunk_out = {0};
    can_profile_commit_t commit = {CAN_PROTO_VERSION, SOLENOID_PROFILE_FORMAT_VERSION, 3u, 0u,
                                   0xDEADBEEFu};
    can_profile_commit_t commit_out = {0};
    can_profile_status_t status = {0x29B1u, 3u, CAN_PROFILE_RESULT_OK, CAN_PROFILE_SOURCE_NVM,
                                   0x11223344u};
    can_profile_status_t status_out = {0};

    CHECK(can_encode_profile_begin(data, &begin) == CAN_PROTO_OK);
    CHECK(data[2] == 3u && data[4] == 66u && data[6] == CAN_PROFILE_CHUNK_SIZE);
    CHECK(can_decode_profile_begin(data, CAN_DLC_PROFILE_BEGIN, &begin_out) == CAN_PROTO_OK);
    CHECK(begin_out.payload_len == 66u && begin_out.transfer_id == 3u);
    CHECK(can_decode_profile_begin(data, 4u, &begin_out) == CAN_PROTO_BAD_DLC);
    begin.chunk_size = 7u;
    CHECK(can_encode_profile_begin(data, &begin) == CAN_PROTO_BAD_VALUE);

    CHECK(can_encode_profile_chunk(data, &chunk) == CAN_PROTO_OK);
    CHECK(can_decode_profile_chunk(data, CAN_DLC_PROFILE_CHUNK, &chunk_out) == CAN_PROTO_OK);
    CHECK(chunk_out.transfer_id == 3u && chunk_out.seq == 0u && chunk_out.data[5] == 6u);

    CHECK(can_encode_profile_commit(data, &commit) == CAN_PROTO_OK);
    CHECK(can_decode_profile_commit(data, CAN_DLC_PROFILE_COMMIT, &commit_out) == CAN_PROTO_OK);
    CHECK(commit_out.crc32 == 0xDEADBEEFu);

    CHECK(can_encode_profile_status(data, &status) == CAN_PROTO_OK);
    CHECK(data[3] == (uint8_t)((uint8_t)CAN_PROFILE_RESULT_OK |
                               ((uint8_t)CAN_PROFILE_SOURCE_NVM << 3u)));
    CHECK(can_decode_profile_status(data, CAN_DLC_PROFILE_STATUS, &status_out) == CAN_PROTO_OK);
    CHECK(status_out.result == CAN_PROFILE_RESULT_OK);
    CHECK(status_out.source == CAN_PROFILE_SOURCE_NVM);
    CHECK(status_out.profile_tag32 == 0x11223344u);

    status.transfer_id = CAN_PROFILE_TRANSFER_ID_BOOT;  // annonce de boot : valide
    CHECK(can_encode_profile_status(data, &status) == CAN_PROTO_OK);
    status.transfer_id = 3u;
    status.result = (can_profile_result_t)9;  // hors énumération
    CHECK(can_encode_profile_status(data, &status) == CAN_PROTO_BAD_VALUE);
}

static bool same_profile(const solenoid_profile_t *a, const solenoid_profile_t *b) {
    return solenoid_profile_tag(a) == solenoid_profile_tag(b);
}

static void test_profile_tuning(void) {
    const solenoid_profile_t base = make_profile();
    solenoid_profile_t p = base;
    profile_tuning_report_t report;
    char text[160];
    size_t index = 0u;

    // Forme `nom valeur` et forme initialiseur C.
    CHECK(profile_tuning_apply("pull_duration_us 12000", &p, &report) == PROFILE_TUNING_OK);
    CHECK(p.pull_duration_us == 12000u && report.assigned == 1u);
    p = base;
    CHECK(profile_tuning_apply(".pull_duration_us = 10000u,", &p, &report) == PROFILE_TUNING_OK);
    CHECK(p.pull_duration_us == 10000u);
    p = base;
    CHECK(profile_tuning_apply("pull_duration_us=9000", &p, &report) == PROFILE_TUNING_OK);
    CHECK(p.pull_duration_us == 9000u);

    // Plusieurs affectations, commentaire final ignoré, casse tolérée, hexadécimal.
    p = base;
    CHECK(profile_tuning_apply(".hold_duty_max_pct = 40u, .HOLD_DUTY_PCT = 0x1E, "
                               "min_rest_ms 12 // essai",
                               &p, &report) == PROFILE_TUNING_OK);
    CHECK(report.assigned == 3u);
    CHECK(p.hold_duty_max_pct == 40u && p.hold_duty_pct == 30u && p.min_rest_ms == 12u);
    CHECK(solenoid_profile_validate(&p) == SOLENOID_PROFILE_OK);
    CHECK(profile_tuning_diff_count(&base, &p) == 3u);
    CHECK(profile_tuning_diff_count(&base, &base) == 0u);

    // Atomicité : une affectation refusée laisse le profil intact.
    p = base;
    CHECK(profile_tuning_apply("pull_duty_pct 60 hold_duty_pct 99", &p, &report) ==
          PROFILE_TUNING_ERR_STRUCTURE);
    CHECK(report.structure == SOLENOID_PROFILE_ERR_DUTY);
    CHECK(same_profile(&p, &base));
    CHECK(profile_tuning_apply("pull_duty_pct 60 inconnu 1", &p, &report) ==
          PROFILE_TUNING_ERR_UNKNOWN_FIELD);
    CHECK(strcmp(report.field, "inconnu") == 0);
    CHECK(same_profile(&p, &base));

    // hold_duty_pct > hold_duty_max_pct : refus structurel, jamais écrêté.
    CHECK(profile_tuning_apply("hold_duty_pct 30", &p, &report) == PROFILE_TUNING_ERR_STRUCTURE);
    CHECK(p.hold_duty_pct == base.hold_duty_pct);
    CHECK(profile_tuning_apply("pull_duration_us 60000", &p, &report) ==
          PROFILE_TUNING_ERR_STRUCTURE);
    CHECK(report.structure == SOLENOID_PROFILE_ERR_CONSISTENCY);
    CHECK(profile_tuning_apply("pull_max_us 80000 pull_duration_us 60000", &p, &report) ==
          PROFILE_TUNING_OK);
    CHECK(p.pull_max_us == 80000u && p.pull_duration_us == 60000u);

    // ADR-0019 : phase IMPACT réglable à chaud, cohérence impact_duration <= impact_max.
    p = base;
    CHECK(profile_tuning_apply("impact_duration_us 1200", &p, &report) == PROFILE_TUNING_OK);
    CHECK(p.impact_duration_us == 1200u && p.impact_max_us == 2000u);
    CHECK(profile_tuning_apply("impact_duration_us 2500", &p, &report) ==
          PROFILE_TUNING_ERR_STRUCTURE);
    CHECK(profile_tuning_apply("impact_max_us 4000 impact_duration_us 3000", &p, &report) ==
          PROFILE_TUNING_OK);
    CHECK(p.impact_duration_us == 3000u && p.impact_max_us == 4000u);
    CHECK(profile_tuning_apply("impact_duration_us 0", &p, &report) == PROFILE_TUNING_OK);
    CHECK(p.impact_duration_us == 0u);
    p = base;

    // Types : u8 refuse 300, nombre invalide, valeur manquante, ligne vide.
    p = base;
    CHECK(profile_tuning_apply("pull_duty_pct 300", &p, &report) == PROFILE_TUNING_ERR_RANGE);
    CHECK(report.range_max == 255u);
    CHECK(profile_tuning_apply("pull_duty_pct 4294967296", &p, &report) ==
          PROFILE_TUNING_ERR_NUMBER);
    CHECK(profile_tuning_apply("pull_duty_pct abc", &p, &report) == PROFILE_TUNING_ERR_NUMBER);
    CHECK(profile_tuning_apply("pull_duty_pct", &p, &report) == PROFILE_TUNING_ERR_NUMBER);
    CHECK(profile_tuning_apply("pull_duty_pct 50x", &p, &report) == PROFILE_TUNING_ERR_NUMBER);
    CHECK(profile_tuning_apply("   ", &p, &report) == PROFILE_TUNING_ERR_EMPTY);
    CHECK(profile_tuning_apply("// rien", &p, &report) == PROFILE_TUNING_ERR_EMPTY);
    CHECK(profile_tuning_apply("= 5", &p, &report) == PROFILE_TUNING_ERR_SYNTAX);
    CHECK(same_profile(&p, &base));

    // Champs verrouillés (raison fournie) et champs demandés par l'utilisateur.
    CHECK(profile_tuning_apply("pwm_freq_hz 25000", &p, &report) == PROFILE_TUNING_ERR_LOCKED);
    CHECK(profile_tuning_apply("dir_level_pull 1", &p, &report) == PROFILE_TUNING_ERR_LOCKED);
    CHECK(profile_tuning_apply("probe_duration_us 100", &p, &report) == PROFILE_TUNING_ERR_LOCKED);
    (void)profile_tuning_describe(PROFILE_TUNING_ERR_LOCKED, &report, text, sizeof(text));
    CHECK(strstr(text, "probe_duration_us") != NULL && strstr(text, "auto-test") != NULL);
    {
        static const char *const wanted[] = {"pull_duration_us", "pull_duty_pct", "pull_max_us",
                                              "hold_duty_pct",    "hold_duty_max_pct",
                                              "hold_max_ms",      "min_rest_ms",
                                              "impact_duration_us", "impact_duty_pct",
                                              "impact_max_us"};

        for (size_t i = 0u; i < sizeof(wanted) / sizeof(wanted[0]); ++i) {
            CHECK(profile_tuning_find(wanted[i], &index));
            CHECK(profile_tuning_field_is_tunable(index));
            CHECK(profile_tuning_field_lock_reason(index) == NULL);
        }
    }

    // Table : un champ par membre de la structure, valeurs lues avec la bonne largeur.
    CHECK(profile_tuning_field_count() == 27u);
    CHECK(profile_tuning_find("dir_safety_delay_us", &index));
    CHECK(profile_tuning_field_value(&base, index) == 200u);
    CHECK(profile_tuning_find("pull_max_us", &index));
    CHECK(profile_tuning_field_value(&base, index) == 50000u);
    CHECK(!profile_tuning_find("nope", &index));

    // Mise en forme prête à recopier dans solenoid_profile_config.h.
    CHECK(profile_tuning_find("pull_duration_us", &index));
    CHECK(profile_tuning_format_field(&base, index, text, sizeof(text)) > 0u);
    CHECK(strcmp(text, ".pull_duration_us = 15000u,") == 0);
    CHECK(profile_tuning_format_field(&base, index, text, 8u) == 0u);

    // Aller-retour : un profil modifié par la console reste encodable et stable.
    {
        uint8_t wire[SOLENOID_PROFILE_WIRE_SIZE];
        solenoid_profile_t decoded = {0};

        p = base;
        CHECK(profile_tuning_apply(".pull_duty_pct = 70u, .hold_max_ms = 5000u,", &p, &report) ==
              PROFILE_TUNING_OK);
        CHECK(solenoid_profile_encode(&p, wire, sizeof(wire)) == SOLENOID_PROFILE_WIRE_SIZE);
        CHECK(solenoid_profile_decode(wire, sizeof(wire), &decoded));
        CHECK(same_profile(&p, &decoded));
        CHECK(solenoid_profile_tag(&p) != solenoid_profile_tag(&base));
    }
}

int main(void) {
    test_profile_tuning();
    test_crc_and_ids();
    test_solenoid_command_codec();
    test_solenoid_profile_codec();
    test_solenoid_profile_codecs();
    test_uid_request_and_announce();
    test_uid_position_table();
    test_commissioning_assignment_examples();
    test_commissioning_totals_and_duplicates();
    test_commissioning_five_boards_total_49();
    test_commissioning_anchor_from_position_zero();
    test_commissioning_flash_crc_and_header();
    test_commissioning_flash_state_and_legacy();
    test_ipropi_ohm_math();
    test_ipropi_ohm_codec();
    test_diag_lines();
    print_example_case_a();

    printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
