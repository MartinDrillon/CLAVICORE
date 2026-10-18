#include "can_link.h"

#include <string.h>

#include "can2040.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "can_protocol.h"
#include "config.h"

// Anneau RX SPSC (producteur : callback IRQ, consommateur : can_link_poll).
#define RX_RING_SIZE 16u  // puissance de 2
#define RX_RING_MASK (RX_RING_SIZE - 1u)
// Bornes de travail par appel de can_link_poll (ne jamais retarder le pilote).
#define POLL_MAX_RX_ENTRIES 4u

typedef struct {
    uint32_t notify;
    struct can2040_msg msg;
} rx_entry_t;

static struct can2040 cbus;
static rx_entry_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head;  // écrit par l'IRQ seulement
static volatile uint32_t rx_tail;  // écrit par la boucle principale seulement
static volatile uint32_t isr_rx_overrun;

static bool started;
static uint8_t board_uid[CAN_UID_SIZE];
static uint16_t board_tag;
static can_link_stats_t stats;

// Un seul slot de réponse en attente (ADR-0003 §4).
static bool reply_pending;
static uint8_t reply_to;  // 0 = annonce de boot
static uint32_t reply_due_us;

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

static void arm_reply(uint8_t new_reply_to, uint32_t now_us) {
    if (reply_pending) {
        reply_to = new_reply_to;  // le délai déjà armé n'est pas réarmé
        return;
    }
    reply_pending = true;
    reply_to = new_reply_to;
    reply_due_us = now_us + can_announce_delay_us(board_tag);
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
            const can_proto_result_t result =
                can_decode_uid_request(msg->data, (uint8_t)msg->dlc, &request);
            if (result == CAN_PROTO_OK) {
                // On répond TOUJOURS, quelle que soit proto_version : c'est le Clavier
                // qui signale PROTO_MISMATCH.
                ++stats.requests_served;
                arm_reply(request.request_id, now_us);
            } else if (result == CAN_PROTO_BAD_DLC) {
                ++stats.rx_bad_dlc;
            } else {
                ++stats.rx_bad_value;
            }
            return;
        }
        case CAN_ID_BOARD_ANNOUNCE_UID:
        case CAN_ID_BOARD_ANNOUNCE_INFO:
            ++stats.rx_ignored;  // annonces des autres cartes : sans objet pour le Moteur
            return;
        default:
            ++stats.rx_unknown_id;  // IDs réservés / inconnus / interdits : jamais interprétés
            return;
    }
}

// Met en file les trames A puis B. Si can2040_transmit échoue (file de 4 pleine),
// la trame est abandonnée et comptée : pas de perte silencieuse.
static void send_announce(void) {
    struct can2040_msg msg;
    bool ok = true;

    memset(&msg, 0, sizeof(msg));
    msg.id = CAN_ID_BOARD_ANNOUNCE_UID;
    msg.dlc = CAN_DLC_ANNOUNCE_UID;
    if (can_encode_announce_uid(msg.data, board_uid) != CAN_PROTO_OK ||
        can2040_transmit(&cbus, &msg) != 0) {
        ++stats.tx_fail;
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
    memset(&msg, 0, sizeof(msg));
    msg.id = CAN_ID_BOARD_ANNOUNCE_INFO;
    msg.dlc = CAN_DLC_ANNOUNCE_INFO;
    if (can_encode_announce_info(msg.data, &info) != CAN_PROTO_OK ||
        can2040_transmit(&cbus, &msg) != 0) {
        ++stats.tx_fail;
        ok = false;
    }

    if (ok) {
        ++stats.announces_sent;
    }
}

void can_link_init(const uint8_t uid[BOARD_IDENTITY_UID_SIZE], uint32_t now_us) {
    if (uid == NULL) {
        return;  // started reste false : aucune activité CAN sans UID valide
    }
    memcpy(board_uid, uid, CAN_UID_SIZE);
    board_tag = can_uid_tag(board_uid);
    memset(&stats, 0, sizeof(stats));
    rx_head = 0u;
    rx_tail = 0u;
    isr_rx_overrun = 0u;
    reply_pending = false;

    can2040_setup(&cbus, MOTEUR_CAN_PIO_NUM);
    can2040_callback_config(&cbus, can_rx_cb);
    irq_set_exclusive_handler(PIO0_IRQ_0, can_pio_irq_handler);
    irq_set_priority(PIO0_IRQ_0, MOTEUR_CAN_IRQ_PRIORITY);
    irq_set_enabled(PIO0_IRQ_0, true);
    can2040_start(&cbus, clock_get_hz(clk_sys), CAN_BITRATE_HZ, MOTEUR_CAN_GPIO_RX,
                  MOTEUR_CAN_GPIO_TX);
    started = true;

    arm_reply(CAN_REPLY_TO_BOOT, now_us);  // annonce de boot après le délai dérivé de l'UID
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

    // Délai écoulé ? (comparaison robuste au débordement de time_us_32)
    if (reply_pending && (int32_t)(now_us - reply_due_us) >= 0) {
        reply_pending = false;
        send_announce();
    }
}

can_link_stats_t can_link_stats(void) {
    can_link_stats_t copy = stats;
    copy.rx_overrun = isr_rx_overrun;
    return copy;
}
