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
#include "listen_control_logic.h"
#include "listen_core1.h"
#include "listen_params.h"
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

// --- Écoute IPROPI (ADR-0032) ----------------------------------------------
// Le Moteur ne compile aucune valeur d'écoute : `listen_staging` ne contient
// que ce que le Clavier a envoyé (18 x LISTEN_PARAM), jamais un défaut.
#define LISTEN_PARK_TIMEOUT_US 10000u
#define LISTEN_TRACE_FRAME_SPACING_US 250u
#define LISTEN_TX_MAX_PER_POLL 4u

static listen_params_t listen_staging;
static bool listen_park_pending;
static can_listen_mode_t listen_mode_cached;
static bool listen_trace_sending;
static uint32_t listen_trace_index;
static uint32_t listen_trace_due_us;
static uint16_t listen_stream_seq;
static uint16_t listen_stream_frame[CAN_LISTEN_TRACE_FRAME_SAMPLES];
static uint8_t listen_stream_frame_count;
static uint32_t listen_stream_due_us;

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

// --- Écoute IPROPI : émission (ADR-0032 §3, §11) ----------------------------

static void send_listen_status_now(can_listen_state_t state, uint8_t refused_param_id) {
    can_listen_status_t status = {
        .uid_tag = board_tag,
        .state = state,
        .refused_param_id = refused_param_id,
        .sweep_period_us = (uint16_t)listen_core1_sweep_period_us(),
        .overflows = listen_core1_overflow_count(),
    };
    uint8_t data[CAN_DLC_LISTEN_STATUS];

    if (can_encode_listen_status(data, &status) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_LISTEN_STATUS, CAN_DLC_LISTEN_STATUS, data)) {
        ++stats.tx_fail;
        return;
    }
    ++stats.listen_status_sent;
}

// Refus explicite de l'auto-test pendant l'écoute (ADR-0032 §11) : le Clavier
// reçoit REFUSED_BUSY, jamais un silence.
static void send_selftest_refused_busy(uint8_t reply_to) {
    const can_announce_status_t status = {
        .uid_tag = board_tag,
        .occupation_bits = 0u,
        .reply_to = reply_to,
        .test_status = CAN_SELFTEST_STATUS_REFUSED_BUSY,
        .fault_bits = 0u,
    };
    uint8_t data[CAN_DLC_ANNOUNCE_STATUS];

    if (can_encode_announce_status(data, &status) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_BOARD_ANNOUNCE_STATUS, CAN_DLC_ANNOUNCE_STATUS, data)) {
        ++stats.tx_fail;
        return;
    }
    ++stats.announces_sent;
}

static uint16_t timing_to_100us(uint32_t us, bool *saturated) {
    uint32_t value = (us + 99u) / 100u;

    if (value > CAN_LISTEN_TIMING_SATURATED) {
        value = CAN_LISTEN_TIMING_SATURATED;
        *saturated = true;
    }
    return (uint16_t)value;
}

static void listen_send_pulse(const listen_pulse_evt_t *evt) {
    const uint32_t t_peak_us =
        (evt->t_peak_us > 0xFFFFFFFFu - 99u) ? 0xFFFFFFFFu : evt->t_peak_us;
    const uint32_t t_delta_us =
        (evt->t_end_us > evt->t_peak_us) ? (evt->t_end_us - evt->t_peak_us) : 0u;
    bool saturated = false;
    const uint16_t t_peak_100us = timing_to_100us(t_peak_us, &saturated);
    const uint16_t t_delta_100us = timing_to_100us(t_delta_us, &saturated);
    const can_listen_pulse_t pulse = {
        .uid_tag = board_tag,
        .channel = evt->channel,
        .seq = evt->seq,
        .base = evt->base,
        .peak = evt->peak,
        .split = evt->split,
        .saturated = saturated,
    };
    uint8_t data[CAN_DLC_LISTEN_PULSE];

    if (can_encode_listen_pulse(data, &pulse) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_LISTEN_PULSE, CAN_DLC_LISTEN_PULSE, data)) {
        ++stats.tx_fail;
        ++stats.listen_tx_dropped;
        return;
    }
    ++stats.listen_pulses_sent;

    if (listen_staging.detail == 0u) {
        return;
    }
    const can_listen_pulse_timing_t timing = {
        .uid_tag = board_tag,
        .seq = evt->seq,
        .t_peak_100us = t_peak_100us,
        .t_end_peak_100us = t_delta_100us,
    };
    uint8_t timing_data[CAN_DLC_LISTEN_PULSE_TIMING];

    if (can_encode_listen_pulse_timing(timing_data, &timing) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_LISTEN_PULSE_TIMING, CAN_DLC_LISTEN_PULSE_TIMING, timing_data)) {
        ++stats.tx_fail;
        ++stats.listen_tx_dropped;
    }
}

static void listen_trace_pump(uint32_t now_us) {
    // N'émettre une trace que si une session TRACE est réellement demandée :
    // une trace devenue prête pendant/après un OFF ne doit jamais être vidée
    // (elle forcerait un retour LISTEN hors de propos).
    if (!listen_core1_trace_ready()) {
        listen_trace_sending = false;
        return;
    }
    if (listen_mode_cached != CAN_LISTEN_MODE_TRACE) {
        // Le mode a changé (STREAM/LISTEN) avant ce poll : la trace ne sera
        // jamais émise. La libérer pour que le Core 1 ne reste pas bloqué dans
        // trace_wait_release, et la compter comme trame d'écoute perdue.
        listen_core1_trace_release();
        listen_trace_sending = false;
        ++stats.listen_tx_dropped;
        return;
    }
    if (!listen_trace_sending) {
        listen_trace_sending = true;
        listen_trace_index = 0u;
        listen_trace_due_us = now_us;
    }
    if ((int32_t)(now_us - listen_trace_due_us) < 0) {
        return;
    }

    can_listen_trace_t frame = {0};
    frame.seq = (uint16_t)(listen_trace_index / CAN_LISTEN_TRACE_FRAME_SAMPLES);
    for (uint8_t i = 0u; i < CAN_LISTEN_TRACE_FRAME_SAMPLES; ++i) {
        frame.samples[i] = listen_core1_trace_sample(listen_trace_index + i);
    }
    uint8_t data[CAN_DLC_LISTEN_TRACE];
    if (can_encode_listen_trace(data, &frame) == CAN_PROTO_OK &&
        send_frame(CAN_ID_LISTEN_TRACE, CAN_DLC_LISTEN_TRACE, data)) {
        ++stats.listen_trace_frames_sent;
    } else {
        // Pas de réémission en boucle : la trame est perdue et comptée.
        ++stats.tx_fail;
        ++stats.listen_tx_dropped;
    }
    listen_trace_index += CAN_LISTEN_TRACE_FRAME_SAMPLES;
    listen_trace_due_us = now_us + LISTEN_TRACE_FRAME_SPACING_US;

    if (listen_trace_index >= CAN_LISTEN_TRACE_SAMPLES) {
        listen_core1_trace_release();
        listen_trace_sending = false;
        // Retour au balayage, même session d'écoute (écrivain unique : Core 0).
        (void)listen_core1_set_mode(CAN_LISTEN_MODE_LISTEN, 0u, 0u);
        listen_mode_cached = CAN_LISTEN_MODE_LISTEN;
    }
}

static void listen_stream_pump(uint32_t now_us) {
    if (listen_mode_cached != CAN_LISTEN_MODE_STREAM) {
        listen_stream_frame_count = 0u;
        return;
    }
    if ((int32_t)(now_us - listen_stream_due_us) < 0) {
        return;
    }
    while (listen_stream_frame_count < CAN_LISTEN_TRACE_FRAME_SAMPLES &&
           listen_core1_take_stream(&listen_stream_frame[listen_stream_frame_count])) {
        ++listen_stream_frame_count;
    }
    if (listen_stream_frame_count < CAN_LISTEN_TRACE_FRAME_SAMPLES) {
        return;  // attendre les échantillons suivants, jamais bloquer
    }

    can_listen_trace_t frame = {0};
    frame.seq = listen_stream_seq;
    for (uint8_t i = 0u; i < CAN_LISTEN_TRACE_FRAME_SAMPLES; ++i) {
        frame.samples[i] = listen_stream_frame[i];
    }
    uint8_t data[CAN_DLC_LISTEN_TRACE];
    if (can_encode_listen_trace(data, &frame) == CAN_PROTO_OK &&
        send_frame(CAN_ID_LISTEN_TRACE, CAN_DLC_LISTEN_TRACE, data)) {
        ++stats.listen_trace_frames_sent;
        ++listen_stream_seq;
    } else {
        ++stats.tx_fail;
        ++stats.listen_tx_dropped;
    }
    listen_stream_frame_count = 0u;
    listen_stream_due_us = now_us + LISTEN_TRACE_FRAME_SPACING_US;
}

static void listen_tx_poll(uint32_t now_us) {
    listen_pulse_evt_t pulse;

    for (uint8_t n = 0u; n < LISTEN_TX_MAX_PER_POLL && listen_core1_take_pulse(&pulse); ++n) {
        listen_send_pulse(&pulse);
    }

    listen_status_evt_t status;
    for (uint8_t n = 0u; n < LISTEN_TX_MAX_PER_POLL && listen_core1_take_status(&status); ++n) {
        const can_listen_status_t frame = {
            .uid_tag = board_tag,
            .state = status.state,
            .refused_param_id = status.refused_param_id,
            .sweep_period_us = status.sweep_period_us,
            .overflows = status.overflows,
        };
        uint8_t data[CAN_DLC_LISTEN_STATUS];

        if (can_encode_listen_status(data, &frame) != CAN_PROTO_OK ||
            !send_frame(CAN_ID_LISTEN_STATUS, CAN_DLC_LISTEN_STATUS, data)) {
            ++stats.tx_fail;
            ++stats.listen_tx_dropped;
        } else {
            ++stats.listen_status_sent;
        }
        if (status.state == CAN_LISTEN_STATE_TRACE_TIMEOUT &&
            listen_mode_cached == CAN_LISTEN_MODE_TRACE) {
            // Après un timeout, le Core 1 reste en mode TRACE et attend un
            // retour LISTEN du Core 0, seul écrivain de s_mode (comme après
            // une trace émise). Ne pas dépendre de la réception Clavier.
            (void)listen_core1_set_mode(CAN_LISTEN_MODE_LISTEN, 0u, 0u);
            listen_mode_cached = CAN_LISTEN_MODE_LISTEN;
        }
    }

    if (listen_core1_trace_ready()) {
        listen_trace_pump(now_us);
    } else {
        listen_stream_pump(now_us);
    }
}

// --- Écoute IPROPI : réception (ADR-0032 §3, §11) ---------------------------

static void handle_listen_param(const struct can2040_msg *msg) {
    can_listen_param_t param;

    const can_proto_result_t result =
        can_decode_listen_param(msg->data, (uint8_t)msg->dlc, &param);
    if (result == CAN_PROTO_BAD_DLC) {
        ++stats.rx_bad_dlc;
        return;
    }
    if (result != CAN_PROTO_OK || param.proto_version != CAN_PROTO_VERSION) {
        ++stats.rx_bad_value;
        return;
    }
    if (can_link_listen_is_active()) {
        // Un jeu ne se modifie jamais en cours d'écoute : refus nommé.
        send_listen_status_now(CAN_LISTEN_STATE_PARAM_MISMATCH, param.param_id);
        return;
    }
    if (!listen_params_set(&listen_staging, param.param_id, param.value)) {
        send_listen_status_now(CAN_LISTEN_STATE_PARAM_MISMATCH, param.param_id);
        return;
    }
}

static void handle_listen_control(const struct can2040_msg *msg, uint32_t now_us) {
    can_listen_control_t control;

    const can_proto_result_t result =
        can_decode_listen_control(msg->data, (uint8_t)msg->dlc, &control);
    if (result == CAN_PROTO_BAD_DLC) {
        ++stats.rx_bad_dlc;
        return;
    }
    if (result != CAN_PROTO_OK || control.proto_version != CAN_PROTO_VERSION ||
        !can_listen_mode_is_valid(control.mode)) {
        ++stats.rx_bad_value;
        return;
    }

    if (control.mode == CAN_LISTEN_MODE_OFF) {
        (void)listen_core1_set_mode(CAN_LISTEN_MODE_OFF, 0u, 0u);
        listen_mode_cached = CAN_LISTEN_MODE_OFF;
        listen_trace_sending = false;
        listen_trace_index = 0u;
        listen_stream_frame_count = 0u;
        // Le Core 0 attend que le Core 1 soit parqué avant de rendre mux/ADC.
        listen_park_pending = !listen_core1_wait_parked(LISTEN_PARK_TIMEOUT_US);
        memset(&listen_staging, 0, sizeof(listen_staging));
        return;
    }

    if (listen_park_pending) {
        // La session précédente n'a pas encore rendu le mux : refus explicite.
        send_listen_status_now(CAN_LISTEN_STATE_BUSY_SELFTEST, 0u);
        return;
    }
    if (ipropi_selftest_is_running()) {
        send_listen_status_now(CAN_LISTEN_STATE_BUSY_SELFTEST, 0u);
        return;
    }

    if (control.mode == CAN_LISTEN_MODE_STREAM_OFF) {
        if (!listen_core1_active()) {
            return;  // rien à arrêter, aucun état silencieux : IDLE vient du Core 1
        }
        if (listen_core1_set_mode(CAN_LISTEN_MODE_LISTEN, 0u, 0u)) {
            listen_mode_cached = CAN_LISTEN_MODE_LISTEN;
            listen_stream_frame_count = 0u;
        }
        return;
    }

    const listen_control_action_t action =
        listen_control_action(control.mode, control.target_uid_tag, board_tag,
                              listen_core1_active(), listen_core1_params_crc(),
                              control.param_crc16);
    if (action == LISTEN_CONTROL_STAY_LISTEN) {
        // TRACE/STREAM ciblant une autre carte : celle-ci reste en écoute.
        return;
    }
    if (action == LISTEN_CONTROL_REFUSE) {
        // CRC différent en cours d'écoute : un jeu ne se substitue jamais.
        send_listen_status_now(CAN_LISTEN_STATE_PARAM_MISMATCH, 0u);
        return;
    }
    if (action == LISTEN_CONTROL_APPLY) {
        uint8_t bad_id = 0u;
        if (listen_params_validate(&listen_staging, &bad_id) != LISTEN_PARAMS_OK ||
            listen_params_crc16(&listen_staging) != control.param_crc16) {
            // CRC ou validation refusés : rien n'est appliqué (rejet en bloc).
            send_listen_status_now(CAN_LISTEN_STATE_PARAM_MISMATCH, bad_id);
            return;
        }
        if (!listen_core1_apply_params(&listen_staging, control.param_crc16)) {
            // Jeu non appliqué (mode non OFF ou arguments invalides) : refus
            // nommé, aucun démarrage avec des paramètres ignorés.
            send_listen_status_now(CAN_LISTEN_STATE_PARAM_MISMATCH, 0u);
            return;
        }
    }
    // LISTEN_CONTROL_REUSE : le Core 1 a déjà ce jeu (relance LISTEN après une
    // trace, ou changement de mode en cours d'écoute) : ne pas le réappliquer.
    if (control.mode == CAN_LISTEN_MODE_STREAM) {
        // Nouvelle session STREAM : jeter les échantillons d'une session passée.
        uint16_t dropped = 0u;
        while (listen_core1_take_stream(&dropped)) {
            // vidage borné par l'anneau (256 échantillons)
        }
        listen_stream_seq = 0u;
        listen_stream_frame_count = 0u;
        listen_stream_due_us = now_us;
    }
    if (!listen_core1_set_mode(control.mode, control.channel, control.target_uid_tag)) {
        send_listen_status_now(CAN_LISTEN_STATE_PARAM_MISMATCH, 0u);
        return;
    }
    listen_mode_cached = control.mode;
    listen_trace_sending = false;
    listen_trace_index = 0u;
}

static void handle_listen_noise_query(const struct can2040_msg *msg) {
    can_listen_noise_query_t query;

    const can_proto_result_t result =
        can_decode_listen_noise_query(msg->data, (uint8_t)msg->dlc, &query);
    if (result == CAN_PROTO_BAD_DLC) {
        ++stats.rx_bad_dlc;
        return;
    }
    if (result != CAN_PROTO_OK) {
        ++stats.rx_bad_value;
        return;
    }
    if (!listen_core1_active() || query.target_uid_tag != board_tag) {
        ++stats.rx_ignored;  // autre carte, ou écoute éteinte
        return;
    }

    uint16_t min = 0u;
    uint16_t max = 0u;
    bool truncated = false;
    (void)listen_core1_take_noise(query.channel, query.lookback_ms, &min, &max, &truncated);

    const can_listen_noise_report_t report = {
        .uid_tag = board_tag,
        .channel = query.channel,
        .request_id = query.request_id,
        .min = min,
        .max = max,
        .truncated = truncated,
    };
    uint8_t data[CAN_DLC_LISTEN_NOISE_REPORT];

    if (can_encode_listen_noise_report(data, &report) != CAN_PROTO_OK ||
        !send_frame(CAN_ID_LISTEN_NOISE_REPORT, CAN_DLC_LISTEN_NOISE_REPORT, data)) {
        ++stats.tx_fail;
        return;
    }
    ++stats.listen_noise_reports_sent;
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
            if (can_link_listen_is_active()) {
                // ADR-0032 §11 : aucune suspension en cours d'écoute ; refus nommé.
                ++stats.requests_served;
                send_selftest_refused_busy(request.request_id);
                return;
            }
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
    case CAN_ID_LISTEN_PARAM:
        handle_listen_param(msg);
        return;
    case CAN_ID_LISTEN_CONTROL:
        handle_listen_control(msg, now_us);
        return;
    case CAN_ID_LISTEN_NOISE_QUERY:
        handle_listen_noise_query(msg);
        return;
    case CAN_ID_IPROPI_SELFTEST_TRIGGER:
    case CAN_ID_BOARD_ANNOUNCE_UID:
    case CAN_ID_BOARD_ANNOUNCE_INFO:
    case CAN_ID_BOARD_ANNOUNCE_STATUS:
    case CAN_ID_IPROPI_RAW:
    case CAN_ID_IPROPI_OHM:
    case CAN_ID_PROFILE_STATUS:
    case CAN_ID_CARD_EVENT:
    case CAN_ID_CARD_STATS:
    // Ces trames sont émises par le Moteur : il ne les reçoit pas.
    case CAN_ID_LISTEN_PULSE:
    case CAN_ID_LISTEN_STATUS:
    case CAN_ID_LISTEN_NOISE_REPORT:
    case CAN_ID_LISTEN_PULSE_TIMING:
    case CAN_ID_LISTEN_TRACE:
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

    // Écoute IPROPI : structures partagées seulement, le Core 1 est lancé par
    // main.c après cette initialisation (ADC et mux déjà prêts).
    memset(&listen_staging, 0, sizeof(listen_staging));
    listen_park_pending = false;
    listen_mode_cached = CAN_LISTEN_MODE_OFF;
    listen_trace_sending = false;
    listen_trace_index = 0u;
    listen_trace_due_us = 0u;
    listen_stream_seq = 0u;
    listen_stream_frame_count = 0u;
    listen_stream_due_us = 0u;
    listen_core1_init(board_uid, board_tag, drivers, driver_count);

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

    // ADR-0032 §11 : tant que le Core 1 n'a pas rendu mux/ADC, l'auto-test
    // reste refusé ; on réessaie sans bloquer la boucle principale.
    if (listen_park_pending && listen_core1_wait_parked(0u)) {
        listen_park_pending = false;
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

    // Vidage des anneaux d'écoute (0x110/0x428/0x426/0x429) : jamais affamé par
    // un acquittement en attente.
    listen_tx_poll(now_us);

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

void can_link_listen_set_drive_mask(uint16_t non_idle_mask) {
    listen_core1_set_drive_mask(non_idle_mask);
}

bool can_link_listen_is_active(void) {
    // `listen_park_pending` couvre la fenêtre entre OFF et le parcage effectif
    // du Core 1 : l'auto-test ne doit pas reprendre le mux avant.
    return listen_core1_active() || listen_park_pending;
}
