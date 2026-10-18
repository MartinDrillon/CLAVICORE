#include "can_link.h"

#include <stdio.h>
#include <string.h>

#include "can2040.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"

#include "can_protocol.h"
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

typedef struct {
    uint32_t notify;
    struct can2040_msg msg;
} rx_entry_t;

typedef enum {
    SLOT_FREE = 0,
    SLOT_PENDING,
    SLOT_POISONED,
} slot_state_t;

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
    uint32_t start_ms;
    bool received_position[UID_POSITION_COUNT];
    uint8_t expected_count;
    uint8_t received_count;
} selftest_session_t;

static struct can2040 cbus;
static rx_entry_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;
static volatile uint32_t isr_rx_overrun;

static reasm_slot_t slots[REASM_SLOTS];
static can_link_stats_t stats;
static bool started;
static bool uid_inflight;
static uint8_t uid_inflight_id;
static uint8_t last_request_id;
static uint32_t uid_inflight_start_ms;
static uint32_t uid_inflight_responses;
static bool tx_unacked;
static bool bad_frame_reported;
static bool overrun_reported;
static uint32_t overrun_snapshot;
static bool note_active;
static uint32_t last_note_event_ms;
static selftest_session_t selftest_session;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void emit_line(const char *line) {
    if (line != NULL && line[0] != '\0') {
        printf("%s\n", line);
    }
}

static void can_rx_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    (void)cd;
    const uint32_t head = rx_head;

    if ((uint32_t)(head - rx_tail) >= RX_RING_SIZE) {
        ++isr_rx_overrun;
        return;
    }
    rx_entry_t *entry = &rx_ring[head & RX_RING_MASK];
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

static bool table_lookup_position(const uint8_t uid[CAN_UID_SIZE], uint8_t *position,
                                  uid_position_lookup_t *lookup_out) {
    clavicore_uid64_t uid64;
    uid_position_lookup_t lookup;

    memcpy(uid64.id, uid, CAN_UID_SIZE);
    lookup = uid_position_lookup(uid_position_table, UID_POSITION_COUNT, &uid64, position);
    if (lookup_out != NULL) {
        *lookup_out = lookup;
    }
    return lookup == UID_POSITION_FOUND;
}

static void emit_uid_report(const uint8_t uid[CAN_UID_SIZE], const can_announce_info_t *info) {
    diag_uid_report_t report;
    uid_position_lookup_t lookup = UID_POSITION_NOT_FOUND;
    uint8_t position = UID_POSITION_UNKNOWN_REPORTED;

    memset(&report, 0, sizeof(report));
    memcpy(report.uid, uid, CAN_UID_SIZE);
    (void)table_lookup_position(uid, &position, &lookup);
    report.position = position;

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

    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_uid_line(line, sizeof(line), &report) == 0u) {
        ++stats.events_dropped;
    } else {
        emit_line(line);
        ++stats.reports_emitted;
    }

    if (uid_inflight && info != NULL && info->reply_to == uid_inflight_id) {
        ++uid_inflight_responses;
    }
    if (info == NULL) {
        emit_status(DIAG_STATUS_INCOMPLETE);
    } else if (info->proto_version != CAN_PROTO_VERSION) {
        emit_status(DIAG_STATUS_PROTO_MISMATCH);
    }
    if (lookup == UID_POSITION_TABLE_INVALID) {
        emit_status(DIAG_STATUS_TABLE_INVALID);
    } else if (lookup == UID_POSITION_NOT_FOUND &&
               uid_position_used_count(uid_position_table, UID_POSITION_COUNT) > 0u) {
        emit_status(DIAG_STATUS_UID_UNKNOWN);
    }
}

static void mark_selftest_received(uint8_t position) {
    if (position >= UID_POSITION_COUNT || selftest_session.received_position[position]) {
        return;
    }
    selftest_session.received_position[position] = true;
    ++selftest_session.received_count;
}

static void emit_selftest_report(const uint8_t uid[CAN_UID_SIZE], const can_announce_status_t *status) {
    uid_position_lookup_t lookup = UID_POSITION_NOT_FOUND;
    uint8_t position = UID_POSITION_UNKNOWN_REPORTED;
    diag_selftest_report_t report;
    char line[DIAG_LOG_LINE_MAX];

    (void)table_lookup_position(uid, &position, &lookup);
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

    if (lookup == UID_POSITION_FOUND) {
        mark_selftest_received(position);
    }
    if (status->test_status == CAN_SELFTEST_STATUS_UNCALIBRATED) {
        emit_status_position(DIAG_STATUS_SELFTEST_UNCALIBRATED, position);
    }
    if (status->test_status == CAN_SELFTEST_STATUS_REFUSED_BUSY) {
        emit_status_bitmap(DIAG_STATUS_BOARD_REFUSED_BUSY, position, status->occupation_bits);
    }
    if (status->fault_bits != 0u) {
        emit_status_bitmap(DIAG_STATUS_FAULT_DURING_TEST, position, status->fault_bits);
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
    slot->state = SLOT_FREE;
}

static void note_bad_frame(void) {
    if ((uid_inflight || selftest_session.active) && !bad_frame_reported) {
        bad_frame_reported = true;
        emit_status(DIAG_STATUS_BAD_FRAME);
    }
}

static void try_emit_slot(reasm_slot_t *slot) {
    const bool is_uid_report =
        slot != NULL && slot->have_info &&
        (slot->info.reply_to == CAN_REPLY_TO_BOOT ||
         (uid_inflight && slot->info.reply_to == uid_inflight_id));

    if (slot == NULL || slot->state != SLOT_PENDING || !slot->have_uid || !slot->have_info) {
        return;
    }

    if (selftest_session.active && slot->have_status &&
        slot->info.reply_to == selftest_session.request_id &&
        slot->status.reply_to == selftest_session.request_id) {
        emit_selftest_report(slot->uid, &slot->status);
        slot_release(slot);
        return;
    }

    if ((!selftest_session.active || slot->info.reply_to != selftest_session.request_id) && is_uid_report) {
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
            emit_status(selftest_session.active ? DIAG_STATUS_DUPLICATE_TAG
                                                : DIAG_STATUS_AMBIGUOUS_TAG);
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

static void sweep_slots(uint32_t now) {
    for (uint8_t i = 0u; i < REASM_SLOTS; ++i) {
        reasm_slot_t *slot = &slots[i];

        if (slot->state == SLOT_FREE || (uint32_t)(now - slot->first_ms) < CAN_ANNOUNCE_PAIR_TIMEOUT_MS) {
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

static bool send_frame(uint32_t id, uint8_t dlc, const uint8_t *data) {
    struct can2040_msg msg;

    memset(&msg, 0, sizeof(msg));
    msg.id = id;
    msg.dlc = dlc;
    memcpy(msg.data, data, dlc);
    tx_unacked = true;
    if (can2040_transmit(&cbus, &msg) != 0) {
        tx_unacked = false;
        ++stats.tx_fail;
        return false;
    }
    return true;
}

static bool recent_playing(void) {
    const uint32_t now = now_ms();
    return note_active ||
           (last_note_event_ms != 0u && (uint32_t)(now - last_note_event_ms) < CAN_LINK_RECENT_PLAY_MS);
}

static bool start_selftest_request(void) {
    uint8_t payload[CAN_DLC_IPROPI_SELFTEST_REQUEST];
    const can_ipropi_selftest_request_t request = {
        .proto_version = CAN_PROTO_VERSION,
        .request_id = can_next_request_id(last_request_id),
    };

    if (can_encode_ipropi_selftest_request(payload, &request) != CAN_PROTO_OK) {
        ++stats.tx_fail;
        return false;
    }
    if (!send_frame(CAN_ID_IPROPI_SELFTEST_REQUEST, CAN_DLC_IPROPI_SELFTEST_REQUEST, payload)) {
        return false;
    }

    memset(&selftest_session, 0, sizeof(selftest_session));
    selftest_session.active = true;
    selftest_session.request_id = request.request_id;
    selftest_session.start_ms = now_ms();
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position != UID_POSITION_EMPTY) {
            ++selftest_session.expected_count;
        }
    }
    last_request_id = request.request_id;
    bad_frame_reported = false;
    overrun_reported = false;
    overrun_snapshot = isr_rx_overrun;
    emit_status_position(DIAG_STATUS_SELFTEST_STARTED, UID_POSITION_UNKNOWN_REPORTED);
    return true;
}

static void finish_selftest_timeout(void) {
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position == UID_POSITION_EMPTY || selftest_session.received_position[i]) {
            continue;
        }
        emit_status_position(DIAG_STATUS_BOARD_MISSING, (uint8_t)i);
    }
    selftest_session.active = false;
}

static void handle_selftest_trigger(uint8_t dlc, const uint8_t *data) {
    can_ipropi_selftest_trigger_t trigger;
    const can_proto_result_t result = can_decode_ipropi_selftest_trigger(data, dlc, &trigger);

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
    if (selftest_session.active || uid_inflight || tx_unacked || recent_playing()) {
        emit_status_position(DIAG_STATUS_SELFTEST_REFUSED_PLAYING, UID_POSITION_UNKNOWN_REPORTED);
        return;
    }
    if (!start_selftest_request()) {
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
    }
}

static void handle_frame(const struct can2040_msg *msg, uint32_t first_ms) {
    if ((msg->id & (CAN2040_ID_EFF | CAN2040_ID_RTR)) != 0u) {
        ++stats.rx_ext_or_rtr;
        return;
    }
    if (msg->dlc > 8u) {
        ++stats.rx_bad_dlc;
        note_bad_frame();
        return;
    }

    const uint8_t dlc = (uint8_t)msg->dlc;
    can_proto_result_t result;

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
    case CAN_ID_IPROPI_SELFTEST_TRIGGER:
        handle_selftest_trigger(dlc, msg->data);
        return;
    case CAN_ID_UID_REQUEST:
    case CAN_ID_IPROPI_SELFTEST_REQUEST:
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
    memset(slots, 0, sizeof(slots));
    memset(&stats, 0, sizeof(stats));
    memset(&selftest_session, 0, sizeof(selftest_session));
    rx_head = 0u;
    rx_tail = 0u;
    isr_rx_overrun = 0u;
    uid_inflight = false;
    tx_unacked = false;
    last_request_id = 0u;
    note_active = false;
    last_note_event_ms = 0u;

    can2040_setup(&cbus, CAN_LINK_PIO_NUM);
    can2040_callback_config(&cbus, can_rx_cb);
    irq_set_exclusive_handler(CAN_LINK_PIO_IRQ, can_pio_irq_handler);
    irq_set_priority(CAN_LINK_PIO_IRQ, CAN_LINK_IRQ_PRIORITY);
    irq_set_enabled(CAN_LINK_PIO_IRQ, true);
    can2040_start(&cbus, clock_get_hz(clk_sys), CAN_BITRATE_HZ, CAN_LINK_GPIO_RX, CAN_LINK_GPIO_TX);
    started = true;
}

void can_link_poll(void) {
    if (!started) {
        return;
    }

    uint32_t tail = rx_tail;
    const uint32_t first_ms = now_ms();
    while (tail != rx_head) {
        __dmb();
        const rx_entry_t entry = rx_ring[tail & RX_RING_MASK];
        ++tail;
        __dmb();
        rx_tail = tail;

        if ((entry.notify & CAN2040_NOTIFY_ERROR) != 0u) {
            ++stats.can_errors;
        }
        if ((entry.notify & CAN2040_NOTIFY_TX) != 0u) {
            tx_unacked = false;
        }
        if ((entry.notify & CAN2040_NOTIFY_RX) != 0u) {
            handle_frame(&entry.msg, first_ms);
        }
    }

    sweep_slots(first_ms);

    if ((uid_inflight || selftest_session.active) && !overrun_reported && isr_rx_overrun != overrun_snapshot) {
        overrun_reported = true;
        emit_status(DIAG_STATUS_RX_OVERRUN);
    }
    if (uid_inflight && (uint32_t)(first_ms - uid_inflight_start_ms) >= CAN_UID_REQUEST_TIMEOUT_MS) {
        if (uid_inflight_responses == 0u) {
            emit_status(DIAG_STATUS_NO_RESPONSE);
        }
        uid_inflight = false;
    }
    if (selftest_session.active &&
        (selftest_session.received_count >= selftest_session.expected_count ||
         (uint32_t)(first_ms - selftest_session.start_ms) >= CAN_LINK_SELFTEST_TIMEOUT_MS)) {
        finish_selftest_timeout();
    }
}

can_link_request_result_t can_link_request_uid(void) {
    uint8_t payload[CAN_DLC_UID_REQUEST];
    const can_uid_request_t request = {
        .proto_version = CAN_PROTO_VERSION,
        .request_id = can_next_request_id(last_request_id),
    };

    if (!started) {
        ++stats.tx_fail;
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    if (uid_inflight || selftest_session.active) {
        emit_status(DIAG_STATUS_BUSY);
        return CAN_LINK_REQUEST_BUSY;
    }
    if (tx_unacked) {
        ++stats.tx_unacked_refused;
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    if (can_encode_uid_request(payload, &request) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_UID_REQUEST, CAN_DLC_UID_REQUEST, payload)) {
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }

    last_request_id = request.request_id;
    uid_inflight = true;
    uid_inflight_id = request.request_id;
    uid_inflight_start_ms = now_ms();
    uid_inflight_responses = 0u;
    bad_frame_reported = false;
    overrun_reported = false;
    overrun_snapshot = isr_rx_overrun;
    return CAN_LINK_REQUEST_SENT;
}

void can_link_set_note_activity(bool any_note_active, uint32_t last_event_ms) {
    note_active = any_note_active;
    last_note_event_ms = last_event_ms;
}

can_link_stats_t can_link_stats(void) {
    can_link_stats_t copy = stats;
    copy.rx_overrun = isr_rx_overrun;
    return copy;
}
