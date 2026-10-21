#include "can_link.h"

#include <string.h>

#include "can2040.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "config.h"
#include "ipropi_selftest.h"

#define RX_RING_SIZE 16u
#define RX_RING_MASK (RX_RING_SIZE - 1u)
#define POLL_MAX_RX_ENTRIES 4u
#define RAW_FRAME_SPACING_US CAN_ANNOUNCE_SLOT_US

typedef struct {
    uint32_t notify;
    struct can2040_msg msg;
} rx_entry_t;

typedef enum {
    ANNOUNCE_KIND_BOOT = 0,
    ANNOUNCE_KIND_UID_REPLY,
    ANNOUNCE_KIND_SELFTEST_REPLY,
} announce_kind_t;

static struct can2040 cbus;
static rx_entry_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;
static volatile uint32_t isr_rx_overrun;

static bool started;
static uint8_t board_uid[CAN_UID_SIZE];
static uint16_t board_tag;
static can_link_stats_t stats;
static solenoid_driver_t *board_drivers;
static uint8_t board_driver_count;
static uint16_t local_occupation_bitmap;

static bool announce_pending;
static announce_kind_t announce_kind;
static uint8_t reply_to;
static uint32_t announce_due_us;
static ipropi_selftest_result_t selftest_result;
static bool raw_reports_pending;
static uint8_t raw_report_index;
static uint32_t raw_report_due_us;
static bool ohm_reports_pending;
static uint8_t ohm_report_index;
static uint32_t ohm_report_due_us;

static void can_rx_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    (void)cd;
    const uint32_t head = rx_head;

    if ((uint32_t)(head - rx_tail) >= RX_RING_SIZE) {
        ++isr_rx_overrun;
        return;
    }
    rx_entry_t *entry = &rx_ring[head & RX_RING_MASK];
    entry->notify = notify;
    if (msg != NULL && (notify & CAN2040_NOTIFY_RX) != 0u) {
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

static void arm_announce(announce_kind_t kind, uint8_t new_reply_to, uint32_t now_us) {
    announce_pending = true;
    announce_kind = kind;
    reply_to = new_reply_to;
    announce_due_us = now_us + can_announce_delay_us(board_tag);
}

static bool send_frame(uint32_t id, uint8_t dlc, const uint8_t *data) {
    struct can2040_msg msg;

    memset(&msg, 0, sizeof(msg));
    msg.id = id;
    msg.dlc = dlc;
    memcpy(msg.data, data, dlc);
    if (can2040_transmit(&cbus, &msg) != 0) {
        ++stats.tx_fail;
        return false;
    }
    return true;
}

static bool channel_is_locally_occupied(uint8_t channel) {
    return channel < CAN_IPROPI_CHANNEL_COUNT &&
           (local_occupation_bitmap & ((uint16_t)1u << channel)) != 0u;
}

static void handle_solenoid_command(const struct can2040_msg *msg, uint32_t now_us) {
    can_solenoid_command_t command;
    const can_proto_result_t result =
        can_decode_solenoid_command(msg->data, (uint8_t)msg->dlc, &command);
    solenoid_result_t driver_result;

    if (result == CAN_PROTO_BAD_DLC) {
        ++stats.rx_bad_dlc;
        return;
    }
    if (result != CAN_PROTO_OK) {
        ++stats.rx_bad_value;
        return;
    }
    if (command.uid_tag != board_tag) {
        ++stats.solenoid_ignored_tag;
        return;
    }
    if (command.channel >= board_driver_count || board_drivers == NULL) {
        ++stats.solenoid_bad_channel;
        return;
    }
    if (ipropi_selftest_is_running()) {
        ++stats.solenoid_selftest_blocked;
        return;
    }
    if (!channel_is_locally_occupied(command.channel)) {
        ++stats.solenoid_unoccupied;
        return;
    }

    if (command.command == CAN_SOLENOID_COMMAND_ON) {
        driver_result =
            solenoid_driver_note_on(&board_drivers[command.channel], command.velocity, now_us);
    } else if (command.command == CAN_SOLENOID_COMMAND_PUSH_ON) {
        driver_result = solenoid_driver_push_start(&board_drivers[command.channel], now_us);
    } else {
        driver_result = solenoid_driver_note_off(&board_drivers[command.channel], now_us);
    }
    if (driver_result == SOLENOID_OK) {
        ++stats.solenoid_commands_ok;
    } else {
        ++stats.solenoid_rejected;
    }
}

// Fin d'une salve de diagnostic en cours : plus rien à émettre pour ce test.
static void finish_diagnostic_reports(void) {
    selftest_result.report_ready = false;
    selftest_result.raw_count = 0u;
    selftest_result.ohm_count = 0u;
}

// Après les IPROPI_RAW, enchaîne les IPROPI_OHM s'il y en a (ADR-0009 §6).
static void start_ohm_reports(uint32_t now_us) {
    if (selftest_result.ohm_count > 0u) {
        ohm_reports_pending = true;
        ohm_report_index = 0u;
        ohm_report_due_us = now_us + RAW_FRAME_SPACING_US;
    } else {
        finish_diagnostic_reports();
    }
}

static void send_announce(uint32_t now_us) {
    uint8_t data[CAN_DLC_ANNOUNCE_STATUS];
    bool ok = true;

    if (can_encode_announce_uid(data, board_uid) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_BOARD_ANNOUNCE_UID, CAN_DLC_ANNOUNCE_UID, data)) {
        ok = false;
    }

    const can_announce_info_t info = {
        .uid_tag = board_tag,
        .proto_version = CAN_PROTO_VERSION,
        .fw_major = MOTEUR_FW_MAJOR,
        .fw_minor = MOTEUR_FW_MINOR,
        .fw_patch = MOTEUR_FW_PATCH,
        .reply_to = reply_to,
    };
    if (can_encode_announce_info(data, &info) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_BOARD_ANNOUNCE_INFO, CAN_DLC_ANNOUNCE_INFO, data)) {
        ok = false;
    }

    if (announce_kind == ANNOUNCE_KIND_SELFTEST_REPLY) {
        if (!selftest_result.report_ready ||
            can_encode_announce_status(data, &selftest_result.report) != CAN_PROTO_OK ||
            !send_frame(CAN_ID_BOARD_ANNOUNCE_STATUS, CAN_DLC_ANNOUNCE_STATUS, data)) {
            ok = false;
        } else {
            if (selftest_result.raw_count > 0u) {
                raw_reports_pending = true;
                raw_report_index = 0u;
                raw_report_due_us = now_us + RAW_FRAME_SPACING_US;
            } else {
                start_ohm_reports(now_us);
            }
        }
    }

    if (ok) {
        ++stats.announces_sent;
    }
}

static void send_raw_report_if_due(uint32_t now_us) {
    uint8_t data[CAN_DLC_IPROPI_RAW];

    if (!raw_reports_pending || (int32_t)(now_us - raw_report_due_us) < 0) {
        return;
    }
    if (raw_report_index >= selftest_result.raw_count) {
        raw_reports_pending = false;
        start_ohm_reports(now_us);
        return;
    }
    if (can_encode_ipropi_raw(data, &selftest_result.raw_reports[raw_report_index]) != CAN_PROTO_OK) {
        raw_reports_pending = false;
        ++stats.tx_fail;
        finish_diagnostic_reports();
        return;
    }
    if (!send_frame(CAN_ID_IPROPI_RAW, CAN_DLC_IPROPI_RAW, data)) {
        ++stats.raw_reports_deferred;
        return;
    }

    ++stats.raw_reports_sent;
    ++raw_report_index;
    raw_report_due_us = now_us + RAW_FRAME_SPACING_US;
    if (raw_report_index >= selftest_result.raw_count) {
        raw_reports_pending = false;
        start_ohm_reports(now_us);
    }
}

// Émission des IPROPI_OHM (0x601, ADR-0009 §6), une par canal, après les RAW.
static void send_ohm_report_if_due(uint32_t now_us) {
    uint8_t data[CAN_DLC_IPROPI_OHM];

    if (!ohm_reports_pending || (int32_t)(now_us - ohm_report_due_us) < 0) {
        return;
    }
    if (ohm_report_index >= selftest_result.ohm_count) {
        ohm_reports_pending = false;
        finish_diagnostic_reports();
        return;
    }
    if (can_encode_ipropi_ohm(data, &selftest_result.ohm_reports[ohm_report_index]) != CAN_PROTO_OK) {
        ohm_reports_pending = false;
        ++stats.tx_fail;
        finish_diagnostic_reports();
        return;
    }
    if (!send_frame(CAN_ID_IPROPI_OHM, CAN_DLC_IPROPI_OHM, data)) {
        ++stats.ohm_reports_deferred;
        return;
    }

    ++stats.ohm_reports_sent;
    ++ohm_report_index;
    ohm_report_due_us = now_us + RAW_FRAME_SPACING_US;
    if (ohm_report_index >= selftest_result.ohm_count) {
        ohm_reports_pending = false;
        finish_diagnostic_reports();
    }
}

static void handle_frame(const struct can2040_msg *msg, uint32_t now_us) {
    if ((msg->id & (CAN2040_ID_EFF | CAN2040_ID_RTR)) != 0u) {
        ++stats.rx_ext_or_rtr;
        return;
    }
    if (msg->dlc > 8u) {
        ++stats.rx_bad_dlc;
        return;
    }

    switch (msg->id) {
    case CAN_ID_SOLENOID_COMMAND:
        handle_solenoid_command(msg, now_us);
        return;
    case CAN_ID_UID_REQUEST: {
        can_uid_request_t request;
        const can_proto_result_t result = can_decode_uid_request(msg->data, (uint8_t)msg->dlc, &request);
        if (result == CAN_PROTO_OK) {
            ++stats.requests_served;
            arm_announce(ANNOUNCE_KIND_UID_REPLY, request.request_id, now_us);
        } else if (result == CAN_PROTO_BAD_DLC) {
            ++stats.rx_bad_dlc;
        } else {
            ++stats.rx_bad_value;
        }
        return;
    }
    case CAN_ID_IPROPI_SELFTEST_REQUEST: {
        can_ipropi_selftest_request_t request;
        const can_proto_result_t result =
            can_decode_ipropi_selftest_request(msg->data, (uint8_t)msg->dlc, &request);
        if (result == CAN_PROTO_OK) {
            ++stats.requests_served;
            if (!announce_pending && !raw_reports_pending && !ohm_reports_pending &&
                !selftest_result.report_ready && ipropi_selftest_start_request(&request)) {
                arm_announce(ANNOUNCE_KIND_SELFTEST_REPLY, request.request_id, now_us);
            } else {
                ++stats.rx_bad_value;
            }
        } else if (result == CAN_PROTO_BAD_DLC) {
            ++stats.rx_bad_dlc;
        } else {
            ++stats.rx_bad_value;
        }
        return;
    }
    case CAN_ID_IPROPI_SELFTEST_TRIGGER:
    case CAN_ID_BOARD_ANNOUNCE_UID:
    case CAN_ID_BOARD_ANNOUNCE_INFO:
    case CAN_ID_BOARD_ANNOUNCE_STATUS:
    case CAN_ID_IPROPI_RAW:
    case CAN_ID_IPROPI_OHM:
        ++stats.rx_ignored;
        return;
    default:
        ++stats.rx_unknown_id;
        return;
    }
}

void can_link_init(const uint8_t uid[BOARD_IDENTITY_UID_SIZE], solenoid_driver_t *drivers,
                   uint8_t driver_count, uint32_t now_us) {
    if (uid == NULL) {
        return;
    }
    memcpy(board_uid, uid, CAN_UID_SIZE);
    board_tag = can_uid_tag(board_uid);
    board_drivers = drivers;
    board_driver_count = driver_count;
    local_occupation_bitmap = driver_count >= CAN_IPROPI_CHANNEL_COUNT
                                  ? CAN_OCCUPATION_BITS_MASK
                                  : (uint16_t)((1u << driver_count) - 1u);
    memset(&stats, 0, sizeof(stats));
    rx_head = 0u;
    rx_tail = 0u;
    isr_rx_overrun = 0u;
    announce_pending = false;
    selftest_result = (ipropi_selftest_result_t){0};
    raw_reports_pending = false;
    raw_report_index = 0u;
    raw_report_due_us = 0u;
    ohm_reports_pending = false;
    ohm_report_index = 0u;
    ohm_report_due_us = 0u;

    ipropi_selftest_init(drivers, driver_count, board_tag);

    can2040_setup(&cbus, MOTEUR_CAN_PIO_NUM);
    can2040_callback_config(&cbus, can_rx_cb);
    irq_set_exclusive_handler(PIO0_IRQ_0, can_pio_irq_handler);
    irq_set_priority(PIO0_IRQ_0, MOTEUR_CAN_IRQ_PRIORITY);
    irq_set_enabled(PIO0_IRQ_0, true);
    can2040_start(&cbus, clock_get_hz(clk_sys), CAN_BITRATE_HZ, MOTEUR_CAN_GPIO_RX,
                  MOTEUR_CAN_GPIO_TX);
    started = true;

    arm_announce(ANNOUNCE_KIND_BOOT, CAN_REPLY_TO_BOOT, now_us);
}

void can_link_poll(uint32_t now_us) {
    if (!started) {
        return;
    }

    uint32_t tail = rx_tail;
    for (uint32_t n = 0u; n < POLL_MAX_RX_ENTRIES && tail != rx_head; ++n) {
        __dmb();
        const rx_entry_t entry = rx_ring[tail & RX_RING_MASK];
        ++tail;
        __dmb();
        rx_tail = tail;

        if ((entry.notify & CAN2040_NOTIFY_ERROR) != 0u) {
            ++stats.can_errors;
        }
        if ((entry.notify & CAN2040_NOTIFY_RX) != 0u) {
            handle_frame(&entry.msg, now_us);
        }
    }

    ipropi_selftest_tick(now_us);
    if (ipropi_selftest_take_result(&selftest_result) &&
        selftest_result.report.test_status == CAN_SELFTEST_STATUS_OK) {
        local_occupation_bitmap = selftest_result.report.occupation_bits;
    }

    if (announce_pending && announce_kind == ANNOUNCE_KIND_SELFTEST_REPLY && !selftest_result.report_ready) {
        return;
    }
    if (announce_pending && (int32_t)(now_us - announce_due_us) >= 0) {
        announce_pending = false;
        send_announce(now_us);
    }
    send_raw_report_if_due(now_us);
    send_ohm_report_if_due(now_us);
}

bool can_link_send_selftest_trigger(void) {
    uint8_t data[CAN_DLC_IPROPI_SELFTEST_TRIGGER];
    const can_ipropi_selftest_trigger_t trigger = {.proto_version = CAN_PROTO_VERSION};

    if (!started || can_encode_ipropi_selftest_trigger(data, &trigger) != CAN_PROTO_OK) {
        ++stats.tx_fail;
        return false;
    }
    if (!send_frame(CAN_ID_IPROPI_SELFTEST_TRIGGER, CAN_DLC_IPROPI_SELFTEST_TRIGGER, data)) {
        return false;
    }
    ++stats.triggers_sent;
    return true;
}

can_link_stats_t can_link_stats(void) {
    can_link_stats_t copy = stats;
    copy.rx_overrun = isr_rx_overrun;
    return copy;
}
