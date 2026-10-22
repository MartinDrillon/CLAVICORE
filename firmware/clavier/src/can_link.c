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
#include "diag_out.h"
#include "solenoid_profile.h"
#include "solenoid_profile_config.h"
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
#define CAN_LINK_PROFILE_SYNC_TIMEOUT_MS 1000u
// Réglage console : même chemin, délai plus large (écriture flash côté Moteur,
// jusqu'à plusieurs centaines de ms en pire cas, cartes en parallèle).
#define CAN_LINK_PROFILE_TUNING_TIMEOUT_MS 2000u
#define CAN_PROFILE_FRAME_SPACING_US 300u
#define CAN_PROFILE_FRAME_RETRY_US 100u
#define CARD_LIST_TEXT_MAX 128u
#define SUMMARY_TEXT_MAX 160u
#ifndef CLAVIER_SELFTEST_CALIBRATION
#define CLAVIER_SELFTEST_CALIBRATION 0
#endif
// Lignes [MAP] (une par canal assigné) : confort de bring-up (ADR-0009 §4).
// Passer à 0 en configuration finale.
#ifndef CLAVIER_MAP_LOG
#define CLAVIER_MAP_LOG 1
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
    // Diagnostic ADR-0009 : valeurs brutes par canal (pour recomposer la ligne
    // `delta` quand l'IPROPI_OHM arrive) et valeurs estimées reçues.
    uint16_t raw_idle[UID_POSITION_COUNT][CAN_IPROPI_CHANNEL_COUNT];
    uint16_t raw_probe[UID_POSITION_COUNT][CAN_IPROPI_CHANNEL_COUNT];
    bool ohm_received_position[UID_POSITION_COUNT][CAN_IPROPI_CHANNEL_COUNT];
    uint16_t ohm_milliohm[UID_POSITION_COUNT][CAN_IPROPI_CHANNEL_COUNT];
    uint16_t i100_ma[UID_POSITION_COUNT][CAN_IPROPI_CHANNEL_COUNT];
    uint8_t ohm_received_count;
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
// Synchronisation du profil d'activation (ADR-0013 §4) : le profil est diffusé
// AVANT 0x401, et l'auto-test n'est déclenché que si toutes les cartes présentes
// l'ont acquitté.
static bool profile_sync_active;
static uint8_t profile_sync_transfer_id;
static uint8_t profile_sync_next_transfer_id;
static uint8_t profile_sync_expected;
static uint8_t profile_sync_received;
static bool profile_sync_failed;
static uint32_t profile_sync_start_ms;
static uint32_t local_profile_tag;

// Attente et attribution des acquittements (ADR-0016 décisions 1 à 5) : tout est
// indexé par INDEX d'entrée de `uid_position_table`, jamais par numéro de
// position. La position restituée au journal est toujours la position STOCKÉE de
// l'entrée (`uid_position_table[i].position`).
typedef enum {
    PROFILE_CARD_WAITING = 0,   // attendue, aucun acquittement attribuable
    PROFILE_CARD_ACK_OK,        // PROFILE_STATUS reçu, result = OK
    PROFILE_CARD_ACK_REJECTED,  // PROFILE_STATUS reçu, result != OK
} profile_card_ack_t;
// UID de l'entrée vu en ligne depuis le dernier scan de boot : c'est ce qui
// définit « attendue » (ADR-0016 décision 2), pas le numéro de position.
static bool profile_seed_online[UID_POSITION_COUNT];
// PROFILE_STATUS de boot (transfer_id = 0) reçu de cette entrée : distingue
// ACK_TIMEOUT de NO_PROFILE_STATUS (ADR-0016 décision 5).
static bool profile_boot_seen[UID_POSITION_COUNT];
static profile_card_ack_t profile_card_ack[UID_POSITION_COUNT];
// Source réelle du déclenchement en cours (ADR-0017 décision 5).
static const char *profile_sync_source = "bouton";
// Re-requête d'UID émise après un échec, pour rendre compte de ce que chaque
// carte détient réellement (ADR-0017 décision 6).
static bool profile_probe_pending;
// Réglage à chaud (ADR-0015) : un transfert de profil a un BUT. L'auto-test
// enchaîne sur 0x401 ; le réglage console se contente de rendre compte.
typedef enum {
    PROFILE_SYNC_GOAL_SELFTEST = 0,
    PROFILE_SYNC_GOAL_TUNING,
} profile_sync_goal_t;
static profile_sync_goal_t profile_sync_goal;
// Profil ACTIF = dernier profil acquitté par les cartes (au boot : celui du
// fichier compilé). `profile_sync_pending` = profil en cours de transfert, promu
// en actif seulement si toutes les cartes attendues l'ont acquitté.
static solenoid_profile_t profile_active;
static solenoid_profile_t profile_sync_pending;
// Mode diagnostic : défaut de compilation, modifiable à chaud par la console
// de bring-up (ADR-0009 §7, livraison B).
static bool calibration_enabled = CLAVIER_SELFTEST_CALIBRATION != 0;

static void can_rx_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg);
static void can_pio_irq_handler(void);

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

// Échéance atteinte ? Comparaison SIGNÉE obligatoire : une échéance peut être
// armée depuis le drainage de la file RX, donc après l'horodatage `first_ms` de
// la passe en cours (déclenchement 0x402 reçu pendant la passe). Une soustraction
// `uint32_t` nue la ferait alors passer pour « déjà dépassée » et l'échec serait
// prononcé avant même que le délai ait couru. ADR-0016 décision 7.
static bool deadline_reached(uint32_t first_ms, uint32_t start_ms, uint32_t timeout_ms) {
    return (int32_t)(first_ms - start_ms) >= (int32_t)timeout_ms;
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
    diag_out_line(line);
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

static void emit_table_state_line(void) {
    diag_table_state_t state;
    char line[DIAG_LOG_LINE_MAX];

    if (!commissioning.valid) {
        return;
    }
    state.state = commissioning.table_state;
    state.flags = commissioning.table_flags;
    state.cards = (uint8_t)commissioning.entry_count;
    state.keys = commissioning.runtime.total_occupied;
    if (diag_format_table_state_line(line, sizeof(line), &state) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

// Une ligne `[MAP]` par canal assigné (ADR-0009 §4) : canal -> touche -> note.
// Dans un bloc, les canaux occupés croissants reçoivent les touches croissantes.
// La touche de départ du bloc vient de `runtime.position_first_key`, calculée par
// `commissioning_build_runtime` : ancrage grave (règle ascendante, ADR-0010).
static void emit_map_line(uint8_t position, uint8_t channel, uint8_t key) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_map_line(line, sizeof(line), position, channel, key,
                             commissioning_key_to_midi_note(key)) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_map_lines(void) {
#if CLAVIER_MAP_LOG
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        const commissioning_entry_t *entry;
        uint8_t key;

        if (!commissioning.runtime.position_present[position]) {
            continue;
        }
        entry = commissioning_state_entry_by_position(&commissioning, position);
        if (entry == NULL) {
            continue;
        }
        key = commissioning.runtime.position_first_key[position];
        if (key == 0u) {
            continue;
        }
        for (uint8_t channel = 0u; channel < CAN_IPROPI_CHANNEL_COUNT; ++channel) {
            if ((entry->occupation_bitmap & ((uint16_t)1u << channel)) == 0u) {
                continue;
            }
            emit_map_line(position, channel, key);
            ++key;
        }
    }
#endif
}

// L'état provisoire est toujours signalé (invariant 1, ADR-0009 §4).
static void emit_table_state_status(void) {
    if (commissioning.valid && commissioning.table_state == COMMISSIONING_TABLE_STATE_PROVISIONAL) {
        emit_status(DIAG_STATUS_TABLE_PROVISIONAL);
    }
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
    diag_boot_report_t report = {0};
    char line[DIAG_LOG_LINE_MAX];

    report.fw_major = CLAVIER_FW_MAJOR;
    report.fw_minor = CLAVIER_FW_MINOR;
    report.fw_patch = CLAVIER_FW_PATCH;
    report.proto_version = CAN_PROTO_VERSION;
    report.build_id = diag_out_build_id();
    report.table_state = table_state;
    report.cards = cards;
    if (diag_format_boot_line(line, sizeof(line), &report) == 0u) {
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

    if (diag_format_trigger_line(line, sizeof(line), profile_sync_source, request_id) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
}

static void emit_trigger_refused_line(const char *reason) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_trigger_refused_line(line, sizeof(line), profile_sync_source, reason) == 0u) {
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

// ADR-0016 décision 1 : la table d'amorçage `uid_position.h` est l'UNIQUE
// référence pour nommer les cartes pendant la synchronisation du profil,
// interrogée par UID COMPLET. La position rendue est toujours la position
// STOCKÉE de l'entrée, jamais l'index de boucle (IMP-001).
static int profile_seed_index_for_uid(const uint8_t uid[CAN_UID_SIZE]) {
    clavicore_uid64_t uid64;
    uint8_t position;
    uid_position_lookup_t lookup;

    memcpy(uid64.id, uid, CAN_UID_SIZE);
    lookup = uid_position_lookup(uid_position_table, UID_POSITION_COUNT, &uid64, &position);
    if (lookup != UID_POSITION_FOUND) {
        return -1;
    }
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position == position) {
            return (int)i;
        }
    }
    return -1;
}

// Même correspondance, depuis le `uid_tag` (CRC16) porté par une trame : c'est
// l'attribution des acquittements (ADR-0016 décisions 3 et 4).
static int profile_seed_index_for_tag(uint16_t uid_tag) {
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position == UID_POSITION_EMPTY) {
            continue;
        }
        if (can_uid_tag(uid_position_table[i].uid.id) == uid_tag) {
            return (int)i;
        }
    }
    return -1;
}

static bool boot_scan_in_progress(void) {
    return uid_inflight && uid_request_mode == UID_REQUEST_MODE_BOOT_SCAN;
}

// ADR-0017 décision 4 : une annonce de boot spontanée reçue hors scan de boot
// est un redémarrage de carte — état des solénoïdes et profil non garantis —
// donc signalé, daté et compté, jamais toléré en silence (invariant 1).
static void emit_card_reboot(uint8_t position, const uint8_t uid[CAN_UID_SIZE]) {
    diag_card_report_t card = {0};
    char line[DIAG_LOG_LINE_MAX];

    card.position = position;
    card.node = position < UID_POSITION_COUNT ? (uint8_t)(position + 2u) : 0u;
    memcpy(card.uid, uid, CAN_UID_SIZE);
    card.state = DIAG_CARD_STATE_REBOOT;
    if (diag_format_card_line(line, sizeof(line), &card) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
    emit_status_position(DIAG_STATUS_CARD_REBOOTED, position);
    ++stats.card_rebooted;
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
    // « Attendue » se compte sur l'UID vu en ligne depuis le dernier scan de boot
    // (ADR-0016 décision 2), jamais sur un numéro de position.
    {
        const int seed_index = profile_seed_index_for_uid(uid);

        if (seed_index >= 0) {
            profile_seed_online[(size_t)seed_index] = true;
        }
    }
    if (info != NULL && info->reply_to == CAN_REPLY_TO_BOOT && !boot_scan_in_progress()) {
        emit_card_reboot(report.position, uid);
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
        // ADR-0016 décisions 2 et 5 : « attendue » et « parle le protocole de
        // profil » se réévaluent à chaque scan de boot, en même temps que la
        // présence des cartes.
        memset(profile_seed_online, 0, sizeof(profile_seed_online));
        memset(profile_boot_seen, 0, sizeof(profile_boot_seen));
    }
    return true;
}

// Émission d'une trame de profil en respectant la file TX de can2040 (3 tampons) :
// on réessaie brièvement si elle est pleine (ADR-0013 IMP-007).
static bool send_profile_frame(uint32_t id, uint8_t dlc, const uint8_t *data) {
    for (uint32_t attempt = 0u; attempt < 100u; ++attempt) {
        if (send_frame(id, dlc, data)) {
            sleep_us(CAN_PROFILE_FRAME_SPACING_US);
            return true;
        }
        sleep_us(CAN_PROFILE_FRAME_RETRY_US);
    }
    return false;
}

// Diffuse `profile` (BEGIN + chunks + COMMIT) puis arme l'attente des
// PROFILE_STATUS. Ne déclenche PAS 0x401 : can_link_poll le fait, pour le but
// SELFTEST uniquement, quand toutes les cartes présentes ont acquitté
// (ADR-0013 §4). Le but TUNING (ADR-0015) s'arrête au compte rendu.
static bool begin_profile_sync(const solenoid_profile_t *profile, profile_sync_goal_t goal,
                               const char *source) {
    uint8_t wire[SOLENOID_PROFILE_WIRE_SIZE];
    uint8_t frame[CAN_DLC_PROFILE_BEGIN];
    const size_t length = solenoid_profile_encode(profile, wire, sizeof(wire));

    if (length != SOLENOID_PROFILE_WIRE_SIZE ||
        solenoid_profile_validate(profile) != SOLENOID_PROFILE_OK) {
        emit_status(DIAG_STATUS_PROFILE_SYNC_FAILED);
        return false;
    }

    profile_sync_transfer_id = profile_sync_next_transfer_id;
    profile_sync_next_transfer_id = (uint8_t)(profile_sync_next_transfer_id + 1u);
    if (profile_sync_next_transfer_id == CAN_PROFILE_TRANSFER_ID_BOOT) {
        profile_sync_next_transfer_id = CAN_PROFILE_TRANSFER_ID_MIN;
    }

    const can_profile_begin_t begin = {
        .proto_version = CAN_PROTO_VERSION,
        .format_version = SOLENOID_PROFILE_FORMAT_VERSION,
        .transfer_id = profile_sync_transfer_id,
        .flags = CAN_PROFILE_FLAG_GLOBAL,
        .payload_len = (uint16_t)length,
        .chunk_size = CAN_PROFILE_CHUNK_SIZE,
        .reserved = 0u,
    };
    if (can_encode_profile_begin(frame, &begin) != CAN_PROTO_OK ||
        !send_profile_frame(CAN_ID_PROFILE_BEGIN, CAN_DLC_PROFILE_BEGIN, frame)) {
        return false;
    }

    for (uint8_t seq = 0u; (size_t)seq * CAN_PROFILE_CHUNK_SIZE < length; ++seq) {
        can_profile_chunk_t chunk = {0};
        uint8_t chunk_frame[CAN_DLC_PROFILE_CHUNK];
        const size_t offset = (size_t)seq * CAN_PROFILE_CHUNK_SIZE;
        const size_t remaining = length - offset;
        const size_t count =
            remaining < CAN_PROFILE_CHUNK_SIZE ? remaining : CAN_PROFILE_CHUNK_SIZE;

        chunk.transfer_id = profile_sync_transfer_id;
        chunk.seq = seq;
        memcpy(chunk.data, &wire[offset], count);
        if (can_encode_profile_chunk(chunk_frame, &chunk) != CAN_PROTO_OK ||
            !send_profile_frame(CAN_ID_PROFILE_CHUNK, CAN_DLC_PROFILE_CHUNK, chunk_frame)) {
            return false;
        }
    }

    const can_profile_commit_t commit = {
        .proto_version = CAN_PROTO_VERSION,
        .format_version = SOLENOID_PROFILE_FORMAT_VERSION,
        .transfer_id = profile_sync_transfer_id,
        .reserved = 0u,
        .crc32 = solenoid_profile_crc32(wire, length),
    };
    if (can_encode_profile_commit(frame, &commit) != CAN_PROTO_OK ||
        !send_profile_frame(CAN_ID_PROFILE_COMMIT, CAN_DLC_PROFILE_COMMIT, frame)) {
        return false;
    }

    memset(profile_card_ack, 0, sizeof(profile_card_ack));
    profile_probe_pending = false;
    profile_sync_source = (source != NULL) ? source : "inconnu";
    // Cartes ATTENDUES (ADR-0016 décision 2) : les entrées de `uid_position.h`
    // dont l'UID a été vu en ligne depuis le dernier scan de boot — c'est-à-dire
    // une carte qui a annoncé son UID. Aucune table persistée n'est requise :
    // l'auto-test de bring-up reste possible sans table valide. Une entrée
    // d'amorçage dont l'UID réel ne correspond pas à la carte présente n'est donc
    // jamais attendue, et ne peut plus bloquer la procédure par un simple effet
    // de numéro de position.
    profile_sync_expected = 0u;
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position == UID_POSITION_EMPTY) {
            continue;
        }
        if (profile_seed_online[i]) {
            ++profile_sync_expected;
        }
    }
    profile_sync_received = 0u;
    profile_sync_failed = false;
    profile_sync_active = true;
    profile_sync_start_ms = now_ms();
    profile_sync_goal = goal;
    profile_sync_pending = *profile;
    diag_out_printf("[PROFILE] sync debut but=%s tag=0x%08lX attendus=%u source=%s\n",
                    goal == PROFILE_SYNC_GOAL_TUNING ? "reglage" : "auto-test",
                    (unsigned long)solenoid_profile_crc32(wire, length),
                    (unsigned)profile_sync_expected, profile_sync_source);
    if (goal == PROFILE_SYNC_GOAL_SELFTEST &&
        solenoid_profile_tag(&profile_active) != solenoid_profile_tag(profile)) {
        diag_out_printf(
            "[PROFILE] auto-test: le profil du fichier compile remplace le profil regle a chaud\n");
    }
    return true;
}

// Durée réellement écoulée depuis l'armement (ADR-0017 décision 3). Comparaison
// SIGNÉE : l'échéance peut avoir été armée après l'horodatage de la passe
// (ADR-0016 décision 7), et une durée ne peut pas être négative.
static uint32_t profile_sync_elapsed_ms(uint32_t first_ms) {
    const int32_t elapsed = (int32_t)(first_ms - profile_sync_start_ms);

    return elapsed > 0 ? (uint32_t)elapsed : 0u;
}

static const char *profile_result_name(uint8_t result) {
    switch (result) {
    case CAN_PROFILE_RESULT_OK: return "OK";
    case CAN_PROFILE_RESULT_NO_TRANSFER: return "NO_TRANSFER";
    case CAN_PROFILE_RESULT_BAD_SEQUENCE: return "BAD_SEQUENCE";
    case CAN_PROFILE_RESULT_BAD_CRC: return "BAD_CRC";
    case CAN_PROFILE_RESULT_INVALID_FIELD: return "INVALID_FIELD";
    case CAN_PROFILE_RESULT_BUSY: return "BUSY(voie occupee)";
    case CAN_PROFILE_RESULT_FLASH_ERROR: return "FLASH_ERROR";
    case CAN_PROFILE_RESULT_UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
    default: return "?";
    }
}

// Motif lisible par carte (ADR-0016 décision 5). Un simple « MANQUANT » ne
// distinguait pas une carte qui ne parle pas le protocole de profil d'une carte
// qui a reçu mais dont l'acquittement s'est perdu.
static const char *profile_card_motive_name(size_t index) {
    switch (profile_card_ack[index]) {
    case PROFILE_CARD_ACK_OK: return "PROFILE_STATUS_SENT_OK";
    case PROFILE_CARD_ACK_REJECTED: return "ACK_REJECTED";
    case PROFILE_CARD_WAITING:
    default:
        return profile_boot_seen[index] ? "ACK_TIMEOUT" : "NO_PROFILE_STATUS";
    }
}

// Au moins une carte attendue n'a rien acquitté ET n'a jamais annoncé de
// PROFILE_STATUS de boot : son firmware est antérieur à ADR-0013 (IMP-011).
static bool profile_sync_any_no_boot_status(void) {
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position == UID_POSITION_EMPTY || !profile_seed_online[i]) {
            continue;
        }
        if (profile_card_ack[i] != PROFILE_CARD_ACK_OK && !profile_boot_seen[i]) {
            return true;
        }
    }
    return false;
}

// ADR-0017 décision 6 : après un échec, une seule requête d'UID pour rendre
// compte de ce que chaque carte détient réellement. N'ouvre aucune fenêtre
// d'acquittement et ne compte rien.
static void profile_request_probe(void) {
    if (start_uid_request(UID_REQUEST_MODE_MANUAL, false)) {
        profile_probe_pending = true;
    }
}

// Liste, pour chaque carte attendue, l'état de son acquittement : sans cela un
// échec ne dit pas QUELLE carte n'a pas répondu, ni pourquoi (ADR-0016 §5).
static void print_profile_sync_cards(void) {
    for (size_t i = 0u; i < UID_POSITION_COUNT; ++i) {
        if (uid_position_table[i].position == UID_POSITION_EMPTY || !profile_seed_online[i]) {
            continue;
        }
        diag_out_printf("[PROFILE] sync carte pos=%u tag=0x%04X motif=%s\n",
                        (unsigned)uid_position_table[i].position,
                        (unsigned)can_uid_tag(uid_position_table[i].uid.id),
                        profile_card_motive_name(i));
    }
}

static void handle_profile_status(const can_profile_status_t *status) {
    // Attribution par UID sur la table d'amorçage, position STOCKÉE restituée
    // (ADR-0016 décisions 1 et 3).
    const int seed_index = profile_seed_index_for_tag(status->uid_tag);
    const uint8_t position = seed_index >= 0 ? uid_position_table[(size_t)seed_index].position
                                             : UID_POSITION_UNKNOWN_REPORTED;

    if (status->transfer_id == CAN_PROFILE_TRANSFER_ID_BOOT) {
        if (seed_index >= 0) {
            // La carte parle le protocole de profil : c'est ce qui distingue
            // ACK_TIMEOUT de NO_PROFILE_STATUS (ADR-0016 décision 5).
            profile_boot_seen[(size_t)seed_index] = true;
        }
        if (profile_probe_pending) {
            // ADR-0017 décision 6 : compte rendu après échec. Une réponse de boot
            // n'est JAMAIS un acquittement du transfert qui vient d'échouer.
            diag_out_printf("[PROFILE] pos=%u detient tag=0x%08lX source=%s\n",
                            (unsigned)position, (unsigned long)status->profile_tag32,
                            status->source == CAN_PROFILE_SOURCE_NVM ? "NVM" : "DEFAULTS");
            return;
        }
        // Annonce de boot / réponse à UID_REQUEST (ADR-0013 §6) : signaler une
        // divergence, jamais corriger en silence (invariant 1).
        if (status->profile_tag32 != local_profile_tag) {
            emit_status_position(DIAG_STATUS_PROFILE_DIVERGENT, position);
            diag_out_printf("[PROFILE] pos=%u card=0x%08lX local=0x%08lX source=%s\n",
                            (unsigned)position, (unsigned long)status->profile_tag32,
                            (unsigned long)local_profile_tag,
                            status->source == CAN_PROFILE_SOURCE_NVM ? "NVM" : "DEFAULTS");
            ++stats.profile_divergent;
        }
        if (status->source == CAN_PROFILE_SOURCE_DEFAULTS) {
            emit_status_position(DIAG_STATUS_PROFILE_FALLBACK_IN_USE, position);
        }
        return;
    }
    if (!profile_sync_active || status->transfer_id != profile_sync_transfer_id) {
        return;
    }
    if (seed_index < 0 || !profile_seed_online[(size_t)seed_index]) {
        // ADR-0016 décision 4 : un acquittement non attribuable n'est jamais
        // jeté en silence. Produit dès réception, sans attendre le délai.
        diag_out_printf("[PROFILE] ack inattendu tag=0x%04X transfer=%u result=%u\n",
                        (unsigned)status->uid_tag, (unsigned)status->transfer_id,
                        (unsigned)status->result);
        emit_status(DIAG_STATUS_PROFILE_ACK_UNATTRIBUTED);
        ++stats.profile_ack_unattributed;
        return;
    }
    if (status->result == CAN_PROFILE_RESULT_OK) {
        if (profile_card_ack[(size_t)seed_index] != PROFILE_CARD_ACK_OK) {
            profile_card_ack[(size_t)seed_index] = PROFILE_CARD_ACK_OK;
            ++profile_sync_received;
        }
    } else {
        profile_card_ack[(size_t)seed_index] = PROFILE_CARD_ACK_REJECTED;
        profile_sync_failed = true;
        diag_out_printf("[PROFILE] refus pos=%u result=%u %s\n", (unsigned)position,
                        (unsigned)status->result,
                        profile_result_name((uint8_t)status->result));
    }
}

static bool start_selftest_request(void) {
    uint8_t payload[CAN_DLC_IPROPI_SELFTEST_REQUEST];
    const can_ipropi_selftest_request_t request = {
        .proto_version = CAN_PROTO_VERSION,
        .request_id = can_next_request_id(last_request_id),
        .calibration_mode = calibration_enabled,
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

// Ligne `idle`/`probe` d'un canal : émise dès réception de l'IPROPI_RAW.
static void emit_selftest_raw_probe_line(uint8_t position, const can_ipropi_raw_t *raw) {
    const uint8_t request_id = raw->reply_to;
    const uint8_t channel = raw->channel;
    const uint16_t idle_avg = raw->idle_avg;
    const uint16_t probe_avg = raw->probe_avg;
    const diag_selftest_raw_report_t report = {
        .request_id = request_id,
        .position = position,
        .channel = channel,
        .idle_avg = idle_avg,
        .probe_avg = probe_avg,
        // Pas encore connues : l'IPROPI_OHM du canal suit les RAW (ADR-0009 §6).
        .ohm_milliohm = CAN_IPROPI_VALUE_INVALID,
        .i100_ma = CAN_IPROPI_VALUE_INVALID,
    };
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_selftest_raw_line(line, sizeof(line), &report) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
    ++stats.reports_emitted;
}

// Ligne `delta` + estimation ohmique : émise à la réception de l'IPROPI_OHM.
static void emit_selftest_delta_line(uint8_t position, uint8_t channel, uint16_t idle_avg,
                                     uint16_t probe_avg, uint16_t ohm_milliohm, uint16_t i100_ma,
                                     uint8_t request_id) {
    const diag_selftest_raw_report_t report = {
        .request_id = request_id,
        .position = position,
        .channel = channel,
        .idle_avg = idle_avg,
        .probe_avg = probe_avg,
        .ohm_milliohm = ohm_milliohm,
        .i100_ma = i100_ma,
    };
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_selftest_raw_delta_line(line, sizeof(line), &report) == 0u) {
        ++stats.events_dropped;
        return;
    }
    emit_line(line);
    ++stats.reports_emitted;
}

// Lignes `delta` des canaux dont l'IPROPI_OHM n'est jamais arrivée : le journal
// reste complet et l'absence est visible (`ohm_est=--`), jamais silencieuse.
static void emit_missing_ohm_delta_lines(void) {
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        for (uint8_t channel = 0u; channel < CAN_IPROPI_CHANNEL_COUNT; ++channel) {
            if (!selftest_session.raw_received_position[position][channel] ||
                selftest_session.ohm_received_position[position][channel]) {
                continue;
            }
            emit_selftest_delta_line(position, channel,
                                     selftest_session.raw_idle[position][channel],
                                     selftest_session.raw_probe[position][channel],
                                     CAN_IPROPI_VALUE_INVALID, CAN_IPROPI_VALUE_INVALID,
                                     selftest_session.request_id);
        }
    }
}

// Synthèse ohmique par carte (ADR-0009 §5c), sur les canaux occupés mesurés :
// min/max/écart et étendue du courant à 100 % de duty.
static void emit_selftest_ohm_summaries(void) {
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        diag_ohm_summary_t summary = {0};
        uint32_t ohm_sum = 0u;
        uint16_t ohm_min = 0u;
        uint16_t ohm_max = 0u;
        uint16_t i100_min = 0u;
        uint16_t i100_max = 0u;
        char line[DIAG_LOG_LINE_MAX];

        if (!selftest_session.status_valid[position]) {
            continue;
        }
        for (uint8_t channel = 0u; channel < CAN_IPROPI_CHANNEL_COUNT; ++channel) {
            const uint16_t ohm = selftest_session.ohm_milliohm[position][channel];
            const uint16_t i100 = selftest_session.i100_ma[position][channel];

            if (!selftest_session.ohm_received_position[position][channel] ||
                ohm == CAN_IPROPI_VALUE_INVALID) {
                continue;
            }
            if (summary.count == 0u) {
                ohm_min = ohm;
                ohm_max = ohm;
                i100_min = i100;
                i100_max = i100;
            } else {
                if (ohm < ohm_min) {
                    ohm_min = ohm;
                }
                if (ohm > ohm_max) {
                    ohm_max = ohm;
                }
                if (i100 < i100_min) {
                    i100_min = i100;
                }
                if (i100 > i100_max) {
                    i100_max = i100;
                }
            }
            ohm_sum += ohm;
            if (i100 != CAN_IPROPI_VALUE_INVALID) {
                if (i100 > 3000u) {
                    summary.warn_i100_over_3a = true;
                }
            }
            ++summary.count;
        }
        if (summary.count == 0u) {
            continue;
        }

        summary.position = position;
        summary.ohm_min_milliohm = ohm_min;
        summary.ohm_max_milliohm = ohm_max;
        summary.i100_min_ma = i100_min;
        summary.i100_max_ma = i100_max;
        // Écart relatif (max - min) / moyenne, en pourcent.
        if (ohm_sum > 0u) {
            const uint32_t mean = ohm_sum / summary.count;
            const uint32_t spread = (mean > 0u) ? (((uint32_t)(ohm_max - ohm_min) * 100u) / mean) : 0u;

            summary.spread_percent = (uint8_t)(spread > 255u ? 255u : spread);
        }
        if (diag_format_selftest_ohm_line(line, sizeof(line), &summary) == 0u) {
            ++stats.events_dropped;
            continue;
        }
        emit_line(line);
        ++stats.reports_emitted;
    }
}

static void on_ipropi_raw(const can_ipropi_raw_t *raw) {
    uint8_t position;

    if (!selftest_session.active || raw->reply_to != selftest_session.request_id ||
        !selftest_session.calibration_mode) {
        return;
    }
    position = selftest_position_for_tag(raw->uid_tag);
    if (position < UID_POSITION_COUNT && raw->channel < CAN_IPROPI_CHANNEL_COUNT) {
        selftest_session.raw_idle[position][raw->channel] = raw->idle_avg;
        selftest_session.raw_probe[position][raw->channel] = raw->probe_avg;
    }
    emit_selftest_raw_probe_line(position, raw);
    if (position < UID_POSITION_COUNT) {
        mark_selftest_raw_received(position, raw->channel);
    }
}

static void on_ipropi_ohm(const can_ipropi_ohm_t *ohm) {
    uint8_t position;

    if (!selftest_session.active || ohm->reply_to != selftest_session.request_id ||
        !selftest_session.calibration_mode) {
        return;
    }
    position = selftest_position_for_tag(ohm->uid_tag);
    if (position >= UID_POSITION_COUNT || ohm->channel >= CAN_IPROPI_CHANNEL_COUNT) {
        return;
    }

    selftest_session.ohm_milliohm[position][ohm->channel] = ohm->ohm_milliohm;
    selftest_session.i100_ma[position][ohm->channel] = ohm->i100_ma;
    if (!selftest_session.ohm_received_position[position][ohm->channel]) {
        selftest_session.ohm_received_position[position][ohm->channel] = true;
        ++selftest_session.ohm_received_count;
    }
    emit_selftest_delta_line(position, ohm->channel,
                             selftest_session.raw_idle[position][ohm->channel],
                             selftest_session.raw_probe[position][ohm->channel],
                             ohm->ohm_milliohm, ohm->i100_ma, selftest_session.request_id);
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
    diag_out_printf("[TABLE] diff pos=%u old_uid=%s new_uid=%s old=0x%03X new=0x%03X\n",
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
    uint8_t flags = 0u;
    bool can_write = true;
    bool differs = false;
    bool has_preview = false;
    commissioning_result_t build_result = COMMISSIONING_OK;
    const char *table_result_text = "NOT_WRITTEN(?)";

    // Une position seedée sans réponse n'est plus bloquante : elle est absente de
    // la table et devient un motif signalé (ADR-0009 §2).
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        if (uid_position_table[position].position == UID_POSITION_EMPTY) {
            continue;
        }
        if (!selftest_session.received_position[position]) {
            emit_status_position(DIAG_STATUS_BOARD_MISSING, position);
            ++stats.boot_missing;
            flags |= COMMISSIONING_FLAG_MISSING_BOARD;
        }
    }

    if (selftest_session.unknown_board) {
        flags |= COMMISSIONING_FLAG_UNKNOWN_BOARD;
    }
    if (selftest_session.any_non_ok_status) {
        flags |= COMMISSIONING_FLAG_BOARD_NOT_OK;
    }
    if (selftest_session.duplicate_tag) {
        can_write = false;
    }
    if (selftest_session.calibration_mode &&
        selftest_session.raw_received_count < selftest_session.raw_expected_count) {
        flags |= COMMISSIONING_FLAG_DIAG_INCOMPLETE;
    }

    // Seules les cartes qui ont répondu entrent dans le cliché (ADR-0009 §1/§2).
    for (uint8_t position = 0u; position < UID_POSITION_COUNT; ++position) {
        if (uid_position_table[position].position == UID_POSITION_EMPTY ||
            !selftest_session.received_position[position]) {
            continue;
        }
        new_entries[new_count].identity.position = position;
        memcpy(new_entries[new_count].identity.uid.id, selftest_session.uid[position],
               CAN_UID_SIZE);
        // Carte répondante en échec : bitmap inconnu, aucun canal activable —
        // jamais un bitmap partiel deviné.
        new_entries[new_count].occupation_bitmap =
            (selftest_session.status[position].test_status == CAN_SELFTEST_STATUS_OK)
                ? selftest_session.status[position].occupation_bits
                : 0u;
        new_entries[new_count].reserved = 0u;
        if (selftest_session.status[position].fault_bits != 0u) {
            flags |= COMMISSIONING_FLAG_FAULT_DURING_TEST;
        }
        ++new_count;
    }

    // Ancrage grave (ADR-0010 §3) : la table n'est FINAL que si les positions
    // présentes forment exactement 0..K-1. Sinon le bloc grave revient à la
    // position présente la plus basse et toutes les suivantes sont décalées : le
    // motif le signale (invariant 1), rien n'est remappé ni corrigé.
    for (uint16_t i = 0u; i < new_count; ++i) {
        if (new_entries[i].identity.position != (uint8_t)i) {
            flags |= COMMISSIONING_FLAG_ANCHOR_SHIFT;
            break;
        }
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
            flags |= COMMISSIONING_FLAG_NOT_49_KEYS;
            emit_status(DIAG_STATUS_TABLE_PARTIAL);
        }
    } else {
        // Aucune carte complète : la table en flash reste intacte.
        can_write = false;
    }

    if (commissioning.valid) {
        if (commissioning.entry_count != new_count) {
            differs = true;
        }
        // L'état et les motifs font partie du contenu persisté (ADR-0009 §3).
        if (commissioning.table_state != commissioning_table_state_from_flags(flags) ||
            commissioning.table_flags != flags) {
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

    if (!can_write) {
        emit_status(DIAG_STATUS_TABLE_NOT_WRITTEN_INCOMPLETE);
        if (new_count == 0u) {
            table_result_text = "NOT_WRITTEN(aucune_carte)";
        } else if (selftest_session.duplicate_tag) {
            table_result_text = "NOT_WRITTEN(tag_duplique)";
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
        if (commissioning_state_store(&commissioning, new_entries, new_count,
                                      commissioning_table_state_from_flags(flags),
                                      flags) != COMMISSIONING_OK) {
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

    // Diagnostic de bring-up (ADR-0009 §4/§5) : les lignes `delta` des canaux
    // sans IPROPI_OHM, puis la synthèse ohmique par carte.
    if (selftest_session.calibration_mode) {
        emit_missing_ohm_delta_lines();
        emit_selftest_ohm_summaries();
    }

    emit_table_state_line();
    emit_table_state_status();
    emit_all_table_entries();
    emit_map_lines();
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

    // Provient d'une trame CAN (bouton Moteur) : posé AVANT tout refus, pour que
    // même une ligne de refus nomme la bonne source (ADR-0017 décision 5).
    profile_sync_source = "bouton";

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
    if (uid_inflight || control_tx_inflight || profile_sync_active) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        emit_trigger_refused_line("requete de controle deja en cours");
        return;
    }
    if (recent_playing()) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        emit_trigger_refused_line("jeu en cours");
        return;
    }
    // Déclenchement reçu en CAN : « bouton » est un fait, pas une supposition
    // (ADR-0017 décision 5).
    if (!begin_profile_sync(&k_solenoid_profile, PROFILE_SYNC_GOAL_SELFTEST, "bouton")) {
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        emit_trigger_refused_line("echec emission profil CAN");
        return;
    }
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
    case CAN_ID_IPROPI_OHM: {
        can_ipropi_ohm_t ohm;
        result = can_decode_ipropi_ohm(msg->data, dlc, &ohm);
        if (result == CAN_PROTO_OK) {
            on_ipropi_ohm(&ohm);
            return;
        }
        break;
    }
    case CAN_ID_IPROPI_SELFTEST_TRIGGER:
        handle_selftest_trigger(dlc, msg->data);
        return;
    case CAN_ID_PROFILE_STATUS: {
        can_profile_status_t status;
        result = can_decode_profile_status(msg->data, dlc, &status);
        if (result == CAN_PROTO_OK) {
            handle_profile_status(&status);
            return;
        }
        break;
    }
    case CAN_ID_UID_REQUEST:
    case CAN_ID_IPROPI_SELFTEST_REQUEST:
    case CAN_ID_SOLENOID_COMMAND:
    case CAN_ID_PROFILE_BEGIN:
    case CAN_ID_PROFILE_CHUNK:
    case CAN_ID_PROFILE_COMMIT:
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
    profile_active = k_solenoid_profile;
    profile_sync_pending = k_solenoid_profile;
    profile_sync_goal = PROFILE_SYNC_GOAL_SELFTEST;
    local_profile_tag = solenoid_profile_tag(&profile_active);
    profile_sync_active = false;
    profile_sync_transfer_id = 0u;
    profile_sync_next_transfer_id = CAN_PROFILE_TRANSFER_ID_MIN;
    profile_sync_expected = 0u;
    profile_sync_received = 0u;
    profile_sync_failed = false;
    profile_sync_start_ms = 0u;
    memset(profile_seed_online, 0, sizeof(profile_seed_online));
    memset(profile_boot_seen, 0, sizeof(profile_boot_seen));
    memset(profile_card_ack, 0, sizeof(profile_card_ack));
    profile_probe_pending = false;

    table_blank = commissioning_flash_table_is_blank();
    load_result = commissioning_state_load(&commissioning);
    if (load_result == COMMISSIONING_OK) {
        emit_boot_line("loaded", (uint8_t)commissioning.entry_count);
        emit_table_state_line();
        emit_table_state_status();
        emit_all_table_entries();
        boot_card_summary_pending = true;
        if (commissioning.runtime.partial) {
            emit_status(DIAG_STATUS_TABLE_PARTIAL);
        }
        emit_map_lines();
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
        deadline_reached(first_ms, uid_inflight_start_ms, CAN_UID_REQUEST_TIMEOUT_MS)) {
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
        // Fin de la re-requête post-échec (ADR-0017 décision 6) : les annonces
        // suivantes retrouvent le compte rendu de divergence habituel.
        profile_probe_pending = false;
    }

    if (selftest_session.active &&
        ((selftest_session.received_count >= selftest_session.expected_count &&
          (!selftest_session.calibration_mode ||
           (selftest_session.raw_received_count >= selftest_session.raw_expected_count &&
            selftest_session.ohm_received_count >= selftest_session.raw_expected_count))) ||
         deadline_reached(first_ms, selftest_session.start_ms,
                          CAN_LINK_SELFTEST_TIMEOUT_MS))) {
        finish_selftest_session();
    }

    // Synchronisation du profil (ADR-0013 §4) : 0x401 n'est émis que lorsque
    // toutes les cartes présentes ont acquitté le profil.
    if (profile_sync_active) {
        const bool tuning = profile_sync_goal == PROFILE_SYNC_GOAL_TUNING;
        const uint32_t timeout_ms = tuning ? CAN_LINK_PROFILE_TUNING_TIMEOUT_MS
                                           : CAN_LINK_PROFILE_SYNC_TIMEOUT_MS;

        if (profile_sync_failed) {
            emit_status(DIAG_STATUS_PROFILE_SYNC_FAILED);
            ++stats.profile_sync_failed;
            profile_sync_active = false;
            print_profile_sync_cards();
            if (tuning) {
                diag_out_printf("[PROFILE] reglage: ECHEC (refus). Etat des cartes incertain; "
                                "le profil actif reste celui d'avant (p show). 'p send' "
                                "resynchronise\n");
            }
        } else if (profile_sync_received >= profile_sync_expected) {
            profile_sync_active = false;
            profile_active = profile_sync_pending;
            local_profile_tag = solenoid_profile_tag(&profile_active);
            diag_out_printf("[PROFILE] sync ok duree=%lu ms acquittements=%u/%u tag=0x%08lX\n",
                            (unsigned long)profile_sync_elapsed_ms(first_ms),
                            (unsigned)profile_sync_received, (unsigned)profile_sync_expected,
                            (unsigned long)local_profile_tag);
            if (tuning) {
                if (profile_sync_expected == 0u) {
                    diag_out_printf("[PROFILE] reglage: AUCUNE carte attendue (table invalide ou "
                                    "aucune carte en ligne au dernier scan): application NON "
                                    "confirmee\n");
                }
            } else if (!start_selftest_request()) {
                emit_status(DIAG_STATUS_CAN_TX_FAIL);
            } else {
                emit_trigger_line(selftest_session.request_id);
            }
        } else if (deadline_reached(first_ms, profile_sync_start_ms, timeout_ms)) {
            emit_status(DIAG_STATUS_PROFILE_SYNC_FAILED);
            ++stats.profile_sync_failed;
            profile_sync_active = false;
            diag_out_printf("[PROFILE] sync echec duree=%lu ms delai=%lu ms acquittements=%u/%u\n",
                            (unsigned long)profile_sync_elapsed_ms(first_ms),
                            (unsigned long)timeout_ms, (unsigned)profile_sync_received,
                            (unsigned)profile_sync_expected);
            print_profile_sync_cards();
            if (profile_sync_any_no_boot_status()) {
                // Carte présente qui n'a jamais annoncé de PROFILE_STATUS de boot :
                // firmware Moteur antérieur à ADR-0013, seule action = reflasher.
                emit_status(DIAG_STATUS_PROFILE_UNSUPPORTED);
            }
            profile_request_probe();
            if (tuning) {
                diag_out_printf("[PROFILE] reglage: ECHEC (pas d'acquittement). Voir le motif par "
                                "carte ci-dessus; 'p send' resynchronise\n");
            }
        }
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

can_link_request_result_t can_link_request_selftest(void) {
    // Demandé depuis la console : posé avant tout refus (ADR-0017 décision 5).
    profile_sync_source = "console";
    if (selftest_session.active) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        return CAN_LINK_REQUEST_BUSY;
    }
    if (uid_inflight || control_tx_inflight || profile_sync_active) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        return CAN_LINK_REQUEST_BUSY;
    }
    if (recent_playing()) {
        emit_status(DIAG_STATUS_SELFTEST_REFUSED_PLAYING);
        return CAN_LINK_REQUEST_BUSY;
    }
    if (!started ||
        !begin_profile_sync(&k_solenoid_profile, PROFILE_SYNC_GOAL_SELFTEST, "console")) {
        emit_status(DIAG_STATUS_CAN_TX_FAIL);
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    return CAN_LINK_REQUEST_SENT;
}

const solenoid_profile_t *can_link_profile_active(void) {
    return &profile_active;
}

const solenoid_profile_t *can_link_profile_compiled(void) {
    return &k_solenoid_profile;
}

// Réglage à chaud (ADR-0015) : mêmes garde-fous que l'auto-test (aucune requête
// de contrôle en cours, pas de jeu récent), puis diffusion du profil. Le compte
// rendu ([PROFILE] reglage: OK/ECHEC) est émis plus tard par can_link_poll.
can_link_request_result_t can_link_profile_send(const solenoid_profile_t *profile) {
    if (profile == NULL || !started) {
        diag_out_printf("[PROFILE] reglage refuse: CAN non demarre\n");
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    if (profile_sync_active || selftest_session.active || uid_inflight || control_tx_inflight) {
        diag_out_printf("[PROFILE] reglage refuse: operation CAN en cours (attendre la fin, voir [STATUS])\n");
        return CAN_LINK_REQUEST_BUSY;
    }
    if (recent_playing()) {
        diag_out_printf("[PROFILE] reglage refuse: jeu en cours (lacher les touches, attendre 0,5 s)\n");
        return CAN_LINK_REQUEST_BUSY;
    }
    if (!begin_profile_sync(profile, PROFILE_SYNC_GOAL_TUNING, "console")) {
        diag_out_printf("[PROFILE] reglage: echec d'emission CAN\n");
        return CAN_LINK_REQUEST_TX_FAIL;
    }
    return CAN_LINK_REQUEST_SENT;
}

bool can_link_calibration_enabled(void) {
    return calibration_enabled;
}

void can_link_set_calibration_enabled(bool enabled) {
    calibration_enabled = enabled;
}

void can_link_dump_table(void) {
    if (commissioning.valid) {
        emit_table_state_line();
        emit_all_table_entries();
        emit_map_lines();
    } else {
        char detail[32];

        snprintf(detail, sizeof(detail), "reason=%s",
                 commissioning_result_name(commissioning.last_error));
        emit_table_loaded(false, detail);
    }
}

void can_link_dump_status(void) {
    char detail[64];

    snprintf(detail, sizeof(detail), "valid=%s state=%s flags=0x%02X cards=%u selftest=%s calib=%s",
             commissioning.valid ? "oui" : "non",
             commissioning_table_state_name(commissioning.table_state),
             (unsigned)commissioning.table_flags, (unsigned)commissioning.entry_count,
             selftest_session.active ? "en_cours" : "repos", calibration_enabled ? "ON" : "OFF");
    emit_line(detail);
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
