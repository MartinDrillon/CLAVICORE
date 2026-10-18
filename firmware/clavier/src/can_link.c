#include "can_link.h"

#include <stdio.h>
#include <string.h>

#include "can2040.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"

#include "can_protocol.h"
#include "commissioning.h"
#include "uid_position.h"

#define CAN_LINK_PIO_NUM 0u
#define CAN_LINK_GPIO_RX 0u
#define CAN_LINK_GPIO_TX 1u
#define CAN_LINK_PIO_IRQ PIO0_IRQ_0
#define CAN_LINK_IRQ_PRIORITY 0u

#define RX_RING_SIZE 32u
#define RX_RING_MASK (RX_RING_SIZE - 1u)
#define REASM_SLOTS 8u
#define CAN_LINK_SELFTEST_TIMEOUT_MS 500u
#define CAN_LINK_RECENT_PLAY_MS 500u
#define CARD_LIST_TEXT_MAX 128u
#define SUMMARY_TEXT_MAX 160u
#ifndef CLAVIER_SELFTEST_CALIBRATION
#define CLAVIER_SELFTEST_CALIBRATION 0
#endif
#ifndef CLAVIER_NOTE_LOG
#define CLAVIER_NOTE_LOG 0
#endif

typedef enum {
    SLOT_FREE = 0,
    SLOT_PENDING,
    SLOT_POISONED,
} slot_state_t;

typedef enum {
    UID_REQUEST_MODE_NONE = 0,
    UID_REQUEST_MODE_BOOT_SCAN,
    UID_REQUEST_MODE_MANUAL,
} uid_request_mode_t;

typedef struct {
    uint32_t notify;
    struct can2040_msg msg;
} rx_entry_t;

typedef struct {
    slot_state_t state;
    uint16_t tag;
    bool have_uid;
    bool have_info;
    bool have_status;
    uint8_t uid[CAN_UID_SIZE];
    can_announce_info_t info;
    can_announce_status_t status;
    uint32_t first_ms;
} reasm_slot_t;

typedef struct {
    bool active;
    uint8_t request_id;
    bool calibration_mode;
    uint32_t start_ms;
    uint8_t expected_count;
    uint8_t received_count;
    uint8_t raw_expected_count;
    uint8_t raw_received_count;
    bool received_position[UID_POSITION_COUNT];
    bool raw_received_position[UID_POSITION_COUNT][CAN_IPROPI_CHANNEL_COUNT];
    bool known_tag_valid[UID_POSITION_COUNT];
    uint16_t known_tag[UID_POSITION_COUNT];
    bool status_valid[UID_POSITION_COUNT];
    can_announce_status_t status[UID_POSITION_COUNT];
    uint8_t uid[UID_POSITION_COUNT][CAN_UID_SIZE];
    uint8_t unknown_uid_count;
    uint8_t unknown_uid[UID_POSITION_COUNT][CAN_UID_SIZE];
    bool duplicate_tag;
    bool unknown_board;
    bool any_non_ok_status;
} selftest_session_t;

static struct can2040 cbus;
static rx_entry_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;
static volatile uint32_t isr_rx_overrun;

static reasm_slot_t slots[REASM_SLOTS];
static can_link_stats_t stats;
static commissioning_state_t commissioning;
static selftest_session_t selftest_session;
static bool started;
static bool uid_inflight;
static uid_request_mode_t uid_request_mode;
static uint8_t uid_inflight_id;
static uint8_t last_request_id;
static uint32_t uid_inflight_start_ms;
static uint32_t uid_inflight_responses;
static bool control_tx_inflight;
static bool bad_frame_reported;
static bool overrun_reported;
static uint32_t overrun_snapshot;
static bool note_active;
static uint32_t last_note_event_ms;
static bool boot_card_summary_pending;

static void can_rx_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg);
static void can_pio_irq_handler(void);

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void format_uid_hex(char out[17], const uint8_t uid[CAN_UID_SIZE]) {
    static const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0u; i < CAN_UID_SIZE; ++i) {
        out[i * 2u] = hex[(uid[i] >> 4u) & 0x0Fu];
        out[i * 2u + 1u] = hex[uid[i] & 0x0Fu];
    }
    out[16] = '\0';
}

static void emit_line(const char *line) {
    if (line != NULL && line[0] != '\0') {
        printf("%s\n", line);
    }
}

static void emit_status(diag_status_code_t code) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_status_line(line, sizeof(line), code) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_status_position(diag_status_code_t code, uint8_t position) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_status_position_line(line, sizeof(line), code, position) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_status_bitmap(diag_status_code_t code, uint8_t position, uint16_t bitmap) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_status_bitmap_line(line, sizeof(line), code, position, bitmap) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_table_loaded(bool loaded, const char *detail) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_table_loaded_line(line, sizeof(line), loaded, detail) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_table_entry_for_position(uint8_t position) {
    const commissioning_entry_t *entry = commissioning_state_entry_by_position(&commissioning, position);
    diag_table_entry_t line_entry;
    char line[DIAG_LOG_LINE_MAX];

    if (entry == NULL) {
        return;
    }

    memset(&line_entry, 0, sizeof(line_entry));
    line_entry.position = position;
    memcpy(line_entry.uid, entry->identity.uid.id, CAN_UID_SIZE);
    line_entry.bitmap = entry->occupation_bitmap;
    line_entry.has_keys = commissioning.runtime.position_present[position] &&
                          commissioning.runtime.position_first_key[position] != 0u;
    line_entry.first_key = commissioning.runtime.position_first_key[position];
    line_entry.last_key = commissioning.runtime.position_last_key[position];
    if (diag_format_table_entry_line(line, sizeof(line), &line_entry) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_all_table_entries(void) {
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        emit_table_entry_for_position(position);
    }
}

static void emit_boot_line(const char *table_state, uint8_t cards) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_boot_line(line, sizeof(line), table_state, cards) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_help_line(void) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_help_line(line, sizeof(line)) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_trigger_line(uint8_t request_id) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_trigger_line(line, sizeof(line), request_id) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_trigger_refused_line(const char *reason) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_trigger_refused_line(line, sizeof(line), reason) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_diff_line(uint8_t position, uint16_t old_bitmap, uint16_t new_bitmap) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_diff_line(line, sizeof(line), position, old_bitmap, new_bitmap) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_card_unknown_line(const uint8_t uid[CAN_UID_SIZE]) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_card_unknown_line(line, sizeof(line), uid) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_card_line(const diag_card_report_t *report) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_card_line(line, sizeof(line), report) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_summary_line(const diag_summary_report_t *report) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_summary_line(line, sizeof(line), report) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static bool seed_position_is_configured(uint8_t position) {
    return position < UID_POSITION_COUNT && uid_position_table[position].position != UID_POSITION_EMPTY;
}

static uint8_t configured_seed_card_count(void) {
    uint8_t count = 0u;

    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        if (seed_position_is_configured(position)) {
            ++count;
        }
    }
    return count;
}

static bool append_fragment(char *buffer, size_t capacity, size_t *used, const char *fragment) {
    int written;

    if (buffer == NULL || used == NULL || fragment == NULL || *used >= capacity) {
        return false;
    }

    written = snprintf(buffer + *used, capacity - *used, "%s", fragment);
    if (written < 0 || (size_t)written >= capacity - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool append_number(char *buffer, size_t capacity, size_t *used, uint8_t value) {
    char fragment[8];

    snprintf(fragment, sizeof(fragment), "%u", (unsigned)value);
    return append_fragment(buffer, capacity, used, fragment);
}

static bool build_channel_list(uint16_t bitmap, char *buffer, size_t capacity, uint8_t *count_out) {
    size_t used = 0u;
    uint8_t count = 0u;
    bool first = true;

    if (buffer == NULL || capacity == 0u) {
        return false;
    }

    buffer[0] = '\0';
    for (uint8_t channel = 0u; channel < CAN_IPROPI_CHANNEL_COUNT; ++channel) {
        if ((bitmap & ((uint16_t)1u << channel)) == 0u) {
            continue;
        }
        if (!first && !append_fragment(buffer, capacity, &used, ",")) {
            return false;
        }
        if (!append_number(buffer, capacity, &used, channel)) {
            return false;
        }
        first = false;
        ++count;
    }

    if (first && !append_fragment(buffer, capacity, &used, "-")) {
        return false;
    }
    if (count_out != NULL) {
        *count_out = count;
    }
    return true;
}

static bool build_note_key_lists(uint16_t bitmap, uint8_t last_key, char *keys_buffer,
                                 size_t keys_capacity, char *notes_buffer,
                                 size_t notes_capacity) {
    size_t keys_used = 0u;
    size_t notes_used = 0u;
    uint8_t block_size = commissioning_bitmap_popcount(bitmap);
    uint8_t current_key;
    bool first = true;

    if (keys_buffer == NULL || keys_capacity == 0u || notes_buffer == NULL || notes_capacity == 0u) {
        return false;
    }

    keys_buffer[0] = '\0';
    notes_buffer[0] = '\0';
    if (block_size == 0u) {
        return append_fragment(keys_buffer, keys_capacity, &keys_used, "-") &&
               append_fragment(notes_buffer, notes_capacity, &notes_used, "-");
    }
    if (last_key < block_size || last_key > COMMISSIONING_KEY_MAX) {
        return append_fragment(keys_buffer, keys_capacity, &keys_used, "-") &&
               append_fragment(notes_buffer, notes_capacity, &notes_used, "-");
    }

    current_key = (uint8_t)(last_key - block_size + 1u);
    for (uint8_t channel = 0u; channel < CAN_IPROPI_CHANNEL_COUNT; ++channel) {
        char note_name[DIAG_NOTE_NAME_MAX];

        if ((bitmap & ((uint16_t)1u << channel)) == 0u) {
            continue;
        }
        if (!first) {
            if (!append_fragment(keys_buffer, keys_capacity, &keys_used, ",") ||
                !append_fragment(notes_buffer, notes_capacity, &notes_used, ",")) {
                return false;
            }
        }
        if (!append_number(keys_buffer, keys_capacity, &keys_used, current_key)) {
            return false;
        }
        if (!diag_format_note_name_fr(note_name, sizeof(note_name),
                                      commissioning_key_to_midi_note(current_key)) ||
            !append_fragment(notes_buffer, notes_capacity, &notes_used, note_name)) {
            return false;
        }
        first = false;
        ++current_key;
    }
    return true;
}

static bool build_missing_keys_text(const bool assigned[COMMISSIONING_KEY_COUNT], char *buffer,
                                    size_t capacity, uint8_t assigned_count) {
    size_t used = 0u;
    bool first = true;

    if (buffer == NULL || capacity == 0u) {
        return false;
    }

    buffer[0] = '\0';
    if (assigned_count >= COMMISSIONING_KEY_COUNT) {
        return append_fragment(buffer, capacity, &used, "-");
    }

    for (uint8_t index = 0u; index < COMMISSIONING_KEY_COUNT; ++index) {
        if (assigned[index]) {
            continue;
        }

        {
            const uint8_t range_start = (uint8_t)(index + COMMISSIONING_KEY_MIN);
            uint8_t range_end = range_start;
            char fragment[16];

            while ((uint8_t)(range_end - COMMISSIONING_KEY_MIN + 1u) < COMMISSIONING_KEY_COUNT &&
                   !assigned[range_end - COMMISSIONING_KEY_MIN + 1u]) {
                ++range_end;
            }

            if (range_start == range_end) {
                snprintf(fragment, sizeof(fragment), "%u", (unsigned)range_start);
            } else {
                snprintf(fragment, sizeof(fragment), "%u-%u", (unsigned)range_start,
                         (unsigned)range_end);
            }
            if (!first && !append_fragment(buffer, capacity, &used, ",")) {
                return false;
            }
            if (!append_fragment(buffer, capacity, &used, fragment)) {
                return false;
            }
            first = false;
            index = (uint8_t)(range_end - COMMISSIONING_KEY_MIN);
        }
    }
    return true;
}

static void emit_note(uint8_t midi_note, uint8_t key, uint8_t position, uint8_t channel) {
#if CLAVIER_NOTE_LOG
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_note_line(line, sizeof(line), midi_note, key, position, channel) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
#else
    (void)midi_note;
    (void)key;
    (void)position;
    (void)channel;
#endif
}

static void emit_push(uint8_t midi_note, uint8_t key, uint8_t position, uint8_t channel,
                      diag_push_event_t event) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_push_line(line, sizeof(line), midi_note, key, position, channel, event) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_note_dropped(uint8_t midi_note, diag_note_dropped_reason_t reason) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_note_dropped_line(line, sizeof(line), midi_note, reason) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static bool seed_lookup_position(const uint8_t uid[CAN_UID_SIZE], uint8_t *position) {
    clavicore_uid64_t uid64;
    uid_position_lookup_t lookup;

    memcpy(uid64.id, uid, CAN_UID_SIZE);
    lookup = uid_position_lookup(uid_position_table, UID_POSITION_COUNT, &uid64, position);
    return lookup == UID_POSITION_FOUND;
}

static uint8_t position_for_uid_any(const uint8_t uid[CAN_UID_SIZE]) {
    uint8_t position;

    if (commissioning_state_contains_uid(&commissioning, uid, &position)) {
        return position;
    }
    if (seed_lookup_position(uid, &position)) {
        return position;
    }
    return UID_POSITION_UNKNOWN_REPORTED;
}

static void emit_uid_report(const uint8_t uid[CAN_UID_SIZE], const can_announce_info_t *info) {
    diag_uid_report_t report = {0};
    char line[DIAG_LOG_LINE_MAX];

    memcpy(report.uid, uid, CAN_UID_SIZE);
    report.position = position_for_uid_any(uid);
    if (info != NULL) {
        report.reply_to = info->reply_to;
        report.fw_major = info->fw_major;
        report.fw_minor = info->fw_minor;
        report.fw_patch = info->fw_patch;
        report.proto_version = info->proto_version;
    } else {
        report.reply_to = uid_inflight ? uid_inflight_id : CAN_REPLY_TO_BOOT;
        report.fw_major = CAN_FIELD_UNKNOWN;
        report.fw_minor = CAN_FIELD_UNKNOWN;
        report.fw_patch = CAN_FIELD_UNKNOWN;
        report.proto_version = CAN_FIELD_UNKNOWN;
    }

    if (diag_format_uid_line(line, sizeof(line), &report) == 0u) {
        ++stats.events_dropped;
    } else {
        emit_line(line);
        ++stats.reports_emitted;
    }

    if (commissioning.valid && !commissioning_state_mark_uid_online(&commissioning, uid, NULL)) {
        emit_status_position(DIAG_STATUS_UID_NOT_IN_TABLE, UID_POSITION_UNKNOWN_REPORTED);
        ++stats.boot_unknown;
    }
    if (uid_inflight && info != NULL && info->reply_to == uid_inflight_id) {
        ++uid_inflight_responses;
    }
    if (info == NULL) {
        emit_status(DIAG_STATUS_INCOMPLETE);
    } else if (info->proto_version != CAN_PROTO_VERSION) {
        emit_status(DIAG_STATUS_PROTO_MISMATCH);
    }
}

static reasm_slot_t *slot_find(uint16_t tag) {
    for (uint8_t i = 0u; i < REASM_SLOTS; ++i) {
        if (slots[i].state != SLOT_FREE && slots[i].tag == tag) {
            return &slots[i];
        }
    }
    return NULL;
}

static reasm_slot_t *slot_alloc(uint16_t tag, uint32_t first_ms) {
    for (uint8_t i = 0u; i < REASM_SLOTS; ++i) {
        if (slots[i].state == SLOT_FREE) {
            memset(&slots[i], 0, sizeof(slots[i]));
            slots[i].state = SLOT_PENDING;
            slots[i].tag = tag;
            slots[i].first_ms = first_ms;
            return &slots[i];
        }
    }
    ++stats.reasm_full;
    return NULL;
}

static void slot_release(reasm_slot_t *slot) {
    if (slot != NULL) {
        slot->state = SLOT_FREE;
    }
}

static void note_bad_frame(void) {
    if ((uid_inflight || selftest_session.active) && !bad_frame_reported) {
        bad_frame_reported = true;
        emit_status(DIAG_STATUS_BAD_FRAME);
    }
}

static bool send_frame(uint32_t id, uint8_t dlc, const uint8_t *data) {
    struct can2040_msg msg;

    memset(&msg, 0, sizeof(msg));
    msg.id = id;
    msg.dlc = dlc;
    memcpy(msg.data, data, dlc);
    return can2040_transmit(&cbus, &msg) == 0;
}

static bool recent_playing(void) {
    const uint32_t now = now_ms();
    return note_active ||
           (last_note_event_ms != 0u &&
            (uint32_t)(now - last_note_event_ms) < CAN_LINK_RECENT_PLAY_MS);
}

static bool start_uid_request(uid_request_mode_t mode, bool emit_busy_status) {
    uint8_t payload[CAN_DLC_UID_REQUEST];
    const can_uid_request_t request = {
        .proto_version = CAN_PROTO_VERSION,
        .request_id = can_next_request_id(last_request_id),
    };

    if (!started) {
        ++stats.tx_fail;
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return false;
    }
    if (uid_inflight || selftest_session.active) {
        if (emit_busy_status) {
            emit_status(DIAG_STATUS_BUSY);
        }
        return false;
    }
    if (control_tx_inflight) {
        ++stats.tx_unacked_refused;
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return false;
    }
    if (can_encode_uid_request(payload, &request) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_UID_REQUEST, CAN_DLC_UID_REQUEST, payload)) {
        ++stats.tx_fail;
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return false;
    }

    last_request_id = request.request_id;
    uid_inflight = true;
    uid_request_mode = mode;
    uid_inflight_id = request.request_id;
    uid_inflight_start_ms = now_ms();
    uid_inflight_responses = 0u;
    bad_frame_reported = false;
    overrun_reported = false;
    overrun_snapshot = isr_rx_overrun;
    control_tx_inflight = true;
    if (mode == UID_REQUEST_MODE_BOOT_SCAN) {
        commissioning_state_set_all_boards_offline(&commissioning);
    }
    return true;
}

static bool start_selftest_request(void) {
    uint8_t payload[CAN_DLC_IPROPI_SELFTEST_REQUEST];
    const can_ipropi_selftest_request_t request = {
        .proto_version = CAN_PROTO_VERSION,
        .request_id = can_next_request_id(last_request_id),
        .calibration_mode = CLAVIER_SELFTEST_CALIBRATION != 0,
    };

    if (can_encode_ipropi_selftest_request(payload, &request) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_IPROPI_SELFTEST_REQUEST, CAN_DLC_IPROPI_SELFTEST_REQUEST,
                    payload)) {
        ++stats.tx_fail;
        return false;
    }

    memset(&selftest_session, 0, sizeof(selftest_session));
    selftest_session.active = true;
    selftest_session.request_id = request.request_id;
    selftest_session.calibration_mode = request.calibration_mode;
    selftest_session.start_ms = now_ms();
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position != UID_POSITION_EMPTY) {
            ++selftest_session.expected_count;
            if (selftest_session.calibration_mode) {
                selftest_session.raw_expected_count = (uint8_t)(
                    selftest_session.raw_expected_count + CAN_IPROPI_CHANNEL_COUNT);
            }
        }
    }
    last_request_id = request.request_id;
    bad_frame_reported = false;
    overrun_reported = false;
    overrun_snapshot = isr_rx_overrun;
    control_tx_inflight = true;
    emit_status(DIAG_STATUS_SELFTEST_STARTED);
    return true;
}

static void mark_selftest_received(uint8_t position) {
    if (position >= UID_POSITION_COUNT || selftest_session.received_position[position]) {
        return;
    }
    selftest_session.received_position[position] = true;
    ++selftest_session.received_count;
}

static void remember_selftest_tag(uint8_t position, uint16_t uid_tag) {
    if (position >= UID_POSITION_COUNT) {
        return;
    }
    selftest_session.known_tag[position] = uid_tag;
    selftest_session.known_tag_valid[position] = true;
}

static uint8_t selftest_position_for_tag(uint16_t uid_tag) {
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        if (selftest_session.known_tag_valid[position] &&
            selftest_session.known_tag[position] == uid_tag) {
            return position;
        }
    }
    return UID_POSITION_UNKNOWN_REPORTED;
}

static void mark_selftest_raw_received(uint8_t position, uint8_t channel) {
    if (position >= UID_POSITION_COUNT || channel >= CAN_IPROPI_CHANNEL_COUNT ||
        selftest_session.raw_received_position[position][channel]) {
        return;
    }
    selftest_session.raw_received_position[position][channel] = true;
    ++selftest_session.raw_received_count;
}

static void remember_unknown_selftest_uid(const uint8_t uid[CAN_UID_SIZE]) {
    for (uint8_t i = 0u; i < selftest_session.unknown_uid_count; ++i) {
        if (memcmp(selftest_session.unknown_uid[i], uid, CAN_UID_SIZE) == 0) {
            return;
        }
    }
    if (selftest_session.unknown_uid_count >= UID_POSITION_COUNT) {
        return;
    }
    memcpy(selftest_session.unknown_uid[selftest_session.unknown_uid_count], uid, CAN_UID_SIZE);
    ++selftest_session.unknown_uid_count;
}

static void emit_boot_card_summary(void) {
    for (int position = (int)UID_POSITION_COUNT - 1; position >= 0; --position) {
        const commissioning_entry_t *entry;
        diag_card_report_t report;
        char channels_text[CARD_LIST_TEXT_MAX];
        char keys_text[CARD_LIST_TEXT_MAX];
        char notes_text[CARD_LIST_TEXT_MAX];
        uint8_t channel_count = 0u;

        if (!commissioning.valid) {
            return;
        }
        entry = commissioning_state_entry_by_position(&commissioning, (uint8_t)position);
        if (entry == NULL) {
            continue;
        }

        memset(&report, 0, sizeof(report));
        report.position = (uint8_t)position;
        report.node = (uint8_t)(position + 2);
        memcpy(report.uid, entry->identity.uid.id, CAN_UID_SIZE);
        report.state = commissioning_state_is_board_online(&commissioning, (uint8_t)position)
                           ? DIAG_CARD_STATE_OK
                           : DIAG_CARD_STATE_MISSING;
        report.bitmap = entry->occupation_bitmap;
        if (!build_channel_list(entry->occupation_bitmap, channels_text, sizeof(channels_text),
                                &channel_count) ||
            !build_note_key_lists(entry->occupation_bitmap,
                                  commissioning.runtime.position_last_key[position], keys_text,
                                  sizeof(keys_text), notes_text, sizeof(notes_text))) {
            ++stats.events_dropped;
            continue;
        }
        report.channels_text = channels_text;
        report.keys_text = keys_text;
        report.notes_text = notes_text;
        emit_card_line(&report);
    }
}

static void emit_selftest_card_summary_lines(const commissioning_runtime_t *preview,
                                             const char *table_result_text) {
    bool assigned_keys[COMMISSIONING_KEY_COUNT] = {0};
    diag_summary_report_t summary = {0};
    uint8_t keys_ok = 0u;
    char missing_keys_text[SUMMARY_TEXT_MAX];

    for (int position = (int)UID_POSITION_COUNT - 1; position >= 0; --position) {
        diag_card_report_t report;
        char channels_text[CARD_LIST_TEXT_MAX];
        char keys_text[CARD_LIST_TEXT_MAX];
        char notes_text[CARD_LIST_TEXT_MAX];
        uint16_t bitmap = 0u;
        diag_card_state_t state = DIAG_CARD_STATE_MISSING;
        uint8_t last_key = 0u;
        uint8_t channel_count = 0u;

        if (!seed_position_is_configured((uint8_t)position)) {
            continue;
        }

        memset(&report, 0, sizeof(report));
        report.position = (uint8_t)position;
        report.node = (uint8_t)(position + 2);
        memcpy(report.uid, uid_position_table[position].uid.id, CAN_UID_SIZE);

        if (selftest_session.status_valid[position]) {
            bitmap = selftest_session.status[position].occupation_bits;
            state = (selftest_session.status[position].test_status == CAN_SELFTEST_STATUS_OK &&
                     selftest_session.status[position].fault_bits == 0u)
                        ? DIAG_CARD_STATE_OK
                        : DIAG_CARD_STATE_NOT_OK;
            memcpy(report.uid, selftest_session.uid[position], CAN_UID_SIZE);
        }

        report.state = state;
        report.bitmap = bitmap;

        if (state == DIAG_CARD_STATE_MISSING) {
            report.channels_text = "-";
            report.keys_text = "-";
            report.notes_text = "-";
        } else {
            if (preview != NULL && preview->position_present[position]) {
                last_key = preview->position_last_key[position];
            }
            if (!build_channel_list(bitmap, channels_text, sizeof(channels_text), &channel_count) ||
                !build_note_key_lists(bitmap, last_key, keys_text, sizeof(keys_text), notes_text,
                                      sizeof(notes_text))) {
                ++stats.events_dropped;
                continue;
            }
            report.channels_text = channels_text;
            report.keys_text = keys_text;
            report.notes_text = notes_text;
        }
        emit_card_line(&report);

        if (state != DIAG_CARD_STATE_MISSING) {
            summary.channels_ok = (uint8_t)(summary.channels_ok + channel_count);
        }
        if (state == DIAG_CARD_STATE_OK) {
            summary.cards_ok++;
        }
    }

    if (preview != NULL) {
        for (uint8_t key = COMMISSIONING_KEY_MIN; key <= COMMISSIONING_KEY_MAX; ++key) {
            commissioning_note_target_t target;

            if (commissioning_lookup_midi_note(preview, commissioning_key_to_midi_note(key),
                                               &target) == COMMISSIONING_OK) {
                assigned_keys[key - COMMISSIONING_KEY_MIN] = true;
                ++keys_ok;
            }
        }
    }

    for (uint8_t i = 0u; i < selftest_session.unknown_uid_count; ++i) {
        emit_card_unknown_line(selftest_session.unknown_uid[i]);
    }
    if (!build_missing_keys_text(assigned_keys, missing_keys_text, sizeof(missing_keys_text),
                                 keys_ok)) {
        strcpy(missing_keys_text, "?");
    }

    summary.cards_total = configured_seed_card_count();
    summary.keys_ok = keys_ok;
    summary.missing_keys_text = missing_keys_text;
    summary.table_text = table_result_text;
    emit_summary_line(&summary);
}

static void emit_selftest_raw_report(const can_ipropi_raw_t *raw) {
    const uint8_t position = selftest_position_for_tag(raw->uid_tag);
    const diag_selftest_raw_report_t report = {
        .request_id = raw->reply_to,
        .position = position,
        .channel = raw->channel,
        .idle_avg = raw->idle_avg,
        .probe_avg = raw->probe_avg,
    };
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_selftest_raw_line(line, sizeof(line), &report) == 0u) {
        ++stats.events_dropped;
    } else {
        emit_line(line);
        ++stats.reports_emitted;
    }
    if (diag_format_selftest_raw_delta_line(line, sizeof(line), &report) == 0u) {
        ++stats.events_dropped;
    } else {
        emit_line(line);
        ++stats.reports_emitted;
    }
    if (position < UID_POSITION_COUNT) {
        mark_selftest_raw_received(position, raw->channel);
    }
}

static void emit_selftest_report(const uint8_t uid[CAN_UID_SIZE],
                                 const can_announce_status_t *status) {
    diag_selftest_report_t report;
    char line[DIAG_LOG_LINE_MAX];
    uint8_t position = UID_POSITION_UNKNOWN_REPORTED;

    if (!seed_lookup_position(uid, &position)) {
        selftest_session.unknown_board = true;
        remember_unknown_selftest_uid(uid);
        emit_status_position(DIAG_STATUS_UID_NOT_IN_TABLE, UID_POSITION_UNKNOWN_REPORTED);
    } else {
        remember_selftest_tag(position, status->uid_tag);
        mark_selftest_received(position);
        selftest_session.status_valid[position] = true;
        selftest_session.status[position] = *status;
        memcpy(selftest_session.uid[position], uid, CAN_UID_SIZE);
    }

    report.request_id = status->reply_to;
    report.position = position;
    report.test_status = status->test_status;
    report.occupation_bits = status->occupation_bits;
    report.fault_bits = status->fault_bits;
    report.uid_tag = status->uid_tag;
    if (diag_format_selftest_line(line, sizeof(line), &report) == 0u) {
        ++stats.events_dropped;
    } else {
        emit_line(line);
        ++stats.reports_emitted;
    }

    if (status->test_status == CAN_SELFTEST_STATUS_UNCALIBRATED) {
        emit_status_position(DIAG_STATUS_SELFTEST_UNCALIBRATED, position);
    }
    if (status->test_status == CAN_SELFTEST_STATUS_REFUSED_BUSY) {
        emit_status_bitmap(DIAG_STATUS_BOARD_REFUSED_BUSY, position,
                           status->occupation_bits);
    }
    if (status->fault_bits != 0u) {
        emit_status_bitmap(DIAG_STATUS_FAULT_DURING_TEST, position, status->fault_bits);
    }
    if (status->test_status != CAN_SELFTEST_STATUS_OK) {
        selftest_session.any_non_ok_status = true;
    }
}

static void log_table_diff(const commissioning_entry_t *old_entry,
                           const commissioning_entry_t *new_entry, uint8_t position) {
    char old_uid[17];
    char new_uid[17];

    strcpy(old_uid, "none");
    strcpy(new_uid, "none");
    if (old_entry != NULL) {
        format_uid_hex(old_uid, old_entry->identity.uid.id);
    }
    if (new_entry != NULL) {
        format_uid_hex(new_uid, new_entry->identity.uid.id);
    }
    printf("[TABLE] diff pos=%u old_uid=%s new_uid=%s old=0x%03X new=0x%03X\n",
           (unsigned)position, old_uid, new_uid,
           (unsigned)(old_entry != NULL ? old_entry->occupation_bitmap : 0u),
           (unsigned)(new_entry != NULL ? new_entry->occupation_bitmap : 0u));
    emit_diff_line(position, old_entry != NULL ? old_entry->occupation_bitmap : 0u,
                   new_entry != NULL ? new_entry->occupation_bitmap : 0u);
}

static void finish_selftest_session(void) {
    commissioning_entry_t new_entries[UID_POSITION_COUNT];
    commissioning_runtime_t preview;
    uint16_t new_count = 0u;
    bool can_write = true;
    bool differs = false;
    bool has_preview = false;
    commissioning_result_t build_result = COMMISSIONING_OK;
    const char *table_result_text = "NOT_WRITTEN(?)";

    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        if (uid_position_table[position].position == UID_POSITION_EMPTY) {
            continue;
        }
        if (!selftest_session.received_position[position]) {
            emit_status_position(DIAG_STATUS_BOARD_MISSING, position);
            ++stats.boot_missing;
            can_write = false;
        }
    }

    if (selftest_session.unknown_board || selftest_session.duplicate_tag ||
        selftest_session.any_non_ok_status) {
        can_write = false;
    }
    if (selftest_session.calibration_mode &&
        selftest_session.raw_received_count < selftest_session.raw_expected_count) {
        can_write = false;
    }

    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        if (uid_position_table[position].position == UID_POSITION_EMPTY) {
            continue;
        }
        new_entries[new_count].identity.position = position;
        memcpy(new_entries[new_count].identity.uid.id, selftest_session.uid[position],
               CAN_UID_SIZE);
        new_entries[new_count].occupation_bitmap =
            selftest_session.status[position].occupation_bits;
        new_entries[new_count].reserved = 0u;
        ++new_count;
    }

    if (new_count > 0u) {
        build_result = commissioning_build_runtime(new_entries, new_count, &preview);
        has_preview = build_result == COMMISSIONING_OK;
        if (build_result == COMMISSIONING_DUPLICATE_TAG) {
            emit_status(DIAG_STATUS_DUPLICATE_TAG);
            can_write = false;
        } else if (build_result != COMMISSIONING_OK) {
            emit_status(DIAG_STATUS_TABLE_ERROR_FINAL);
            can_write = false;
        } else if (preview.partial) {
            emit_status(DIAG_STATUS_TABLE_PARTIAL);
        }
    }

    if (commissioning.valid) {
        if (commissioning.entry_count != new_count) {
            differs = true;
        }
        for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
            const commissioning_entry_t *old_entry =
                commissioning_state_entry_by_position(&commissioning, position);
            const commissioning_entry_t *new_entry =
                commissioning_find_entry_by_position(new_entries, new_count, position);
            if ((old_entry == NULL) != (new_entry == NULL) ||
                (old_entry != NULL && new_entry != NULL &&
                 !commissioning_entry_equal(old_entry, new_entry))) {
                differs = true;
                log_table_diff(old_entry, new_entry, position);
            }
        }
    }
    if (differs) {
        emit_status(DIAG_STATUS_TABLE_DIFFERS);
    }

    if (!can_write || selftest_session.received_count < selftest_session.expected_count) {
        emit_status(DIAG_STATUS_TABLE_NOT_WRITTEN_INCOMPLETE);
        if (selftest_session.unknown_board) {
            table_result_text = "NOT_WRITTEN(uid_inconnu)";
        } else if (selftest_session.duplicate_tag) {
            table_result_text = "NOT_WRITTEN(tag_duplique)";
        } else if (selftest_session.any_non_ok_status) {
            table_result_text = "NOT_WRITTEN(carte_not_ok)";
        } else if (selftest_session.calibration_mode &&
                   selftest_session.raw_received_count < selftest_session.raw_expected_count) {
            table_result_text = "NOT_WRITTEN(calibration_incomplete)";
        } else if (build_result != COMMISSIONING_OK) {
            table_result_text = "NOT_WRITTEN(erreur_table)";
        } else {
            table_result_text = "NOT_WRITTEN(incomplete)";
        }
        emit_selftest_card_summary_lines(has_preview ? &preview : NULL, table_result_text);
        selftest_session.active = false;
        return;
    }

    if (differs) {
        table_result_text = "DIFFERS_WRITTEN";
    } else if (commissioning.valid && commissioning.entry_count == new_count) {
        table_result_text = "UNCHANGED";
    } else {
        table_result_text = "WRITTEN";
    }

    if (strcmp(table_result_text, "UNCHANGED") != 0) {
        if (commissioning_state_store(&commissioning, new_entries, new_count) != COMMISSIONING_OK) {
            emit_status(DIAG_STATUS_TABLE_ERROR_FINAL);
            emit_selftest_card_summary_lines(&preview, "NOT_WRITTEN(flash_error)");
            selftest_session.active = false;
            return;
        }
        emit_status(DIAG_STATUS_TABLE_WRITTEN);
    }

    commissioning_state_set_all_boards_offline(&commissioning);
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        if (selftest_session.received_position[position]) {
            commissioning.board_online[position] = true;
        }
    }
    emit_table_loaded(true, strcmp(table_result_text, "UNCHANGED") == 0 ? "count=unchanged"
                                                                         : "count=updated");
    emit_all_table_entries();
    emit_selftest_card_summary_lines(&preview, table_result_text);
    selftest_session.active = false;
}

static void try_emit_slot(reasm_slot_t *slot) {
    const bool is_uid_report =
        slot != NULL && slot->have_info &&
        (slot->info.reply_to == CAN_REPLY_TO_BOOT ||
         (uid_inflight && slot->info.reply_to == uid_inflight_id));

    if (slot == NULL || slot->state != SLOT_PENDING || !slot->have_uid ||
        !slot->have_info) {
        return;
    }

    if (selftest_session.active && slot->have_status &&
        slot->info.reply_to == selftest_session.request_id &&
        slot->status.reply_to == selftest_session.request_id) {
        emit_selftest_report(slot->uid, &slot->status);
        slot_release(slot);
        return;
    }

    if ((!selftest_session.active || slot->info.reply_to != selftest_session.request_id) &&
        is_uid_report) {
        emit_uid_report(slot->uid, &slot->info);
        slot_release(slot);
    }
}

static void on_announce_uid(const uint8_t uid[CAN_UID_SIZE], uint32_t first_ms) {
    const uint16_t tag = can_uid_tag(uid);
    reasm_slot_t *slot = slot_find(tag);

    if (slot == NULL) {
        slot = slot_alloc(tag, first_ms);
        if (slot == NULL) {
            return;
        }
    } else if (slot->state == SLOT_POISONED) {
        return;
    } else if (slot->have_uid) {
        if (memcmp(slot->uid, uid, CAN_UID_SIZE) != 0) {
            slot->state = SLOT_POISONED;
            if (selftest_session.active) {
                selftest_session.duplicate_tag = true;
                emit_status(DIAG_STATUS_DUPLICATE_TAG);
            } else {
                emit_status(DIAG_STATUS_AMBIGUOUS_TAG);
            }
        }
        return;
    }

    memcpy(slot->uid, uid, CAN_UID_SIZE);
    slot->have_uid = true;
    try_emit_slot(slot);
}

static void on_announce_info(const can_announce_info_t *info, uint32_t first_ms) {
    reasm_slot_t *slot = slot_find(info->uid_tag);

    if (slot == NULL) {
        slot = slot_alloc(info->uid_tag, first_ms);
        if (slot == NULL) {
            return;
        }
    } else if (slot->state == SLOT_POISONED || slot->have_info) {
        return;
    }

    slot->info = *info;
    slot->have_info = true;
    try_emit_slot(slot);
}

static void on_announce_status(const can_announce_status_t *status, uint32_t first_ms) {
    reasm_slot_t *slot = slot_find(status->uid_tag);

    if (slot == NULL) {
        slot = slot_alloc(status->uid_tag, first_ms);
        if (slot == NULL) {
            return;
        }
    } else if (slot->state == SLOT_POISONED || slot->have_status) {
        return;
    }

    slot->status = *status;
    slot->have_status = true;
    try_emit_slot(slot);
}

static void on_ipropi_raw(const can_ipropi_raw_t *raw) {
    if (!selftest_session.active || raw->reply_to != selftest_session.request_id ||
        !selftest_session.calibration_mode) {
        return;
    }
    emit_selftest_raw_report(raw);
}

static void sweep_slots(uint32_t now) {
    for (uint8_t i = 0u; i < REASM_SLOTS; ++i) {
        reasm_slot_t *slot = &slots[i];

        if (slot->state == SLOT_FREE ||
            (uint32_t)(now - slot->first_ms) < CAN_ANNOUNCE_PAIR_TIMEOUT_MS) {
            continue;
        }
        if (slot->state == SLOT_PENDING) {
            if (selftest_session.active && slot->have_info &&
                slot->info.reply_to == selftest_session.request_id) {
                slot_release(slot);
                continue;
            }
            if (slot->have_info && slot->info.reply_to != CAN_REPLY_TO_BOOT &&
                (!uid_inflight || slot->info.reply_to != uid_inflight_id)) {
                slot_release(slot);
                continue;
            }
            if (slot->have_uid && !slot->have_info) {
                emit_uid_report(slot->uid, NULL);
            } else if (!slot->have_uid && slot->have_info) {
                emit_status(DIAG_STATUS_INCOMPLETE);
            }
        }
        slot_release(slot);
    }
}

static void handle_selftest_trigger(uint8_t dlc, const uint8_t *data) {
    can_ipropi_selftest_trigger_t trigger;
    const can_proto_result_t result =
        can_decode_ipropi_selftest_trigger(data, dlc, &trigger);

    if (result == CAN_PROTO_BAD_DLC) {
        ++stats.rx_bad_dlc;
        note_bad_frame();
        return;
    }
    if (result != CAN_PROTO_OK) {
        ++stats.rx_bad_value;
        note_bad_frame();
        return;
    }
    if (selftest_session.active) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        emit_trigger_refused_line("auto-test deja en cours");
        return;
    }
    if (uid_inflight || control_tx_inflight) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        emit_trigger_refused_line("requete de controle deja en cours");
        return;
    }
    if (recent_playing()) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        emit_trigger_refused_line("jeu en cours");
        return;
    }
    if (!start_selftest_request()) {
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        emit_trigger_refused_line("echec emission requete CAN");
        return;
    }
    emit_trigger_line(selftest_session.request_id);
}

static void handle_frame(const struct can2040_msg *msg, uint32_t first_ms) {
    can_proto_result_t result;
    const uint8_t dlc = (uint8_t)msg->dlc;

    if ((msg->id & (CAN2040_ID_EFF | CAN2040_ID_RTR)) != 0u) {
        ++stats.rx_ext_or_rtr;
        return;
    }
    if (msg->dlc > 8u) {
        ++stats.rx_bad_dlc;
        note_bad_frame();
        return;
    }

    switch (msg->id) {
    case CAN_ID_BOARD_ANNOUNCE_UID: {
        uint8_t uid[CAN_UID_SIZE];
        result = can_decode_announce_uid(msg->data, dlc, uid);
        if (result == CAN_PROTO_OK) {
            on_announce_uid(uid, first_ms);
            return;
        }
        break;
    }
    case CAN_ID_BOARD_ANNOUNCE_INFO: {
        can_announce_info_t info;
        result = can_decode_announce_info(msg->data, dlc, &info);
        if (result == CAN_PROTO_OK) {
            on_announce_info(&info, first_ms);
            return;
        }
        break;
    }
    case CAN_ID_BOARD_ANNOUNCE_STATUS: {
        can_announce_status_t status;
        result = can_decode_announce_status(msg->data, dlc, &status);
        if (result == CAN_PROTO_OK) {
            on_announce_status(&status, first_ms);
            return;
        }
        break;
    }
    case CAN_ID_IPROPI_RAW: {
        can_ipropi_raw_t raw;
        result = can_decode_ipropi_raw(msg->data, dlc, &raw);
        if (result == CAN_PROTO_OK) {
            on_ipropi_raw(&raw);
            return;
        }
        break;
    }
    case CAN_ID_IPROPI_SELFTEST_TRIGGER:
        handle_selftest_trigger(dlc, msg->data);
        return;
    case CAN_ID_UID_REQUEST:
    case CAN_ID_IPROPI_SELFTEST_REQUEST:
    case CAN_ID_SOLENOID_COMMAND:
        ++stats.rx_ignored;
        return;
    default:
        ++stats.rx_unknown_id;
        return;
    }

    if (result == CAN_PROTO_BAD_DLC) {
        ++stats.rx_bad_dlc;
    } else {
        ++stats.rx_bad_value;
    }
    note_bad_frame();
}

void can_link_init(void) {
    commissioning_result_t load_result;
    bool table_blank;
    char detail[32];

    memset(slots, 0, sizeof(slots));
    memset(&stats, 0, sizeof(stats));
    commissioning_state_init(&commissioning);
    memset(&selftest_session, 0, sizeof(selftest_session));
    rx_head = 0u;
    rx_tail = 0u;
    isr_rx_overrun = 0u;
    uid_inflight = false;
    uid_request_mode = UID_REQUEST_MODE_NONE;
    control_tx_inflight = false;
    last_request_id = 0u;
    note_active = false;
    last_note_event_ms = 0u;
    boot_card_summary_pending = false;

    table_blank = commissioning_flash_table_is_blank();
    load_result = commissioning_state_load(&commissioning);
    if (load_result == COMMISSIONING_OK) {
        emit_boot_line("loaded", (uint8_t)commissioning.entry_count);
        snprintf(detail, sizeof(detail), "count=%u",
                 (unsigned)commissioning.entry_count);
        emit_table_loaded(true, detail);
        emit_all_table_entries();
        boot_card_summary_pending = true;
        if (commissioning.runtime.partial) {
            emit_status(DIAG_STATUS_TABLE_PARTIAL);
        }
    } else {
        emit_boot_line(table_blank ? "absent" : "invalid", 0u);
        snprintf(detail, sizeof(detail), "reason=%s",
                 commissioning_result_name(load_result));
        emit_table_loaded(false, detail);
        emit_status(DIAG_STATUS_FLASH_TABLE_INVALID);
    }
    emit_help_line();

    can2040_setup(&cbus, CAN_LINK_PIO_NUM);
    can2040_callback_config(&cbus, can_rx_cb);
    irq_set_exclusive_handler(CAN_LINK_PIO_IRQ, can_pio_irq_handler);
    irq_set_priority(CAN_LINK_PIO_IRQ, CAN_LINK_IRQ_PRIORITY);
    irq_set_enabled(CAN_LINK_PIO_IRQ, true);
    can2040_start(&cbus, clock_get_hz(clk_sys), CAN_BITRATE_HZ, CAN_LINK_GPIO_RX,
                  CAN_LINK_GPIO_TX);
    started = true;

    (void)start_uid_request(UID_REQUEST_MODE_BOOT_SCAN, false);
}

void can_link_poll(void) {
    uint32_t tail;
    const uint32_t first_ms = now_ms();

    if (!started) {
        return;
    }

    tail = rx_tail;
    while (tail != rx_head) {
        const rx_entry_t entry = rx_ring[tail & RX_RING_MASK];

        ++tail;
        rx_tail = tail;
        if ((entry.notify & CAN2040_NOTIFY_ERROR) != 0u) {
            ++stats.can_errors;
        }
        if ((entry.notify & CAN2040_NOTIFY_TX) != 0u) {
            control_tx_inflight = false;
        }
        if ((entry.notify & CAN2040_NOTIFY_RX) != 0u) {
            handle_frame(&entry.msg, first_ms);
        }
    }

    sweep_slots(first_ms);

    if ((uid_inflight || selftest_session.active) && !overrun_reported &&
        isr_rx_overrun != overrun_snapshot) {
        overrun_reported = true;
        emit_status(DIAG_STATUS_RX_OVERRUN);
    }

    if (uid_inflight &&
        (uint32_t)(first_ms - uid_inflight_start_ms) >= CAN_UID_REQUEST_TIMEOUT_MS) {
        if (uid_request_mode == UID_REQUEST_MODE_BOOT_SCAN && commissioning.valid) {
            for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
                if (commissioning.runtime.position_present[position] &&
                    !commissioning_state_is_board_online(&commissioning, position)) {
                    emit_status_position(DIAG_STATUS_BOARD_MISSING, position);
                    ++stats.boot_missing;
                }
            }
            if (boot_card_summary_pending) {
                emit_boot_card_summary();
                boot_card_summary_pending = false;
            }
        } else if (uid_inflight_responses == 0u) {
            emit_status(DIAG_STATUS_NO_RESPONSE);
        }
        uid_inflight = false;
        uid_request_mode = UID_REQUEST_MODE_NONE;
    }

    if (selftest_session.active &&
        ((selftest_session.received_count >= selftest_session.expected_count &&
          (!selftest_session.calibration_mode ||
           selftest_session.raw_received_count >=
               selftest_session.raw_expected_count)) ||
         (uint32_t)(first_ms - selftest_session.start_ms) >=
             CAN_LINK_SELFTEST_TIMEOUT_MS)) {
        finish_selftest_session();
    }
}

can_link_request_result_t can_link_request_uid(void) {
    if (!started) {
        ++stats.tx_fail;
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    if (uid_inflight || selftest_session.active) {
        emit_status(DIAG_STATUS_BUSY);
        return CAN_LINK_REQUEST_BUSY;
    }
    if (control_tx_inflight) {
        ++stats.tx_unacked_refused;
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    if (start_uid_request(UID_REQUEST_MODE_MANUAL, false)) {
        return CAN_LINK_REQUEST_SENT;
    }
    return CAN_LINK_REQUEST_TX_FAIL;
}

static bool send_note_command(uint8_t midi_note, uint8_t velocity,
                              can_solenoid_command_kind_t kind,
                              const diag_push_event_t *push_event,
                              diag_note_dropped_reason_t *drop_reason) {
    commissioning_note_target_t target;
    can_solenoid_command_t command;
    uint8_t payload[CAN_DLC_SOLENOID_COMMAND];

    if (drop_reason == NULL) {
        return false;
    }
    if (selftest_session.active) {
        *drop_reason = DIAG_NOTE_DROPPED_SELFTEST_ACTIVE;
        return false;
    }
    if (!commissioning.valid) {
        *drop_reason = DIAG_NOTE_DROPPED_TABLE_INVALID;
        return false;
    }
    if (commissioning_lookup_midi_note(&commissioning.runtime, midi_note, &target) !=
        COMMISSIONING_OK) {
        *drop_reason = commissioning_midi_note_to_key(midi_note, &target.key)
                           ? DIAG_NOTE_DROPPED_NOTE_UNASSIGNED
                           : DIAG_NOTE_DROPPED_OUT_OF_RANGE;
        return false;
    }
    if (!commissioning_state_is_board_online(&commissioning, target.position)) {
        *drop_reason = DIAG_NOTE_DROPPED_BOARD_OFFLINE;
        return false;
    }

    command.uid_tag = target.uid_tag;
    command.channel = target.channel;
    command.command = kind;
    command.velocity = velocity;
    if (can_encode_solenoid_command(payload, &command) != CAN_PROTO_OK) {
        *drop_reason = DIAG_NOTE_DROPPED_PROTOCOL;
        return false;
    }
    if (!send_frame(CAN_ID_SOLENOID_COMMAND, CAN_DLC_SOLENOID_COMMAND, payload)) {
        *drop_reason = DIAG_NOTE_DROPPED_TX_FULL;
        return false;
    }

    if (push_event != NULL) {
        emit_push(midi_note, target.key, target.position, target.channel, *push_event);
    } else {
        emit_note(midi_note, target.key, target.position, target.channel);
    }
    ++stats.note_sent;
    return true;
}

bool can_link_has_valid_table(void) {
    return commissioning.valid;
}

bool can_link_selftest_active(void) {
    return selftest_session.active;
}

bool can_link_note_on(uint8_t midi_note, uint8_t velocity) {
    diag_note_dropped_reason_t reason;

    if (send_note_command(midi_note, velocity, CAN_SOLENOID_COMMAND_ON, NULL, &reason)) {
        return true;
    }
    ++stats.note_dropped;
    if (reason == DIAG_NOTE_DROPPED_TX_FULL) {
        ++stats.note_tx_fail;
    }
    emit_note_dropped(midi_note, reason);
    return false;
}

bool can_link_note_off(uint8_t midi_note) {
    diag_note_dropped_reason_t reason;

    if (send_note_command(midi_note, 0u, CAN_SOLENOID_COMMAND_OFF, NULL, &reason)) {
        return true;
    }
    ++stats.note_dropped;
    if (reason == DIAG_NOTE_DROPPED_TX_FULL) {
        ++stats.note_tx_fail;
    }
    emit_note_dropped(midi_note, reason);
    return false;
}

bool can_link_maintenance_push_start(uint8_t midi_note) {
    static const diag_push_event_t event = DIAG_PUSH_EVENT_START;
    diag_note_dropped_reason_t reason;

    if (send_note_command(midi_note, 0u, CAN_SOLENOID_COMMAND_PUSH_ON, &event, &reason)) {
        return true;
    }
    ++stats.note_dropped;
    if (reason == DIAG_NOTE_DROPPED_TX_FULL) {
        ++stats.note_tx_fail;
    }
    emit_note_dropped(midi_note, reason);
    return false;
}

bool can_link_maintenance_push_stop(uint8_t midi_note, bool timed_out) {
    const diag_push_event_t event = timed_out ? DIAG_PUSH_EVENT_TIMEOUT : DIAG_PUSH_EVENT_STOP;
    diag_note_dropped_reason_t reason;

    if (send_note_command(midi_note, 0u, CAN_SOLENOID_COMMAND_OFF, &event, &reason)) {
        return true;
    }
    ++stats.note_dropped;
    if (reason == DIAG_NOTE_DROPPED_TX_FULL) {
        ++stats.note_tx_fail;
    }
    emit_note_dropped(midi_note, reason);
    return false;
}

void can_link_set_note_activity(bool any_note_active, uint32_t last_note_event_ms_value) {
    note_active = any_note_active;
    last_note_event_ms = last_note_event_ms_value;
}

can_link_stats_t can_link_stats(void) {
    can_link_stats_t copy = stats;

    copy.rx_overrun = isr_rx_overrun;
    return copy;
}

static void can_rx_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    const uint32_t head = rx_head;
    rx_entry_t *entry;

    (void)cd;
    if ((uint32_t)(head - rx_tail) >= RX_RING_SIZE) {
        ++isr_rx_overrun;
        return;
    }
    entry = &rx_ring[head & RX_RING_MASK];
    entry->notify = notify;
    if (msg != NULL && (notify & (CAN2040_NOTIFY_RX | CAN2040_NOTIFY_TX)) != 0u) {
        entry->msg = *msg;
    } else {
        memset(&entry->msg, 0, sizeof(entry->msg));
    }
    __dmb();
    rx_head = head + 1u;
}

static void can_pio_irq_handler(void) {
    can2040_pio_irq_handler(&cbus);
}
