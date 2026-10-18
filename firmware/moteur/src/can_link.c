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

static bool announce_pending;
static announce_kind_t announce_kind;
static uint8_t reply_to;
static uint32_t announce_due_us;
static ipropi_selftest_result_t selftest_result;

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

static void send_announce(void) {
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
            selftest_result.report_ready = false;
        }
    }

    if (ok) {
        ++stats.announces_sent;
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
            if (ipropi_selftest_start(request.request_id)) {
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
    memset(&stats, 0, sizeof(stats));
    rx_head = 0u;
    rx_tail = 0u;
    isr_rx_overrun = 0u;
    announce_pending = false;
    selftest_result = (ipropi_selftest_result_t){0};

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
    (void)ipropi_selftest_take_result(&selftest_result);

    if (announce_pending && announce_kind == ANNOUNCE_KIND_SELFTEST_REPLY && !selftest_result.report_ready) {
        return;
    }
    if (announce_pending && (int32_t)(now_us - announce_due_us) >= 0) {
        announce_pending = false;
        send_announce();
    }
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
