// Tests unitaires hôte des helpers purs : can_protocol, commissioning_table,
// uid_position_format et diag_log.
#include <stdio.h>
#include <string.h>

#include "can_protocol.h"
#include "commissioning_table.h"
#include "diag_log.h"
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

    CHECK(commissioning_build_runtime(single_hole, 1u, &runtime) == COMMISSIONING_OK);
    CHECK(runtime.partial);
    CHECK(commissioning_lookup_midi_note(&runtime, 82u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.channel == 0u && target.key == 47u);
    CHECK(commissioning_lookup_midi_note(&runtime, 83u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 1u && target.key == 48u);
    CHECK(commissioning_lookup_midi_note(&runtime, 84u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 3u && target.key == 49u);

    CHECK(commissioning_build_runtime(single_sparse, 1u, &runtime) == COMMISSIONING_OK);
    CHECK(commissioning_lookup_midi_note(&runtime, 82u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 0u);
    CHECK(commissioning_lookup_midi_note(&runtime, 83u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 6u);
    CHECK(commissioning_lookup_midi_note(&runtime, 84u, &target) == COMMISSIONING_OK);
    CHECK(target.channel == 11u);

    CHECK(commissioning_build_runtime(two_boards, 2u, &runtime) == COMMISSIONING_OK);
    CHECK(commissioning_lookup_midi_note(&runtime, 84u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.channel == 3u);
    CHECK(commissioning_lookup_midi_note(&runtime, 81u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 3u && target.channel == 1u && target.key == 46u);
    CHECK(commissioning_lookup_midi_note(&runtime, 80u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 3u && target.channel == 0u && target.key == 45u);
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
    CHECK(commissioning_lookup_midi_note(&runtime, 36u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 0u && target.key == 1u);
    CHECK(commissioning_lookup_midi_note(&runtime, 84u, &target) == COMMISSIONING_OK);
    CHECK(target.position == 4u && target.key == 49u);
}

static void test_commissioning_flash_crc_and_header(void) {
    commissioning_entry_t decoded[UID_POSITION_COUNT];
    uint8_t sector[COMMISSIONING_FLASH_SECTOR_SIZE];
    uint16_t count = 0u;
    commissioning_entry_t entries[] = {
        make_entry(UID_A, 4u, 0x00Bu),
        make_entry(UID_B, 3u, 0x003u),
    };

    CHECK(commissioning_flash_encode(sector, sizeof(sector), entries, 2u) == COMMISSIONING_OK);
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT,
                                     &count) == COMMISSIONING_OK);
    CHECK(count == 2u);
    CHECK(commissioning_entry_equal(&decoded[0], &entries[0]));
    CHECK(commissioning_entry_equal(&decoded[1], &entries[1]));

    sector[12] ^= 0x01u;
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT,
                                     &count) == COMMISSIONING_BAD_CRC);
    sector[12] ^= 0x01u;
    sector[0] = 0u;
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT,
                                     &count) == COMMISSIONING_BAD_MAGIC);
    sector[0] = (uint8_t)'C';
    sector[1] = (uint8_t)'L';
    sector[2] = (uint8_t)'V';
    sector[3] = (uint8_t)'C';
    sector[4] = 0xFFu;
    CHECK(commissioning_flash_decode(sector, sizeof(sector), decoded, UID_POSITION_COUNT,
                                     &count) == COMMISSIONING_BAD_VERSION);
}

static void test_diag_lines(void) {
    char out[DIAG_LOG_LINE_MAX];
    diag_uid_report_t uid = {0u, {0}, 0u, 1u, 0u, 1u, 0x7Fu};
    diag_table_entry_t table = {4u, {0}, 0x00Bu, 47u, 49u, true};
    diag_selftest_report_t report = {42u, 4u, CAN_SELFTEST_STATUS_OK, 0x0A5u, 0x003u, 0xF29Bu};
    diag_selftest_raw_report_t raw = {42u, 4u, 3u, 12u, 345u};
    diag_card_report_t card = {4u, 6u, {0}, DIAG_CARD_STATE_OK, 0x00Bu,
                               "0,1,3", "47,48,49", "LA#4(82),SI4(83),DO5(84)"};
    diag_summary_report_t summary = {1u, 1u, 3u, 3u, "1-46", "WRITTEN"};

    memcpy(uid.uid, UID_A, 8u);
    memcpy(table.uid, UID_A, 8u);
    memcpy(card.uid, UID_A, 8u);
    CHECK(diag_format_uid_line(out, sizeof(out), &uid) ==
          strlen("[UID] pos=127 uid=E660583883123456 fw=0.1.0 proto=1 reply_to=0"));
    CHECK(diag_format_boot_line(out, sizeof(out), "loaded", 2u) ==
          strlen("[BOOT] table=loaded cards=2"));
    CHECK(strcmp(out, "[BOOT] table=loaded cards=2") == 0);
    CHECK(diag_format_help_line(out, sizeof(out)) ==
          strlen("[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash"));
    CHECK(diag_format_trigger_line(out, sizeof(out), 42u) ==
          strlen("[TRIGGER] bouton Moteur recu -> auto-test lance (req=42)"));
    CHECK(strcmp(out, "[TRIGGER] bouton Moteur recu -> auto-test lance (req=42)") == 0);
    CHECK(diag_format_trigger_refused_line(out, sizeof(out), "jeu en cours") ==
          strlen("[TRIGGER] bouton Moteur recu -> refus: jeu en cours"));
    CHECK(strcmp(out, "[TRIGGER] bouton Moteur recu -> refus: jeu en cours") == 0);
    CHECK(diag_format_table_loaded_line(out, sizeof(out), true, "count=2") ==
          strlen("[TABLE] loaded count=2"));
    CHECK(strcmp(out, "[TABLE] loaded count=2") == 0);
    CHECK(diag_format_table_entry_line(out, sizeof(out), &table) ==
          strlen("[TABLE] pos=4 uid=E660583883123456 bitmap=0x00B keys=47-49"));
    CHECK(strcmp(out, "[TABLE] pos=4 uid=E660583883123456 bitmap=0x00B keys=47-49") == 0);
    CHECK(diag_format_diff_line(out, sizeof(out), 4u, 0x003u, 0x00Bu) ==
          strlen("[DIFF] pos=4 ancien=0x003 nouveau=0x00B"));
    CHECK(strcmp(out, "[DIFF] pos=4 ancien=0x003 nouveau=0x00B") == 0);
    CHECK(diag_format_note_name_fr(out, sizeof(out), 82u));
    CHECK(strcmp(out, "LA#4(82)") == 0);
    CHECK(diag_format_card_line(out, sizeof(out), &card) ==
          strlen("[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00B channels=0,1,3 keys=47,48,49 notes=LA#4(82),SI4(83),DO5(84)"));
    CHECK(strcmp(out, "[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00B channels=0,1,3 keys=47,48,49 notes=LA#4(82),SI4(83),DO5(84)") == 0);
    CHECK(diag_format_card_unknown_line(out, sizeof(out), UID_B) ==
          strlen("[CARD_UNKNOWN] uid=AA10203040506070"));
    CHECK(strcmp(out, "[CARD_UNKNOWN] uid=AA10203040506070") == 0);
    CHECK(diag_format_summary_line(out, sizeof(out), &summary) ==
          strlen("[SUMMARY] cards_ok=1/1 channels=3 keys=3/49 missing_keys=1-46 table=WRITTEN"));
    CHECK(strcmp(out, "[SUMMARY] cards_ok=1/1 channels=3 keys=3/49 missing_keys=1-46 table=WRITTEN") == 0);
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
}

static void print_example_case_a(void) {
    char line[DIAG_LOG_LINE_MAX];
    diag_card_report_t card = {4u, 6u, {0}, DIAG_CARD_STATE_OK, 0x00Bu,
                               "0,1,3", "47,48,49", "LA#4(82),SI4(83),DO5(84)"};
    diag_summary_report_t summary = {1u, 1u, 3u, 3u, "1-46", "WRITTEN"};

    memcpy(card.uid, UID_A, 8u);

    puts("EXEMPLE_CASE_A_BEGIN");
    (void)diag_format_boot_line(line, sizeof(line), "absent", 0u);
    puts(line);
    (void)diag_format_trigger_line(line, sizeof(line), 42u);
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

int main(void) {
    test_crc_and_ids();
    test_solenoid_command_codec();
    test_uid_request_and_announce();
    test_uid_position_table();
    test_commissioning_assignment_examples();
    test_commissioning_totals_and_duplicates();
    test_commissioning_five_boards_total_49();
    test_commissioning_flash_crc_and_header();
    test_diag_lines();
    print_example_case_a();

    printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
