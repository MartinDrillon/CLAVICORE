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

// --- Matériel (ADR-0002 : imposé par le câblage) ----------------------------
#define CAN_LINK_PIO_NUM 0u
#define CAN_LINK_GPIO_RX 0u
#define CAN_LINK_GPIO_TX 1u
#define CAN_LINK_PIO_IRQ PIO0_IRQ_0
#define CAN_LINK_IRQ_PRIORITY 0u  // la plus haute (> IRQ USB TinyUSB, priorité par défaut)

// --- Anneau RX SPSC (producteur : callback IRQ, consommateur : can_link_poll) ---
#define RX_RING_SIZE 32u  // puissance de 2
#define RX_RING_MASK (RX_RING_SIZE - 1u)

typedef struct {
    uint32_t notify;
    struct can2040_msg msg;
} rx_entry_t;

static struct can2040 cbus;
static rx_entry_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head;  // écrit par l'IRQ seulement
static volatile uint32_t rx_tail;  // écrit par la boucle principale seulement
static volatile uint32_t isr_rx_overrun;

// --- Réassemblage A+B ---------------------------------------------------------
#define REASM_SLOTS 8u  // 5 cartes + marge
#define SYSEX_QUEUE_SIZE 8u

typedef enum {
    SLOT_FREE = 0,
    SLOT_PENDING,
    SLOT_POISONED,  // collision de uid_tag détectée : rien n'est associé
} slot_state_t;

typedef struct {
    slot_state_t state;
    uint16_t tag;
    bool have_a;
    bool have_b;
    uint8_t uid[CAN_UID_SIZE];
    can_announce_info_t info;
    uint32_t first_ms;
} reasm_slot_t;

static reasm_slot_t slots[REASM_SLOTS];

static can_link_sysex_t sysex_queue[SYSEX_QUEUE_SIZE];
static uint8_t sysex_head;
static uint8_t sysex_count;

static can_link_stats_t stats;

// Requête en vol.
static bool started;
static bool inflight;
static uint8_t inflight_id;
static uint8_t last_request_id;
static uint32_t inflight_start_ms;
static uint32_t inflight_responses;
static bool tx_unacked;  // trame de requête mise en file can2040, pas encore envoyée avec succès
static bool bad_frame_reported;
static bool overrun_reported;
static uint32_t overrun_snapshot;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

// --- Callback can2040 : contexte IRQ, COPIE UNIQUEMENT -------------------------
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

// --- File de SysEx --------------------------------------------------------------
static void queue_sysex(const uint8_t *data, size_t len) {
    if (len == 0u) {
        return;  // encodage refusé : ne devrait pas arriver (champs validés avant)
    }
    if (sysex_count >= SYSEX_QUEUE_SIZE) {
        ++stats.events_dropped;
        return;
    }
    can_link_sysex_t *slot = &sysex_queue[(sysex_head + sysex_count) % SYSEX_QUEUE_SIZE];
    memcpy(slot->data, data, len);
    slot->len = (uint8_t)len;
    ++sysex_count;
}

static void queue_status(sysex_status_code_t code) {
    // Octet de détail : réservé, toujours 0 (non défini par l'ADR-0003).
    uint8_t buf[SYSEX_MAX_LEN];
    queue_sysex(buf, sysex_encode_status(buf, sizeof(buf), (uint8_t)code, 0u));
}

// --- Rapport ----------------------------------------------------------------------
// `info` nul = trame B manquante (rapport INCOMPLETE).
static void emit_report(const uint8_t uid[CAN_UID_SIZE], const can_announce_info_t *info) {
    sysex_uid_report_t report;
    clavicore_uid64_t uid64;
    uint8_t position = UID_POSITION_UNKNOWN_REPORTED;

    memcpy(uid64.id, uid, CAN_UID_SIZE);
    const uid_position_lookup_t lookup =
        uid_position_lookup(uid_position_table, UID_POSITION_COUNT, &uid64, &position);
    if (lookup != UID_POSITION_FOUND) {
        position = UID_POSITION_UNKNOWN_REPORTED;
    }

    memset(&report, 0, sizeof(report));
    memcpy(report.uid, uid, CAN_UID_SIZE);
    report.position = position;
    if (info != NULL) {
        report.origin = (info->reply_to == CAN_REPLY_TO_BOOT) ? SYSEX_ORIGIN_BOOT
                                                              : SYSEX_ORIGIN_REQUEST;
        report.request_id = info->reply_to;
        report.fw_major = info->fw_major;
        report.fw_minor = info->fw_minor;
        report.fw_patch = info->fw_patch;
        report.proto_version = info->proto_version;
    } else {
        // Trame B absente : versions inconnues. L'origine n'est pas connue ; on
        // l'attribue à la requête en vol s'il y en a une, sinon à une annonce de boot.
        report.origin = inflight ? SYSEX_ORIGIN_REQUEST : SYSEX_ORIGIN_BOOT;
        report.request_id = inflight ? inflight_id : 0u;
        report.fw_major = CAN_FIELD_UNKNOWN;
        report.fw_minor = CAN_FIELD_UNKNOWN;
        report.fw_patch = CAN_FIELD_UNKNOWN;
        report.proto_version = CAN_FIELD_UNKNOWN;
    }

    uint8_t buf[SYSEX_MAX_LEN];
    queue_sysex(buf, sysex_encode_uid_report(buf, sizeof(buf), &report));
    ++stats.reports_emitted;

    if (inflight && (info == NULL || info->reply_to == inflight_id)) {
        ++inflight_responses;
    }
    if (info == NULL) {
        queue_status(SYSEX_STATUS_INCOMPLETE);
    } else if (info->proto_version != CAN_PROTO_VERSION) {
        queue_status(SYSEX_STATUS_PROTO_MISMATCH);
    }
    if (lookup == UID_POSITION_TABLE_INVALID) {
        queue_status(SYSEX_STATUS_TABLE_INVALID);
    } else if (lookup == UID_POSITION_NOT_FOUND &&
               uid_position_used_count(uid_position_table, UID_POSITION_COUNT) > 0u) {
        // Table vide = état normal de la Phase 3 : pas de STATUS UID_UNKNOWN.
        queue_status(SYSEX_STATUS_UID_UNKNOWN);
    }
}

// --- Table de réassemblage -----------------------------------------------------------
static reasm_slot_t *slot_find(uint16_t tag) {
    for (uint8_t i = 0u; i < REASM_SLOTS; ++i) {
        if (slots[i].state != SLOT_FREE && slots[i].tag == tag) {
            return &slots[i];
        }
    }
    return NULL;
}

static reasm_slot_t *slot_alloc(uint16_t tag) {
    for (uint8_t i = 0u; i < REASM_SLOTS; ++i) {
        if (slots[i].state == SLOT_FREE) {
            memset(&slots[i], 0, sizeof(slots[i]));
            slots[i].state = SLOT_PENDING;
            slots[i].tag = tag;
            slots[i].first_ms = now_ms();
            return &slots[i];
        }
    }
    ++stats.reasm_full;  // trame perdue, comptée
    return NULL;
}

static void slot_try_complete(reasm_slot_t *slot) {
    if (slot->have_a && slot->have_b) {
        emit_report(slot->uid, &slot->info);
        slot->state = SLOT_FREE;
    }
}

static void on_announce_uid(const uint8_t uid[CAN_UID_SIZE]) {
    const uint16_t tag = can_uid_tag(uid);
    reasm_slot_t *slot = slot_find(tag);

    if (slot == NULL) {
        slot = slot_alloc(tag);
        if (slot == NULL) {
            return;
        }
    } else if (slot->state == SLOT_POISONED) {
        return;
    } else if (slot->have_a) {
        if (memcmp(slot->uid, uid, CAN_UID_SIZE) != 0) {
            // Deux UID différents, même tag : on n'associe rien.
            slot->state = SLOT_POISONED;
            slot->first_ms = now_ms();
            queue_status(SYSEX_STATUS_AMBIGUOUS_TAG);
        }
        return;  // doublon de la trame A : ignoré
    }
    memcpy(slot->uid, uid, CAN_UID_SIZE);
    slot->have_a = true;
    slot_try_complete(slot);
}

static void on_announce_info(const can_announce_info_t *info) {
    reasm_slot_t *slot = slot_find(info->uid_tag);

    if (slot == NULL) {
        slot = slot_alloc(info->uid_tag);
        if (slot == NULL) {
            return;
        }
    } else if (slot->state == SLOT_POISONED || slot->have_b) {
        return;
    }
    slot->info = *info;
    slot->have_b = true;
    slot_try_complete(slot);
}

static void sweep_slots(uint32_t now) {
    for (uint8_t i = 0u; i < REASM_SLOTS; ++i) {
        reasm_slot_t *slot = &slots[i];

        if (slot->state == SLOT_FREE ||
            (uint32_t)(now - slot->first_ms) < CAN_ANNOUNCE_PAIR_TIMEOUT_MS) {
            continue;
        }
        if (slot->state == SLOT_PENDING) {
            if (slot->have_a && !slot->have_b) {
                emit_report(slot->uid, NULL);  // INCOMPLETE
            } else {
                queue_status(SYSEX_STATUS_INCOMPLETE);  // trame B sans trame A : pas d'UID
            }
        }
        slot->state = SLOT_FREE;
    }
}

// --- Traitement des trames ----------------------------------------------------------------
static void note_bad_frame(void) {
    // STATUS BAD_FRAME une seule fois par requête en vol, pour ne pas inonder le SysEx.
    if (inflight && !bad_frame_reported) {
        bad_frame_reported = true;
        queue_status(SYSEX_STATUS_BAD_FRAME);
    }
}

static void handle_frame(const struct can2040_msg *msg) {
    if ((msg->id & (CAN2040_ID_EFF | CAN2040_ID_RTR)) != 0u) {
        ++stats.rx_ext_or_rtr;  // seuls les identifiants 11 bits de données sont acceptés
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
                on_announce_uid(uid);
                return;
            }
            break;
        }
        case CAN_ID_BOARD_ANNOUNCE_INFO: {
            can_announce_info_t info;
            result = can_decode_announce_info(msg->data, dlc, &info);
            if (result == CAN_PROTO_OK) {
                on_announce_info(&info);
                return;
            }
            break;
        }
        case CAN_ID_UID_REQUEST:
            ++stats.rx_ignored;  // le Clavier est l'émetteur des requêtes
            return;
        default:
            ++stats.rx_unknown_id;  // IDs réservés / inconnus / interdits : jamais interprétés
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
    sysex_head = 0u;
    sysex_count = 0u;
    rx_head = 0u;
    rx_tail = 0u;
    isr_rx_overrun = 0u;
    inflight = false;
    tx_unacked = false;
    last_request_id = 0u;

    can2040_setup(&cbus, CAN_LINK_PIO_NUM);
    can2040_callback_config(&cbus, can_rx_cb);
    irq_set_exclusive_handler(CAN_LINK_PIO_IRQ, can_pio_irq_handler);
    irq_set_priority(CAN_LINK_PIO_IRQ, CAN_LINK_IRQ_PRIORITY);
    irq_set_enabled(CAN_LINK_PIO_IRQ, true);
    can2040_start(&cbus, clock_get_hz(clk_sys), CAN_BITRATE_HZ, CAN_LINK_GPIO_RX,
                  CAN_LINK_GPIO_TX);
    started = true;
}

void can_link_poll(void) {
    if (!started) {
        return;
    }

    uint32_t tail = rx_tail;
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
            tx_unacked = false;  // trame de requête envoyée (acquittée par un autre nœud)
        }
        if ((entry.notify & CAN2040_NOTIFY_RX) != 0u) {
            handle_frame(&entry.msg);
        }
    }

    const uint32_t now = now_ms();
    sweep_slots(now);

    if (inflight) {
        if (!overrun_reported && isr_rx_overrun != overrun_snapshot) {
            overrun_reported = true;
            queue_status(SYSEX_STATUS_RX_OVERRUN);
        }
        if ((uint32_t)(now - inflight_start_ms) >= CAN_UID_REQUEST_TIMEOUT_MS) {
            if (inflight_responses == 0u) {
                queue_status(SYSEX_STATUS_NO_RESPONSE);
            }
            inflight = false;
        }
    }
}

can_link_request_result_t can_link_request_uid(void) {
    if (!started) {
        ++stats.tx_fail;
        queue_status(SYSEX_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    if (inflight) {
        queue_status(SYSEX_STATUS_BUSY);
        return CAN_LINK_REQUEST_BUSY;
    }
    if (tx_unacked) {
        // La requête précédente est toujours en file can2040 (probablement pas
        // d'ACK : aucun autre nœud actif). On n'en empile jamais une seconde.
        ++stats.tx_unacked_refused;
        queue_status(SYSEX_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }

    const can_uid_request_t request = {
        .proto_version = CAN_PROTO_VERSION,
        .request_id = can_next_request_id(last_request_id),
    };
    struct can2040_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.id = CAN_ID_UID_REQUEST;
    msg.dlc = CAN_DLC_UID_REQUEST;
    if (can_encode_uid_request(msg.data, &request) != CAN_PROTO_OK) {
        ++stats.tx_fail;
        queue_status(SYSEX_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }

    tx_unacked = true;  // avant l'émission : la notification TX peut arriver à tout moment
    if (can2040_transmit(&cbus, &msg) != 0) {
        tx_unacked = false;
        ++stats.tx_fail;
        queue_status(SYSEX_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }

    last_request_id = request.request_id;
    inflight = true;
    inflight_id = request.request_id;
    inflight_start_ms = now_ms();
    inflight_responses = 0u;
    bad_frame_reported = false;
    overrun_reported = false;
    overrun_snapshot = isr_rx_overrun;
    return CAN_LINK_REQUEST_SENT;
}

bool can_link_peek_sysex(can_link_sysex_t *out) {
    if (out == NULL || sysex_count == 0u) {
        return false;
    }
    *out = sysex_queue[sysex_head];
    return true;
}

void can_link_pop_sysex(void) {
    if (sysex_count == 0u) {
        return;
    }
    sysex_head = (uint8_t)((sysex_head + 1u) % SYSEX_QUEUE_SIZE);
    --sysex_count;
}

can_link_stats_t can_link_stats(void) {
    can_link_stats_t copy = stats;
    copy.rx_overrun = isr_rx_overrun;
    return copy;
}
