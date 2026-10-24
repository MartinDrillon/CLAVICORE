#include "can_link.h"

#ifndef MOTEUR_DIAG_TRACE
#define MOTEUR_DIAG_TRACE 1
#endif

#include <string.h>

#include "can2040.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "config.h"
#include "ipropi_selftest.h"
#include "motor_counter_map.h"
#include "solenoid_profile.h"
#include "solenoid_profile_store.h"

#define RX_RING_SIZE 16u
#define RX_RING_MASK (RX_RING_SIZE - 1u)
#define POLL_MAX_RX_ENTRIES 4u
#define RAW_FRAME_SPACING_US CAN_ANNOUNCE_SLOT_US
#define PROFILE_STAGING_TIMEOUT_US 500000u
#define PROFILE_STATUS_SPACING_US CAN_ANNOUNCE_SLOT_US

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

// Réception du profil d'activation (ADR-0013 §3-§4). Le transfert est diffusé :
// chaque carte le reçoit et l'acquitte par un PROFILE_STATUS.
static bool profile_staging_active;
static uint8_t profile_transfer_id;
static uint8_t profile_format_version;
static uint8_t profile_next_seq;
static uint16_t profile_expected_len;
static uint16_t profile_received;
static uint8_t profile_staging[SOLENOID_PROFILE_WIRE_MAX];
static uint32_t profile_staging_start_us;

static bool profile_status_pending;
static can_profile_status_t profile_status;
static uint32_t profile_status_due_us;
// ADR-0020 §1 : tentatives restantes pour l'acquittement en cours.
static uint8_t profile_status_attempts;

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

// ADR-0020 §3 : une commande reçue et non exécutée est signalée, jamais muette.
// Limitée à un événement par seconde et par raison pour ne pas noyer le bus.
static void report_ignored_command(uint8_t channel, can_card_ignore_reason_t reason, uint8_t fault,
                                   uint32_t now_us) {
    static uint32_t last_us[6];
    uint32_t *slot;

    if ((unsigned)reason > 5u) {
        return;
    }
    slot = &last_us[reason];
    if (*slot != 0u && (uint32_t)(now_us - *slot) < 1000000u) {
        return;
    }
    *slot = (now_us != 0u) ? now_us : 1u;
    (void)can_link_send_card_event(CAN_CARD_EVENT_COMMAND_IGNORED, channel, (uint8_t)reason, fault);
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
        // Routage normal en diffusion : aucun événement, mais le compteur reste
        // la trace d'un éventuel mauvais adressage (ADR-0025).
        ++stats.solenoid_ignored_tag;
        return;
    }
    if (command.channel >= board_driver_count || board_drivers == NULL) {
        ++stats.solenoid_bad_channel;
        report_ignored_command(command.channel, CAN_CARD_IGNORE_CHANNEL, 0u, now_us);
        return;
    }
    if (ipropi_selftest_is_running()) {
        ++stats.solenoid_selftest_blocked;
        report_ignored_command(command.channel, CAN_CARD_IGNORE_SELFTEST, 0u, now_us);
        return;
    }
    if (!channel_is_locally_occupied(command.channel)) {
        ++stats.solenoid_unoccupied;
        report_ignored_command(command.channel, CAN_CARD_IGNORE_UNOCCUPIED, 0u, now_us);
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
        switch (solenoid_reject_counter_id(driver_result)) {
        case CAN_CARD_COUNTER_REJECT_SLEEP: ++stats.solenoid_reject_sleep; break;
        case CAN_CARD_COUNTER_REJECT_REST: ++stats.solenoid_reject_rest; break;
        case CAN_CARD_COUNTER_REJECT_BUSY: ++stats.solenoid_reject_busy; break;
        case CAN_CARD_COUNTER_REJECT_FAULT: ++stats.solenoid_reject_fault; break;
        default: ++stats.solenoid_reject_other; break;
        }
        report_ignored_command(command.channel, CAN_CARD_IGNORE_DRIVER,
                               (uint8_t)solenoid_driver_state(&board_drivers[command.channel]),
                               now_us);
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

    // Annonce de l'identité du profil (ADR-0013 §6) : le Clavier compare ce
    // `profile_tag32` au sien et signale toute divergence. Émise au boot et en
    // réponse à UID_REQUEST, jamais en annonce spontanée hors boot.
    if (announce_kind != ANNOUNCE_KIND_SELFTEST_REPLY) {
        const can_profile_status_t boot_status = {
            .uid_tag = board_tag,
            .transfer_id = CAN_PROFILE_TRANSFER_ID_BOOT,
            .result = CAN_PROFILE_RESULT_OK,
            .source = solenoid_profile_active_source(),
            .profile_tag32 = solenoid_profile_active_tag(),
        };
        if (can_encode_profile_status(data, &boot_status) != CAN_PROTO_OK ||
            !send_frame(CAN_ID_PROFILE_STATUS, CAN_DLC_PROFILE_STATUS, data)) {
            ok = false;
        }
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

// --- Profil d'activation (ADR-0013 §3-§4) -----------------------------------

static bool all_channels_idle(void) {
    if (board_drivers == NULL) {
        return false;
    }
    for (uint8_t i = 0u; i < board_driver_count; ++i) {
        if (solenoid_driver_state(&board_drivers[i]) != SOLENOID_STATE_IDLE) {
            return false;
        }
    }
    return true;
}

// ADR-0020 §1 : l'acquittement de profil est répété, borné, dans la fenêtre
// d'acquittement du Clavier (1 000 / 2 000 ms). Un refus momentané de
// `can2040_transmit` ne doit plus perdre l'information pour toujours.
#define PROFILE_STATUS_MAX_ATTEMPTS 3u
#define PROFILE_STATUS_RETRY_US 20000u

static void arm_profile_status(can_profile_result_t result, uint8_t transfer_id, uint32_t now_us) {
    profile_status.uid_tag = board_tag;
    profile_status.transfer_id = transfer_id;
    profile_status.result = result;
    profile_status.source = solenoid_profile_active_source();
    profile_status.profile_tag32 = solenoid_profile_active_tag();
    profile_status_pending = true;
    profile_status_attempts = PROFILE_STATUS_MAX_ATTEMPTS;
    profile_status_due_us = now_us + PROFILE_STATUS_SPACING_US;
}

static void handle_profile_begin(const struct can2040_msg *msg, uint32_t now_us) {
    can_profile_begin_t begin;

    if (can_decode_profile_begin(msg->data, (uint8_t)msg->dlc, &begin) != CAN_PROTO_OK) {
        ++stats.rx_bad_value;
        return;
    }
    profile_staging_active = true;
    profile_transfer_id = begin.transfer_id;
    profile_format_version = begin.format_version;
    profile_expected_len = begin.payload_len;
    profile_next_seq = 0u;
    profile_received = 0u;
    memset(profile_staging, 0, sizeof(profile_staging));
    profile_staging_start_us = now_us;
    ++stats.profile_begins;
}

static void handle_profile_chunk(const struct can2040_msg *msg) {
    can_profile_chunk_t chunk;
    uint16_t offset;
    uint16_t remaining;
    uint16_t length;

    if (can_decode_profile_chunk(msg->data, (uint8_t)msg->dlc, &chunk) != CAN_PROTO_OK) {
        ++stats.rx_bad_value;
        return;
    }
    if (!profile_staging_active || chunk.transfer_id != profile_transfer_id) {
        ++stats.rx_ignored;
        return;
    }
    if (chunk.seq != profile_next_seq || profile_expected_len > SOLENOID_PROFILE_WIRE_MAX) {
        profile_staging_active = false;
        ++stats.profile_bad_sequence;
        return;
    }
    offset = (uint16_t)((uint16_t)chunk.seq * CAN_PROFILE_CHUNK_SIZE);
    if (offset >= profile_expected_len) {
        profile_staging_active = false;
        ++stats.profile_bad_sequence;
        return;
    }
    remaining = (uint16_t)(profile_expected_len - offset);
    length = remaining < CAN_PROFILE_CHUNK_SIZE ? remaining : CAN_PROFILE_CHUNK_SIZE;
    memcpy(&profile_staging[offset], chunk.data, length);
    profile_received = (uint16_t)(offset + length);
    ++profile_next_seq;
    ++stats.profile_chunks;
}

static void handle_profile_commit(const struct can2040_msg *msg, uint32_t now_us) {
    can_profile_commit_t commit;
    solenoid_profile_t decoded;
    can_profile_result_t result = CAN_PROFILE_RESULT_NO_TRANSFER;
    uint32_t tag;

    if (can_decode_profile_commit(msg->data, (uint8_t)msg->dlc, &commit) != CAN_PROTO_OK) {
        ++stats.rx_bad_value;
        return;
    }
    if (profile_staging_active && commit.transfer_id == profile_transfer_id) {
        if (profile_format_version != SOLENOID_PROFILE_FORMAT_VERSION ||
            commit.format_version != SOLENOID_PROFILE_FORMAT_VERSION) {
            result = CAN_PROFILE_RESULT_UNSUPPORTED_VERSION;
        } else if (profile_received != profile_expected_len ||
                   profile_expected_len != SOLENOID_PROFILE_WIRE_SIZE) {
            result = CAN_PROFILE_RESULT_BAD_SEQUENCE;
        } else if (solenoid_profile_crc32(profile_staging, profile_received) != commit.crc32) {
            result = CAN_PROFILE_RESULT_BAD_CRC;
        } else if (!solenoid_profile_decode(profile_staging, profile_received, &decoded) ||
                   solenoid_profile_validate(&decoded) != SOLENOID_PROFILE_OK ||
                   !ipropi_selftest_profile_is_compatible(&decoded)) {
            result = CAN_PROFILE_RESULT_INVALID_FIELD;
        } else if (!all_channels_idle()) {
            result = CAN_PROFILE_RESULT_BUSY;
        } else {
            tag = solenoid_profile_crc32(profile_staging, profile_received);
            if (tag == solenoid_profile_active_tag() &&
                solenoid_profile_active_source() == CAN_PROFILE_SOURCE_NVM) {
                result = CAN_PROFILE_RESULT_OK;
            } else if (solenoid_profile_store_write(&decoded, tag)) {
                result = CAN_PROFILE_RESULT_OK;
            } else {
                result = CAN_PROFILE_RESULT_FLASH_ERROR;
            }
        }
        // Seul un COMMIT apparié clôt le staging ; un COMMIT tardif d'une
        // tentative antérieure ne détruit pas celui de la tentative courante.
        profile_staging_active = false;
    }
    if (result == CAN_PROFILE_RESULT_OK) {
        ++stats.profile_commits_ok;
    } else {
        ++stats.profile_commits_rejected;
    }
    arm_profile_status(result, commit.transfer_id, now_us);
}

static void send_profile_status_if_due(uint32_t now_us) {
    uint8_t data[CAN_DLC_PROFILE_STATUS];

    if (!profile_status_pending || (int32_t)(now_us - profile_status_due_us) < 0) {
        return;
    }
    if (can_encode_profile_status(data, &profile_status) != CAN_PROTO_OK) {
        ++stats.tx_fail;
    } else if (send_frame(CAN_ID_PROFILE_STATUS, CAN_DLC_PROFILE_STATUS, data)) {
        // Un acquittement suffit : on s'arrête au premier envoi réussi.
        profile_status_pending = false;
        ++stats.profile_status_sent;
        return;
    }
    if (profile_status_attempts > 0u) {
        --profile_status_attempts;
    }
    if (profile_status_attempts == 0u) {
        // Trois tentatives épuisées : l'information est perdue, mais comptée.
        profile_status_pending = false;
        ++stats.profile_status_lost;
        return;
    }
    profile_status_due_us = now_us + PROFILE_STATUS_RETRY_US;
}

bool can_link_send_card_event(can_card_event_t event, uint8_t channel, uint8_t state, uint8_t fault) {
    can_card_event_msg_t message = {0};
    uint8_t data[CAN_DLC_CARD_EVENT];

    message.proto_version = CAN_PROTO_VERSION;
    message.event = event;
    message.channel = channel;
    message.state = state;
    message.fault = fault;
    message.uid_tag = board_tag;
    if (can_encode_card_event(data, &message) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_CARD_EVENT, CAN_DLC_CARD_EVENT, data)) {
        // Best-effort : jamais de boucle de réémission ; le compteur reste la trace.
        ++stats.card_events_dropped;
        return false;
    }
    ++stats.card_events_sent;
    return true;
}

// ADR-0020 §2 : valeur d'un compteur demandé par le Clavier.
static uint32_t card_counter_value(can_card_counter_t counter) {
    switch (counter) {
    case CAN_CARD_COUNTER_CAN_ERRORS: return stats.can_errors;
    case CAN_CARD_COUNTER_TX_FAIL: return stats.tx_fail;
    case CAN_CARD_COUNTER_RX_OVERRUN:
        return motor_counter_rx_overrun(isr_rx_overrun, stats.rx_overrun);
    case CAN_CARD_COUNTER_SELFTEST_BLOCKED: return stats.solenoid_selftest_blocked;
    case CAN_CARD_COUNTER_IGNORED_TAG: return stats.solenoid_ignored_tag;
    case CAN_CARD_COUNTER_BAD_CHANNEL: return stats.solenoid_bad_channel;
    case CAN_CARD_COUNTER_PROFILE_STATUS_LOST: return stats.profile_status_lost;
    case CAN_CARD_COUNTER_PROFILE_COMMITS_REJECTED: return stats.profile_commits_rejected;
    case CAN_CARD_COUNTER_PROFILE_COMMITS_OK: return stats.profile_commits_ok;
    case CAN_CARD_COUNTER_RX_IGNORED: return stats.rx_ignored;
    case CAN_CARD_COUNTER_UNOCCUPIED: return stats.solenoid_unoccupied;
    case CAN_CARD_COUNTER_REJECTED: return stats.solenoid_rejected;
    case CAN_CARD_COUNTER_COMMANDS_OK: return stats.solenoid_commands_ok;
    case CAN_CARD_COUNTER_REJECT_SLEEP: return stats.solenoid_reject_sleep;
    case CAN_CARD_COUNTER_REJECT_REST: return stats.solenoid_reject_rest;
    case CAN_CARD_COUNTER_REJECT_BUSY: return stats.solenoid_reject_busy;
    case CAN_CARD_COUNTER_REJECT_FAULT: return stats.solenoid_reject_fault;
    case CAN_CARD_COUNTER_REJECT_OTHER: return stats.solenoid_reject_other;
    default: return 0u;
    }
}

static void handle_card_stats_request(const struct can2040_msg *msg) {
#if !MOTEUR_DIAG_TRACE
    (void)msg;
    return;
#else
    can_card_stats_request_t request;
    can_card_stats_t reply = {0};
    uint8_t data[CAN_DLC_CARD_STATS];

    if (can_decode_card_stats_request(msg->data, (uint8_t)msg->dlc, &request) != CAN_PROTO_OK) {
        ++stats.rx_bad_value;
        return;
    }
    reply.proto_version = CAN_PROTO_VERSION;
    reply.counter_id = request.counter_id;
    reply.uid_tag = board_tag;
    reply.value = card_counter_value(request.counter_id);
    if (can_encode_card_stats(data, &reply) != CAN_PROTO_OK) {
        ++stats.tx_fail;
        return;
    }
    if (!send_frame(CAN_ID_CARD_STATS, CAN_DLC_CARD_STATS, data)) {
        return;
    }
    ++stats.stats_requests_served;
#endif
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
    case CAN_ID_PROFILE_STATUS:
    case CAN_ID_CARD_EVENT:
    case CAN_ID_CARD_STATS:
        ++stats.rx_ignored;
        return;
    case CAN_ID_CARD_STATS_REQUEST:
        handle_card_stats_request(msg);
        return;
    case CAN_ID_PROFILE_BEGIN:
        handle_profile_begin(msg, now_us);
        return;
    case CAN_ID_PROFILE_CHUNK:
        handle_profile_chunk(msg);
        return;
    case CAN_ID_PROFILE_COMMIT:
        handle_profile_commit(msg, now_us);
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
    profile_staging_active = false;
    profile_transfer_id = 0u;
    profile_format_version = 0u;
    profile_next_seq = 0u;
    profile_expected_len = 0u;
    profile_received = 0u;
    profile_staging_start_us = 0u;
    profile_status_pending = false;
    profile_status_due_us = 0u;
    memset(profile_staging, 0, sizeof(profile_staging));

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
    if (ipropi_selftest_take_result(&selftest_result)) {
        if (selftest_result.report.test_status == CAN_SELFTEST_STATUS_OK) {
            local_occupation_bitmap = selftest_result.report.occupation_bits;
        } else if (selftest_result.report.test_status == CAN_SELFTEST_STATUS_ABORTED) {
            // ADR-0018 / ADR-0021 : la cause de l'avortement devient visible.
            (void)can_link_send_card_event(CAN_CARD_EVENT_SELFTEST_ABORTED,
                                           selftest_result.aborted_channel, 0u, 0u);
        }
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

    if (profile_staging_active &&
        (uint32_t)(now_us - profile_staging_start_us) >= PROFILE_STAGING_TIMEOUT_US) {
        profile_staging_active = false;
        ++stats.profile_bad_sequence;
    }
    send_profile_status_if_due(now_us);
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
