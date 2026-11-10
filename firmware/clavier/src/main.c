#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "can_link.h"
#include "bringup_console.h"
#include "combo.h"
#include "diag_log.h"
#include "diag_out.h"
#include "keycode.h"
#include "keymap.h"
#include "listen_config.h"
#include "matrix_scan.h"
#include "midi.h"
#include "note_state.h"
#include "can_protocol.h"
#include "rec_console.h"
#include "rec_mode.h"
#include "usb_log.h"

typedef struct {
    uint8_t position;
    bool pressed;
} matrix_event_t;

// ADR-0032 : mémoire courte d'une impulsion, pour corréler le LISTEN_PULSE
// (uid, canal, seq) au LISTEN_PULSE_TIMING (uid, seq) et déclencher `e bruit`.
typedef struct {
    bool valid;
    uint16_t uid_tag;
    uint8_t seq;
    uint8_t channel;
    uint8_t key;
    uint16_t base;
    bool split;
} rec_pulse_memory_t;

// Requête de bruit en attente de son LISTEN_NOISE_REPORT.
typedef struct {
    bool valid;
    uint8_t request_id;
    uint8_t neighbor;  // 0 = k-1, 1 = k+1
    uint16_t uid_tag;
    uint8_t channel;
    uint8_t key;
    uint16_t base;
} rec_noise_pending_t;

#define REC_NOISE_PENDING_SLOTS 8u
#define REC_LISTEN_EVENTS_PER_LOOP 16u

typedef struct {
    keycode_state_t keycode;
    note_state_t notes;
    uint32_t last_note_event_ms;
    bool maintenance_mode;
    bool maintenance_active;
    bool maintenance_timed_out;
    uint8_t maintenance_position;
    uint8_t maintenance_midi_note;
    uint32_t maintenance_started_ms;
    // ADR-0026 / ADR-0029 / ADR-0030 : modificateurs maintenus ALT/CLE/TEMP/
    // TEMP_ST/TEMP_OCT, hors keycode_state_t (que UNTRANSPOSE remet à zéro).
    // La dernière direction encore enfoncée gagne, par famille et par côté.
    keycode_modifiers_t modifiers;
    // File d'un lot de scan, vidée après le poll (≤ MATRIX_KEY_COUNT).
    matrix_event_t matrix_events[MATRIX_KEY_COUNT];
    uint8_t matrix_event_count;
    // ADR-0031 : état du moteur de combos de positions.
    combo_state_t combo;
    // ADR-0032 : paramètres d'écoute (k_listen_config, modifiables à chaud par
    // `e`) et état du mode REC.
    listen_params_t listen_params;
    bool rec_params_valid;
    rec_state_t rec;
    rec_mode_t rec_mode;
    // Corrélation impulsion/timing et requêtes `e bruit`.
    rec_pulse_memory_t pulse_memory[COMMISSIONING_KEY_COUNT + 1u];
    rec_noise_pending_t noise_pending[REC_NOISE_PENDING_SLOTS];
    uint8_t noise_pending_next;
    uint8_t noise_request_id;
    // Journal LISTEN_STATUS : au plus 1x/s par carte, immédiat au changement.
    uint32_t listen_status_log_ms[UID_POSITION_COUNT];
    can_listen_state_t listen_status_last[UID_POSITION_COUNT];
    bool listen_status_seen[UID_POSITION_COUNT];
    // Cible de la trace en cours (console `e trace`, lot 3b).
    bool trace_target_valid;
    bool trace_logged;
    uint16_t trace_uid_tag;
    uint8_t trace_channel;
    uint8_t trace_key;
    uint8_t trace_position;
    // Cible du flux Serial Studio (`e stream`) : trames LISTEN_TRACE_* du même
    // canal, sans résumé de trace.
    bool stream_active;
    uint16_t stream_uid_tag;
    uint8_t stream_channel;
    uint8_t stream_key;
    uint8_t stream_position;
    uint16_t stream_evt_delta;  // delta de la dernière impulsion de ce canal
    bool stream_evt_pending;
} clavier_state_t;

static void dispatch_keycode(clavier_state_t *state, uint8_t position,
                             keycode_t keycode, bool pressed);

// État unique, exposé à la console `e` par le pont rec_console.h. main() est
// l'unique écrivain de ce pointeur, avant la boucle.
static clavier_state_t *g_console_state;

// Borne de la coupure EXPLICITE de la poussée maintenance envoyée par le Clavier
// (trame OFF). Couplée à SOLENOID_MAINT_MAX_MS du Moteur (ADR-0012) : elle doit
// rester inférieure ou égale à cette borne locale, qui est la dernière barrière
// du driver.
#define MAINTENANCE_TIMEOUT_MS 20000u

static void emit_diag_line(size_t (*formatter)(char *, size_t, bool), bool value) {
    char line[DIAG_LOG_LINE_MAX];

    if (formatter(line, sizeof(line), value) != 0u) {
        diag_out_printf("%s\n", line);
    }
}

static void emit_status(diag_status_code_t code) {
    char line[DIAG_LOG_LINE_MAX];

    if (diag_format_status_line(line, sizeof(line), code) != 0u) {
        diag_out_printf("%s\n", line);
    }
}

static void emit_mode(bool maintenance_enabled) {
    emit_diag_line(diag_format_mode_line, maintenance_enabled);
}

static void silence_play_notes(clavier_state_t *state) {
    if (state == NULL) {
        return;
    }

    for (uint8_t position = 1u; position <= KEYMAP_POSITION_COUNT; ++position) {
        if (!state->notes.active[position]) {
            continue;
        }
        (void)can_link_note_off(state->notes.midi_note[position]);
        (void)midi_note_off(state->notes.midi_note[position]);
    }
    note_state_init(&state->notes);
}

// ---------------------------------------------------------------------------
//  Mode REC (ADR-0032 §7, §8, §11)
// ---------------------------------------------------------------------------

static const char *listen_state_name(can_listen_state_t state) {
    switch (state) {
        case CAN_LISTEN_STATE_IDLE:
            return "IDLE";
        case CAN_LISTEN_STATE_LISTENING:
            return "LISTENING";
        case CAN_LISTEN_STATE_TRACE:
            return "TRACE";
        case CAN_LISTEN_STATE_STREAM:
            return "STREAM";
        case CAN_LISTEN_STATE_PARAM_MISMATCH:
            return "PARAM_MISMATCH";
        case CAN_LISTEN_STATE_BUSY_SELFTEST:
            return "BUSY_SELFTEST";
        case CAN_LISTEN_STATE_UNAVAILABLE_SLEEP:
            return "UNAVAILABLE_SLEEP";
        case CAN_LISTEN_STATE_TRACE_TIMEOUT:
            return "TRACE_TIMEOUT";
        case CAN_LISTEN_STATE_FAULT:
            return "FAULT";
        default:
            return "?";
    }
}

static void format_note_label(uint8_t midi_note, char *out, size_t capacity) {
    if (out == NULL || capacity == 0u) {
        return;
    }
    if (!rec_mode_note_name(midi_note, out, capacity)) {
        (void)snprintf(out, capacity, "%u", (unsigned)midi_note);
    }
}

// Coupe les solénoïdes encore pilotés AVANT d'ouvrir l'écoute : aucune trame
// 0x100 n'est émise ensuite pendant REC.
static void cut_active_solenoids(clavier_state_t *state) {
    for (uint8_t position = 1u; position <= KEYMAP_POSITION_COUNT; ++position) {
        if (state->notes.active[position]) {
            (void)can_link_note_off(state->notes.midi_note[position]);
        }
    }
    if (state->combo.output_note_active) {
        (void)can_link_note_off(state->combo.output_note);
    }
}

static void clear_listen_tracking(clavier_state_t *state) {
    memset(state->pulse_memory, 0, sizeof(state->pulse_memory));
    memset(state->noise_pending, 0, sizeof(state->noise_pending));
    state->noise_pending_next = 0u;
    state->noise_request_id = 0u;
    state->trace_target_valid = false;
    state->trace_logged = false;
    state->stream_active = false;
    state->stream_evt_pending = false;
}

static void enter_rec_mode(clavier_state_t *state, rec_mode_t mode) {
    rec_off_list_t offs;
    rec_refusal_t refusal;

    if (state == NULL) {
        return;
    }
    if (!state->rec_params_valid) {
        diag_out_printf_class(DIAG_CLASS_ALWAYS,
                              "[REC] refus config invalide (voir [LISTEN] config invalide)\n");
        return;
    }

    // Réamorce le compteur matrice depuis l'état physique : la sortie de REC
    // l'a vidé, mais les touches encore tenues restent actives côté note_state.
    memset(state->rec.matrix_ref, 0, sizeof(state->rec.matrix_ref));
    for (uint8_t position = 1u; position <= KEYMAP_POSITION_COUNT; ++position) {
        if (state->notes.active[position]) {
            rec_mode_seed_matrix_note(&state->rec, state->notes.midi_note[position]);
        }
    }
    if (state->combo.output_note_active) {
        rec_mode_seed_matrix_note(&state->rec, state->combo.output_note);
    }

    refusal = rec_mode_enter(&state->rec, mode, can_link_has_valid_table(),
                             can_link_selftest_active(), can_link_profile_sync_active(),
                             state->maintenance_mode, &offs);
    if (refusal != REC_OK) {
        diag_out_printf_class(DIAG_CLASS_ALWAYS, "[REC] refus %s\n",
                              rec_refusal_name(refusal));
        return;
    }

    // Entrée : coupure des solénoïdes actifs. En REC_MUTE seulement, la matrice
    // est aussi rendue muette (Note Off MIDI des notes qu'elle tenait).
    cut_active_solenoids(state);
    if (mode == REC_MODE_MUTE) {
        for (uint8_t index = 0u; index < offs.count; ++index) {
            (void)midi_note_off(offs.note[index]);
        }
    }

    if (!can_link_listen_start(&state->listen_params, CAN_LISTEN_MODE_LISTEN, 0u, 0u)) {
        // Jamais de mode REC partiel : on rend l'état et on le dit.
        rec_mode_exit(&state->rec, &offs);
        diag_out_printf_class(DIAG_CLASS_ALWAYS, "[REC] demarrage impossible\n");
        return;
    }

    clear_listen_tracking(state);
    memset(state->listen_status_seen, 0, sizeof(state->listen_status_seen));
    state->rec_mode = mode;
    diag_out_printf_class(DIAG_CLASS_ALWAYS, "[REC] ON %s\n", rec_mode_name(mode));
}

static void leave_rec_mode(clavier_state_t *state) {
    rec_off_list_t offs;

    if (state == NULL || state->rec_mode == REC_MODE_OFF) {
        return;
    }
    rec_mode_exit(&state->rec, &offs);
    for (uint8_t index = 0u; index < offs.count; ++index) {
        (void)midi_note_off(offs.note[index]);
    }
    (void)can_link_listen_stop();
    clear_listen_tracking(state);
    state->rec_mode = REC_MODE_OFF;
    diag_out_printf_class(DIAG_CLASS_ALWAYS, "[REC] OFF\n");
}

// ---------------------------------------------------------------------------
//  Pont console `e` (rec_console.h)
// ---------------------------------------------------------------------------

listen_params_t *rec_console_params(void) {
    return (g_console_state != NULL) ? &g_console_state->listen_params : NULL;
}

const listen_params_t *rec_console_compiled_params(void) {
    return &k_listen_config;
}

rec_state_t *rec_console_state(void) {
    return (g_console_state != NULL) ? &g_console_state->rec : NULL;
}

const commissioning_runtime_t *rec_console_runtime(void) {
    return can_link_commissioning_runtime();
}

bool rec_console_table_valid(void) {
    return can_link_has_valid_table();
}

bool rec_console_params_valid(void) {
    return (g_console_state != NULL) && g_console_state->rec_params_valid;
}

rec_mode_t rec_console_mode(void) {
    return (g_console_state != NULL) ? g_console_state->rec_mode : REC_MODE_OFF;
}

bool rec_console_enter_sole(void) {
    if (g_console_state == NULL || g_console_state->rec_mode != REC_MODE_OFF) {
        return false;
    }
    enter_rec_mode(g_console_state, REC_MODE_SOLE);
    return g_console_state->rec_mode == REC_MODE_SOLE;
}

bool rec_console_raz(void) {
    if (g_console_state == NULL) {
        return false;
    }
    rec_mode_raz(&g_console_state->rec);
    return true;
}

bool rec_console_send_params(void) {
    clavier_state_t *state = g_console_state;
    uint8_t bad_id = 0u;

    if (state == NULL) {
        return false;
    }
    if (listen_params_validate(&state->listen_params, &bad_id) != LISTEN_PARAMS_OK) {
        state->rec_params_valid = false;
        return false;
    }
    state->rec_params_valid = true;
    if (state->rec_mode != REC_MODE_OFF) {
        // Le Moteur refuse LISTEN_PARAM hors mode OFF : stop puis start, qui
        // rediffuse le jeu complet et le CRC avant de repasser en écoute.
        (void)can_link_listen_stop();
        return can_link_listen_start(&state->listen_params, CAN_LISTEN_MODE_LISTEN, 0u, 0u);
    }
    return can_link_listen_send_params(&state->listen_params);
}

bool rec_console_trace(uint8_t midi_note, const commissioning_note_target_t *target) {
    clavier_state_t *state = g_console_state;

    (void)midi_note;  // le libellé est journalisé par main.c à la réception
    if (state == NULL || target == NULL || !can_link_has_valid_table()) {
        return false;
    }
    state->stream_active = false;
    state->trace_target_valid = true;
    state->trace_logged = false;
    state->trace_uid_tag = target->uid_tag;
    state->trace_channel = target->channel;
    state->trace_key = target->key;
    state->trace_position = target->position;
    rec_mode_trace_reset(&state->rec);
    // Séparateur Serial Studio avant une trace (port 2 uniquement, no-op OFF).
    serial_studio_printf("/*-1,0*/\n");
    if (!can_link_listen_trace(&state->listen_params, target->uid_tag, target->channel)) {
        state->trace_target_valid = false;
        return false;
    }
    return true;
}

bool rec_console_stream(const commissioning_note_target_t *target) {
    clavier_state_t *state = g_console_state;

    if (state == NULL || target == NULL || !can_link_has_valid_table()) {
        return false;
    }
    state->trace_target_valid = false;
    state->trace_logged = false;
    state->stream_active = true;
    state->stream_uid_tag = target->uid_tag;
    state->stream_channel = target->channel;
    state->stream_key = target->key;
    state->stream_position = target->position;
    state->stream_evt_pending = false;
    if (!can_link_listen_stream(&state->listen_params, target->uid_tag, target->channel)) {
        state->stream_active = false;
        return false;
    }
    return true;
}

bool rec_console_stream_off(void) {
    if (g_console_state == NULL) {
        return false;
    }
    g_console_state->stream_active = false;
    g_console_state->stream_evt_pending = false;
    return can_link_listen_stream_off();
}

// Émission d'une note matrice : hors REC, chemin historique (CAN + MIDI) ;
// en REC, MIDI seul (jamais de 0x100) et REC_MUTE ne retourne aucune action.
static void emit_matrix_note(clavier_state_t *state, uint8_t position, uint8_t midi_note,
                             bool pressed) {
    if (state == NULL) {
        return;
    }
    if (state->rec_mode == REC_MODE_OFF) {
        if (pressed) {
            (void)can_link_note_on(midi_note, MIDI_NOTE_VELOCITY);
            if (!midi_note_on(midi_note, MIDI_NOTE_VELOCITY)) {
                diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                      "[KEY] pos=%u note=%u state=pressed usb_midi=unavailable\n",
                                      position, midi_note);
            }
        } else {
            (void)can_link_note_off(midi_note);
            if (!midi_note_off(midi_note)) {
                diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                      "[KEY] pos=%u note=%u state=released usb_midi=unavailable\n",
                                      position, midi_note);
            }
        }
        return;
    }

    {
        const rec_action_t action =
            rec_mode_matrix_event(&state->rec, state->rec_mode, midi_note, pressed);

        if (action.emit_on && !midi_note_on(action.midi_note, action.velocity)) {
            diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                  "[KEY] pos=%u note=%u state=pressed usb_midi=unavailable\n",
                                  position, action.midi_note);
        }
        if (action.emit_off && !midi_note_off(action.midi_note)) {
            diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                  "[KEY] pos=%u note=%u state=released usb_midi=unavailable\n",
                                  position, action.midi_note);
        }
    }
}

static void stop_maintenance_push(clavier_state_t *state, bool timed_out, bool emit_log) {
    if (state == NULL || !state->maintenance_active) {
        return;
    }

    if (emit_log) {
        (void)can_link_maintenance_push_stop(state->maintenance_midi_note, timed_out);
    } else {
        (void)can_link_note_off(state->maintenance_midi_note);
    }
    state->maintenance_active = false;
    state->maintenance_timed_out = false;
    state->maintenance_position = 0u;
    state->maintenance_midi_note = 0u;
    state->maintenance_started_ms = 0u;
}

static void reset_maintenance_state(clavier_state_t *state) {
    if (state == NULL) {
        return;
    }

    state->maintenance_active = false;
    state->maintenance_timed_out = false;
    state->maintenance_position = 0u;
    state->maintenance_midi_note = 0u;
    state->maintenance_started_ms = 0u;
}

// ADR-0031 : relâche une sortie de combo encore tenue avant de changer de mode
// (note ou keycode momentané), pour ne pas laisser de note bloquée.
static void release_combo_output(clavier_state_t *state) {
    combo_action_t action;

    if (state == NULL) {
        return;
    }
    if (combo_release_output(&state->combo, &action, 1u) > 0u) {
        dispatch_keycode(state, 0u, action.keycode, false);
    }
}

static void enter_maintenance(clavier_state_t *state) {
    if (state == NULL) {
        return;
    }
    release_combo_output(state);
    combo_reset(&state->combo);
    silence_play_notes(state);
    stop_maintenance_push(state, false, false);
    reset_maintenance_state(state);
    state->maintenance_mode = true;
    emit_mode(true);
}

static void leave_maintenance(clavier_state_t *state) {
    if (state == NULL) {
        return;
    }
    stop_maintenance_push(state, false, false);
    release_combo_output(state);
    combo_reset(&state->combo);
    silence_play_notes(state);
    reset_maintenance_state(state);
    state->maintenance_mode = false;
    emit_mode(false);
}

static void handle_maintenance_toggle(clavier_state_t *state) {
    if (state == NULL) {
        return;
    }
    // ADR-0032 §7 : la MAINTENANCE est refusée en REC (comme l'auto-test).
    if (state->rec_mode != REC_MODE_OFF) {
        diag_out_printf_class(DIAG_CLASS_ALWAYS, "[REC] refus REC_MAINTENANCE\n");
        return;
    }
    if (!state->maintenance_mode) {
        if (!can_link_has_valid_table()) {
            emit_status(DIAG_STATUS_MAINTENANCE_NO_TABLE);
            return;
        }
        if (can_link_selftest_active()) {
            emit_status(DIAG_STATUS_BUSY);
            return;
        }
        enter_maintenance(state);
        return;
    }
    leave_maintenance(state);
}

static void handle_maintenance_transition(clavier_state_t *state, uint8_t position, bool pressed,
                                          keycode_t keycode) {
    if (state == NULL) {
        return;
    }
    if (keycode.kind == KEYCODE_MAINTENANCE_TOGGLE) {
        if (pressed) {
            handle_maintenance_toggle(state);
        }
        return;
    }
    if (keycode.kind != KEYCODE_NOTE) {
        return;
    }

    if (pressed) {
        uint8_t midi_note;

        if (state->maintenance_active) {
            emit_status(DIAG_STATUS_MAINTENANCE_BUSY);
            return;
        }
        // Maintenance : les keycodes momentanés sont suivis mais sans effet
        // (ADR-0030 §7), d'où momentary_delta = 0.
        if (!note_state_press(&state->notes, &state->keycode, position, keycode.value,
                              -1, 0, &midi_note)) {
            diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                  "[KEY] pos=%u error=maintenance_resolution\n", position);
            return;
        }
        if (!can_link_maintenance_push_start(midi_note)) {
            note_state_release(&state->notes, position, &midi_note);
            return;
        }
        state->maintenance_active = true;
        state->maintenance_timed_out = false;
        state->maintenance_position = position;
        state->maintenance_midi_note = midi_note;
        state->maintenance_started_ms = to_ms_since_boot(get_absolute_time());
        state->last_note_event_ms = state->maintenance_started_ms;
        return;
    }

    if (position != state->maintenance_position) {
        uint8_t ignored_note;
        (void)note_state_release(&state->notes, position, &ignored_note);
        return;
    }

    {
        uint8_t midi_note;

        (void)note_state_release(&state->notes, position, &midi_note);
        if (state->maintenance_active) {
            (void)can_link_maintenance_push_stop(midi_note, false);
        }
        reset_maintenance_state(state);
        state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
    }
}

// ADR-0026 : le handler matriciel ne fait que mettre l'événement en file. Le
// contrat matrix_scan_event_handler_t reste inchangé ; le lot est traité après
// le poll dans un ordre déterministe (voir process_event_queue).
static void handle_matrix_transition(uint8_t position, bool pressed, void *context) {
    clavier_state_t *state = context;

    if (state == NULL || state->matrix_event_count >= MATRIX_KEY_COUNT) {
        return;
    }

    state->matrix_events[state->matrix_event_count].position = position;
    state->matrix_events[state->matrix_event_count].pressed = pressed;
    ++state->matrix_event_count;
}

// Direction active d'une famille à deux directions (ALT/CLE/TEMP).
static bool active_modifier(const clavier_state_t *state, uint8_t family, uint8_t *direction) {
    if (state == NULL) {
        return false;
    }

    return keycode_modifiers_active(&state->modifiers, family, 0u, 2u, direction);
}

// Classe de modificateur maintenu d'un keycode, s'il en est un (ADR-0029,
// ADR-0030).
static bool keycode_modifier_family(keycode_kind_t kind, uint8_t *family) {
    if (family == NULL) {
        return false;
    }

    switch (kind) {
        case KEYCODE_ALTERATION:
            *family = KEYCODE_MOD_FAMILY_ALT;
            return true;
        case KEYCODE_CLE:
            *family = KEYCODE_MOD_FAMILY_CLE;
            return true;
        case KEYCODE_TEMP:
            *family = KEYCODE_MOD_FAMILY_TEMP;
            return true;
        case KEYCODE_TEMP_TRANSPOSE_ST:
            *family = KEYCODE_MOD_FAMILY_TEMP_ST;
            return true;
        case KEYCODE_TEMP_TRANSPOSE_OCT:
            *family = KEYCODE_MOD_FAMILY_TEMP_OCT;
            return true;
        default:
            return false;
    }
}

// ADR-0031 : point de dispatch unique d'un keycode (événement physique ou
// action de combo). `position == 0` identifie une sortie de combo, sans
// position physique.
static void dispatch_keycode(clavier_state_t *state, uint8_t position,
                             keycode_t keycode, bool pressed) {
    if (state == NULL) {
        return;
    }

    if (keycode.kind == KEYCODE_NONE) {
#if CLAVIER_DIAG_TRACE
        diag_out_printf_class(DIAG_CLASS_TRACE,
                              "[KEY] pos=%u behavior=unused state=%s\n", position,
                              pressed ? "pressed" : "released");
#endif
        return;
    }

    // ADR-0031 : une sortie de combo note est littérale (aucune armure,
    // aucune transposition). En maintenance, elle suit le chemin de poussée
    // maintenance comme une note physique, mais sans note_state (position 0).
    if (position == 0u && keycode.kind == KEYCODE_NOTE) {
        if (!state->maintenance_mode) {
            // ADR-0032 : en REC, une sortie de combo est une note matrice comme
            // une autre (MIDI seul, muette en REC_MUTE).
            state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
            emit_matrix_note(state, 0u, keycode.value, pressed);
            return;
        }

        if (pressed) {
            if (state->maintenance_active) {
                emit_status(DIAG_STATUS_MAINTENANCE_BUSY);
                return;
            }
            if (!can_link_maintenance_push_start(keycode.value)) {
                return;
            }
            state->maintenance_active = true;
            state->maintenance_timed_out = false;
            state->maintenance_position = 0u;
            state->maintenance_midi_note = keycode.value;
            state->maintenance_started_ms = to_ms_since_boot(get_absolute_time());
            state->last_note_event_ms = state->maintenance_started_ms;
        } else if (state->maintenance_active && state->maintenance_position == 0u) {
            (void)can_link_maintenance_push_stop(state->maintenance_midi_note, false);
            reset_maintenance_state(state);
            state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
        }
        return;
    }

    // ADR-0026 / ADR-0029 : les modificateurs ALT/CLE/TEMP sont suivis dans
    // tous les modes, mais aucune action n'a lieu en mode maintenance.
    {
        uint8_t modifier_family = 0u;
        if (keycode_modifier_family(keycode.kind, &modifier_family)) {
            keycode_modifiers_update(&state->modifiers, modifier_family, keycode.value, pressed);
#if CLAVIER_DIAG_TRACE
            diag_out_printf_class(DIAG_CLASS_TRACE,
                                  "[KEY] pos=%u behavior=%s event=%s\n", position,
                                  keycode_name(keycode), pressed ? "pressed" : "released");
#endif
            return;
        }
    }

    if (state->maintenance_mode) {
        handle_maintenance_transition(state, position, pressed, keycode);
        return;
    }

    if (keycode.kind == KEYCODE_NOTE) {
        uint8_t midi_note;

        if (pressed) {
            uint8_t alt_direction = 0u;
            uint8_t cle_direction = 0u;
            uint8_t temp_direction_raw = 0u;
            const bool alt_active = active_modifier(state, KEYCODE_MOD_FAMILY_ALT, &alt_direction);
            const bool cle_active = active_modifier(state, KEYCODE_MOD_FAMILY_CLE, &cle_direction);
            const bool temp_active = active_modifier(state, KEYCODE_MOD_FAMILY_TEMP, &temp_direction_raw);
            const int8_t temp_direction = temp_active ? (int8_t)temp_direction_raw : -1;
            const int16_t momentary_delta = keycode_momentary_delta(&state->modifiers, position);
            bool skip_alt = false;

            // ADR-0029 §3 : l'armure est posée AVANT la résolution de la note
            // tonique (une tonique n'est jamais altérée par sa propre armure) ;
            // CLE l'emporte sur ALT pour cet appui si la touche est naturelle.
            if (cle_active) {
                bool applied = false;
                const keycode_cle_t mode = (cle_direction == 0u) ? KEYCODE_CLE_MINOR
                                                                 : KEYCODE_CLE_MAJOR;
                if (!keycode_armure_apply(&state->keycode, mode, keycode.value, &applied)) {
                    diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                          "[KEY] pos=%u error=armure_resolution\n", position);
                    return;
                }
#if CLAVIER_DIAG_TRACE
                diag_out_printf_class(DIAG_CLASS_TRACE,
                                      "[KEY] pos=%u cle=%s tonique=%u applied=%d\n",
                                      position,
                                      keycode_name((keycode_t){KEYCODE_CLE, cle_direction}),
                                      keycode.value, applied ? 1 : 0);
#endif
                skip_alt = applied;
            }

            // ADR-0026 §3 : la bascule ALT a lieu AVANT la résolution de la note.
            if (!skip_alt && alt_active) {
                uint8_t root_index = 0u;
                uint8_t new_state = 0u;
                if (!keycode_alteration_toggle(&state->keycode, alt_direction, keycode.value,
                                               &root_index, &new_state)) {
                    diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                          "[KEY] pos=%u error=alteration_resolution\n", position);
                    return;
                }
#if CLAVIER_DIAG_TRACE
                diag_out_printf_class(DIAG_CLASS_TRACE,
                                      "[KEY] pos=%u mod=%s root=%u state=%u\n",
                                      position,
                                      keycode_name(KEYCODE_ALTERATION_VALUE(alt_direction)),
                                      root_index, new_state);
#endif
            }

#if CLAVIER_DIAG_TRACE
            if (temp_active) {
                const int16_t before = (int16_t)keycode.value +
                    keycode_alteration_for_note(&state->keycode, keycode.value);
                const int16_t after = before +
                    keycode_temp_delta((keycode_temp_t)temp_direction, before);
                diag_out_printf_class(DIAG_CLASS_TRACE,
                                      "[KEY] pos=%u temp=%s avant=%d apres=%d\n",
                                      position, (temp_direction_raw == 0u) ? "TEMP_b" : "TEMP_d",
                                      (int)before, (int)after);
            }
            {
                const uint8_t first_direction =
                    keycode_position_is_left(position) ? 0u : 2u;
                uint8_t st_direction = 0u;
                uint8_t oct_direction = 0u;
                const bool st_active = keycode_modifiers_active(
                    &state->modifiers, KEYCODE_MOD_FAMILY_TEMP_ST, first_direction, 2u,
                    &st_direction);
                const bool oct_active = keycode_modifiers_active(
                    &state->modifiers, KEYCODE_MOD_FAMILY_TEMP_OCT, first_direction, 2u,
                    &oct_direction);
                diag_out_printf_class(DIAG_CLASS_TRACE,
                                      "[KEY] pos=%u temp_st=%s temp_oct=%s delta=%d\n",
                                      position,
                                      st_active
                                          ? keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_ST,
                                                                     st_direction})
                                          : "off",
                                      oct_active
                                          ? keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_OCT,
                                                                     oct_direction})
                                          : "off",
                                      (int)momentary_delta);
            }
#endif

            if (!note_state_press(&state->notes, &state->keycode, position,
                                  keycode.value, temp_direction, momentary_delta, &midi_note)) {
                diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                      "[KEY] pos=%u error=note_resolution\n", position);
                return;
            }
            state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
            emit_matrix_note(state, position, midi_note, true);
        } else if (note_state_release(&state->notes, position, &midi_note)) {
            state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
            emit_matrix_note(state, position, midi_note, false);
        }
        return;
    }

    if (keycode.kind == KEYCODE_GET_UID) {
        // ADR-0003 : demande d'UID Moteur, à l'appui seulement (pas d'état dans keycode_state_t).
        if (pressed) {
            const can_link_request_result_t result = can_link_request_uid();
            diag_out_printf_class(DIAG_CLASS_BRINGUP,
                                  "[KEY] pos=%u action=get_uid result=%d (0=sent 1=busy 2=tx_fail)\n",
                                  position, (int)result);
        }
        return;
    }

    // ADR-0006 §2 / ADR-0012 : bascule du mode maintenance, à l'appui seulement.
    if (keycode.kind == KEYCODE_MAINTENANCE_TOGGLE) {
        if (pressed) {
            handle_maintenance_toggle(state);
        }
        return;
    }

    // ADR-0032 §7 : bascule REC. Hors REC, REC_SOLE ou REC_MUTE entre ; en REC,
    // l'une ou l'autre des deux touches fait sortir.
    if (keycode.kind == KEYCODE_REC_SOLE || keycode.kind == KEYCODE_REC_MUTE) {
        if (pressed) {
            if (state->rec_mode != REC_MODE_OFF) {
                leave_rec_mode(state);
            } else {
                enter_rec_mode(state, (keycode.kind == KEYCODE_REC_SOLE) ? REC_MODE_SOLE
                                                                         : REC_MODE_MUTE);
            }
        }
        return;
    }

    if (pressed) {
        keycode_press(&state->keycode, keycode);
    }
#if CLAVIER_DIAG_TRACE
    diag_out_printf_class(DIAG_CLASS_TRACE,
                          "[KEY] pos=%u behavior=%s event=%s\n", position,
                          keycode_name(keycode), pressed ? "pressed" : "released");
#endif
}

// Événement physique hors combo : résolution habituelle par le keymap.
static void process_matrix_event(clavier_state_t *state, uint8_t position, bool pressed) {
    dispatch_keycode(state, position, keymap_keycode_for_position(position), pressed);
}

// Exécute les actions produites par le moteur de combos dans l'ordre reçu.
static void dispatch_combo_actions(clavier_state_t *state, const combo_action_t *actions,
                                   uint8_t count) {
    for (uint8_t index = 0u; index < count; ++index) {
        switch (actions[index].kind) {
            case COMBO_ACTION_PENDING_PRESS:
                dispatch_keycode(state, actions[index].position,
                                 keymap_keycode_for_position(actions[index].position), true);
                break;
            case COMBO_ACTION_OUTPUT_PRESS:
                dispatch_keycode(state, 0u, actions[index].keycode, true);
                break;
            case COMBO_ACTION_OUTPUT_RELEASE:
                dispatch_keycode(state, 0u, actions[index].keycode, false);
                break;
            default:
                break;
        }
    }
}

// Catégorie de tri d'un événement dans un lot (ADR-0026 §5, ADR-0029 §6) :
// 0 = appui de modificateur (ALT/CLE/TEMP), 1 = autre appui, 2 = relâchement.
static bool matrix_event_in_pass(const clavier_state_t *state, uint8_t index, uint8_t pass) {
    const matrix_event_t *event = &state->matrix_events[index];
    uint8_t family;

    if (pass == 0u) {
        return event->pressed &&
               keycode_modifier_family(keymap_keycode_for_position(event->position).kind, &family);
    }
    if (pass == 1u) {
        return event->pressed &&
               !keycode_modifier_family(keymap_keycode_for_position(event->position).kind, &family);
    }
    return !event->pressed;
}

// Vide la file du lot : modificateurs, autres appuis, relâchements, chacun par
// position croissante. Un modificateur relâché dans le lot compte donc encore
// comme maintenu pour les notes appuyées dans ce même lot. Les positions
// membres d'un combo passent par le moteur (ADR-0031) ; les autres suivent le
// chemin immédiat. L'expiration des fenêtres est vérifiée après la passe 2.
static void process_event_queue(clavier_state_t *state, uint32_t now_ms) {
    combo_action_t combo_actions[COMBO_POSITION_SLOTS];
    const uint8_t combo_action_capacity = (uint8_t)COMBO_POSITION_SLOTS;

    if (state == NULL) {
        return;
    }

    for (uint8_t pass = 0u; pass < 3u; ++pass) {
        for (uint8_t position = 1u; position <= KEYMAP_POSITION_COUNT; ++position) {
            for (uint8_t index = 0u; index < state->matrix_event_count; ++index) {
                const matrix_event_t *event = &state->matrix_events[index];

                if (event->position != position || !matrix_event_in_pass(state, index, pass)) {
                    continue;
                }

                if (!keymap_combo_is_member(position)) {
                    process_matrix_event(state, position, event->pressed);
                    continue;
                }

                if (event->pressed) {
                    const uint8_t written = combo_on_press(
                        &state->combo, position, now_ms, combo_actions,
                        combo_action_capacity);
                    dispatch_combo_actions(state, combo_actions, written);
                } else {
                    const bool was_armed =
                        combo_position_is_armed(&state->combo, position);
                    const uint8_t written = combo_on_release(
                        &state->combo, position, combo_actions, combo_action_capacity);

                    dispatch_combo_actions(state, combo_actions, written);
                    // Un relâchement de sortie de combo n'est pas un
                    // relâchement physique ; les autres le sont.
                    if (!was_armed) {
                        process_matrix_event(state, position, false);
                    }
                }
            }
        }
    }

    {
        const uint8_t written = combo_expire(&state->combo, now_ms, combo_actions,
                                             combo_action_capacity);
        dispatch_combo_actions(state, combo_actions, written);
    }

    state->matrix_event_count = 0u;
}

// ADR-0031 §5 : vérification au démarrage de la table livrée. Aucune
// correction, aucune écriture : seulement un message pour chaque entrée
// invalide. Une table vide ne produit aucun message.
static void validate_combo_table(void) {
    const uint8_t combo_count = keymap_combo_count();

    for (uint8_t combo_index = 0u; combo_index < combo_count; ++combo_index) {
        const keymap_combo_t *combo = keymap_combo_at(combo_index);
        const char *reason = NULL;

        if (combo == NULL || combo->positions == NULL) {
            reason = "null_positions";
        } else if (combo->count < 2u) {
            reason = "count_lt_2";
        } else if (combo->count > COMBO_MAX_POSITIONS) {
            reason = "count_gt_max";
        } else if (combo->timeout_ms == 0u) {
            reason = "timeout_zero";
        } else {
            for (uint8_t member = 0u; member < combo->count; ++member) {
                const uint8_t position = combo->positions[member];

                if (position == 0u || position > KEYMAP_POSITION_COUNT) {
                    reason = "position_out_of_range";
                    break;
                }
            }
            if (reason == NULL) {
                for (uint8_t first = 0u; first < combo->count && reason == NULL; ++first) {
                    for (uint8_t second = (uint8_t)(first + 1u); second < combo->count;
                         ++second) {
                        if (combo->positions[first] == combo->positions[second]) {
                            reason = "duplicate_position";
                            break;
                        }
                    }
                }
            }
        }

        if (reason == NULL) {
            const uint16_t binding = combo->binding;
            const keycode_t keycode = {
                .kind = (keycode_kind_t)(binding >> 8u),
                .value = (uint8_t)binding,
            };

            if (keycode.kind == KEYCODE_NOTE && keycode.value > 127u) {
                reason = "note_out_of_range";
            }
        }

        if (reason != NULL) {
            diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                  "[COMBO] table error index=%u reason=%s\n",
                                  combo_index, reason);
        }
    }
}

// ---------------------------------------------------------------------------
//  Traitement des événements LISTEN_* (ADR-0032)
// ---------------------------------------------------------------------------

static void handle_listen_pulse(clavier_state_t *state, const can_listen_pulse_t *pulse) {
    const commissioning_runtime_t *runtime = can_link_commissioning_runtime();
    rec_pulse_memory_t memory;
    char label[16];
    rec_action_t action;
    uint8_t key;
    uint8_t position;
    uint16_t delta;

    if (runtime == NULL ||
        !rec_mode_lookup_uid_channel(runtime, pulse->uid_tag, pulse->channel, &key)) {
        diag_out_printf_class(DIAG_CLASS_ALWAYS,
                              "[REC] canal non attribue uid=0x%04X ch=%u\n",
                              (unsigned)pulse->uid_tag, (unsigned)pulse->channel);
        return;
    }
    position = runtime->notes[key - COMMISSIONING_KEY_MIN].position;
    delta = (pulse->peak >= pulse->base) ? (uint16_t)(pulse->peak - pulse->base) : 0u;
    format_note_label(commissioning_key_to_midi_note(key), label, sizeof(label));

    action = rec_mode_captured_event(&state->rec, state->rec_mode, key, delta);
    if (action.emit_on) {
        (void)midi_note_on(action.midi_note, action.velocity);
    }
    if (action.emit_off) {
        (void)midi_note_off(action.midi_note);
    }
    diag_out_printf_class(DIAG_CLASS_ALWAYS,
                          "[LISTEN] pos=%u ch=%u touche=%s min=%u max=%u delta=%u -> %s\n",
                          (unsigned)position, (unsigned)pulse->channel, label,
                          (unsigned)pulse->base, (unsigned)pulse->peak, (unsigned)delta,
                          state->rec.key_down[key] != 0u ? "ON" : "OFF");

    // Mémorise l'impulsion pour le LISTEN_PULSE_TIMING (même uid, même seq).
    memory.valid = true;
    memory.uid_tag = pulse->uid_tag;
    memory.seq = pulse->seq;
    memory.channel = pulse->channel;
    memory.key = key;
    memory.base = pulse->base;
    memory.split = pulse->split;
    state->pulse_memory[key] = memory;

    // `evt` du flux Serial Studio : delta de la dernière impulsion du canal,
    // consommé par l'échantillon suivant puis remis à zéro.
    if (state->stream_active && key == state->stream_key) {
        state->stream_evt_delta = delta;
        state->stream_evt_pending = true;
    }
}

static void queue_noise_query(clavier_state_t *state, uint8_t key, int8_t delta,
                              uint16_t base, uint16_t lookback_ms) {
    const commissioning_runtime_t *runtime = can_link_commissioning_runtime();
    commissioning_note_target_t target;
    rec_noise_pending_t *slot = NULL;

    if (runtime == NULL || !rec_mode_lookup_neighbor(runtime, key, delta, &target)) {
        diag_out_printf_class(DIAG_CLASS_ALWAYS, "[BRUIT] k%+d : non attribuée\n", (int)delta);
        return;
    }
    for (uint8_t index = 0u; index < REC_NOISE_PENDING_SLOTS; ++index) {
        if (!state->noise_pending[index].valid) {
            slot = &state->noise_pending[index];
            break;
        }
    }
    if (slot == NULL) {
        slot = &state->noise_pending[state->noise_pending_next];
        state->noise_pending_next =
            (uint8_t)((state->noise_pending_next + 1u) % REC_NOISE_PENDING_SLOTS);
    }

    state->noise_request_id =
        (state->noise_request_id >= 127u) ? 1u : (uint8_t)(state->noise_request_id + 1u);
    slot->valid = false;
    slot->request_id = state->noise_request_id;
    slot->neighbor = (delta < 0) ? 0u : 1u;
    slot->uid_tag = target.uid_tag;
    slot->channel = target.channel;
    slot->key = target.key;
    slot->base = base;
    if (can_link_listen_noise_query(target.uid_tag, target.channel, slot->request_id,
                                    lookback_ms)) {
        slot->valid = true;
    }
}

static void handle_listen_timing(clavier_state_t *state,
                                 const can_listen_pulse_timing_t *timing) {
    for (uint8_t key = COMMISSIONING_KEY_MIN; key <= COMMISSIONING_KEY_MAX; ++key) {
        rec_pulse_memory_t *memory = &state->pulse_memory[key];
        uint32_t t_peak;
        uint32_t t_rest;
        uint32_t total;
        char label[16];

        if (!memory->valid || memory->uid_tag != timing->uid_tag ||
            memory->seq != timing->seq) {
            continue;
        }
        memory->valid = false;
        format_note_label(commissioning_key_to_midi_note(key), label, sizeof(label));
        t_peak = timing->t_peak_100us;
        t_rest = timing->t_end_peak_100us;
        total = t_peak + t_rest;
        // Unité 100 µs -> millisecondes avec une décimale.
        diag_out_printf_class(
            DIAG_CLASS_ALWAYS,
            "[LISTEN] seq=%u touche=%s m>M=%lu.%lums M>m=%lu.%lums m>M>m=%lu.%lums%s\n",
            (unsigned)timing->seq, label, (unsigned long)(t_peak / 10u),
            (unsigned long)(t_peak % 10u), (unsigned long)(t_rest / 10u),
            (unsigned long)(t_rest % 10u), (unsigned long)(total / 10u),
            (unsigned long)(total % 10u), memory->split ? " [trille]" : "");

        if (state->rec.noise_enabled) {
            uint32_t lookback = total / 10u + 2u;

            if (lookback > 65535u) {
                lookback = 65535u;
            }
            queue_noise_query(state, key, -1, memory->base, (uint16_t)lookback);
            queue_noise_query(state, key, 1, memory->base, (uint16_t)lookback);
        }
        return;
    }
}

static void handle_listen_noise(clavier_state_t *state,
                                const can_listen_noise_report_t *report) {
    for (uint8_t index = 0u; index < REC_NOISE_PENDING_SLOTS; ++index) {
        rec_noise_pending_t *pending = &state->noise_pending[index];

        if (!pending->valid || pending->request_id != report->request_id ||
            pending->uid_tag != report->uid_tag) {
            continue;
        }
        {
            char label[16];
            uint8_t position = 0u;

            format_note_label(commissioning_key_to_midi_note(pending->key), label,
                              sizeof(label));
            (void)can_link_position_for_uid_tag(report->uid_tag, &position);
            diag_out_printf_class(
                DIAG_CLASS_ALWAYS,
                "[BRUIT] k%s%u pos=%u ch=%u touche=%s min=%u max=%u base=%u%s\n",
                (pending->neighbor == 0u) ? "-" : "+", 1u, (unsigned)position,
                (unsigned)report->channel, label, (unsigned)report->min,
                (unsigned)report->max, (unsigned)pending->base,
                report->truncated ? " TRONQUE" : "");
        }
        pending->valid = false;
        return;
    }
}

// Fin commune d'une session de trace (résumé émis ou timeout) : retour au
// balayage avec le même CRC en REC (aucun LISTEN_PARAM rediffusé), arrêt de
// l'écoute hors REC.
static void end_trace_session(clavier_state_t *state) {
    state->trace_target_valid = false;
    if (state->rec_mode != REC_MODE_OFF) {
        // Le Moteur revient seul en LISTEN après une trace émise ou un timeout,
        // mais ce contrôle LISTEN (même CRC, donc sans LISTEN_PARAM) remet la
        // carte ciblée au balayage dans tous les cas.
        if (!can_link_listen_start(&state->listen_params, CAN_LISTEN_MODE_LISTEN, 0u, 0u)) {
            diag_out_printf_class(DIAG_CLASS_ALWAYS, "[LISTEN] retour balayage impossible\n");
        }
    } else {
        (void)can_link_listen_stop();
    }
}

static void handle_listen_status(clavier_state_t *state, const can_listen_status_t *status,
                                 uint32_t now_ms) {
    uint8_t position;

    if (!can_link_position_for_uid_tag(status->uid_tag, &position)) {
        diag_out_printf_class(DIAG_CLASS_ALWAYS,
                              "[LISTEN] pos=? uid=0x%04X etat=%s periode=%uus debordements=%u\n",
                              (unsigned)status->uid_tag, listen_state_name(status->state),
                              (unsigned)status->sweep_period_us,
                              (unsigned)status->overflows);
        return;
    }

    {
        const bool changed = !state->listen_status_seen[position] ||
                             state->listen_status_last[position] != status->state;
        const bool due = state->listen_status_log_ms[position] == 0u ||
                         (uint32_t)(now_ms - state->listen_status_log_ms[position]) >= 1000u;

        if (changed || due) {
            diag_out_printf_class(
                DIAG_CLASS_ALWAYS,
                "[LISTEN] pos=%u etat=%s periode=%uus debordements=%u\n",
                (unsigned)position, listen_state_name(status->state),
                (unsigned)status->sweep_period_us, (unsigned)status->overflows);
            state->listen_status_seen[position] = true;
            state->listen_status_last[position] = status->state;
            state->listen_status_log_ms[position] = (now_ms == 0u) ? 1u : now_ms;
        }
    }

    if (status->state == CAN_LISTEN_STATE_TRACE_TIMEOUT && state->trace_target_valid &&
        status->uid_tag == state->trace_uid_tag) {
        // Le Moteur est déjà revenu en LISTEN de son côté ; terminer la session
        // côté Clavier exactement comme après une trace émise.
        diag_out_printf_class(DIAG_CLASS_ALWAYS, "[TRACE] timeout pos=%u ch=%u\n",
                              (unsigned)position, (unsigned)state->trace_channel);
        end_trace_session(state);
    }
}

static void handle_listen_trace(clavier_state_t *state, const can_listen_trace_t *trace) {
    // Flux Serial Studio (`e stream`) : 5 colonnes par échantillon, jamais de
    // résumé, jamais rien sur le port 1 (PuTTY). La base est celle du dernier
    // LISTEN_PULSE de ce canal (0 avant la première impulsion).
    if (state->stream_active) {
#if CLAVIER_SERIAL_STUDIO
        const uint16_t base = state->pulse_memory[state->stream_key].base;
        const uint32_t high = (uint32_t)base + state->listen_params.rise_thr;
        const uint32_t low = (uint32_t)base + state->listen_params.return_thr;

        for (uint8_t i = 0u; i < CAN_LISTEN_TRACE_FRAME_SAMPLES; ++i) {
            uint16_t evt = 0u;

            if (state->stream_evt_pending) {
                evt = state->stream_evt_delta;
                state->stream_evt_pending = false;
            }
            serial_studio_printf("/*%u,%u,%lu,%lu,%u*/\n", (unsigned)trace->samples[i],
                                 (unsigned)base, (unsigned long)high, (unsigned long)low,
                                 (unsigned)evt);
        }
#endif
        return;
    }
    if (!state->trace_target_valid) {
        return;
    }
    {
#if CLAVIER_SERIAL_STUDIO
        // Index du premier échantillon de cette trame, avant consommation.
        const uint32_t first_index = state->rec.trace_count;
#endif

        rec_mode_trace_feed(&state->rec, trace);
#if CLAVIER_SERIAL_STUDIO
        // Suite brute de la trace sur le port 2 uniquement (PuTTY ne la reçoit
        // jamais). `/*-1,0*/` a été écrit au démarrage de la trace.
        for (uint8_t i = 0u; i < CAN_LISTEN_TRACE_FRAME_SAMPLES; ++i) {
            serial_studio_printf("/*%lu,%u*/\n", (unsigned long)(first_index + i),
                                 (unsigned)trace->samples[i]);
        }
#endif
    }
    if (state->trace_logged || state->rec.trace_count < CAN_LISTEN_TRACE_SAMPLES) {
        return;
    }
    {
        rec_trace_summary_t summary;
        char label[16] = "?";

        format_note_label(commissioning_key_to_midi_note(state->trace_key), label,
                          sizeof(label));
        if (rec_mode_trace_summary(&state->rec, state->listen_params.trace_period_us,
                                   &summary)) {
            diag_out_printf_class(
                DIAG_CLASS_ALWAYS,
                "[TRACE] pos=%u ch=%u touche=%s base=%u pic=%u delta=%u bruit_cc=%u "
                "t_pic=%lums n=%lu\n",
                (unsigned)state->trace_position, (unsigned)state->trace_channel, label,
                (unsigned)summary.base, (unsigned)summary.pic, (unsigned)summary.delta,
                (unsigned)summary.bruit_cc, (unsigned long)summary.t_pic_ms,
                (unsigned long)summary.count);
        }
    }
    state->trace_logged = true;
    end_trace_session(state);
}

static void process_listen_events(clavier_state_t *state, uint32_t now_ms) {
    can_link_listen_event_t event;

    for (uint8_t count = 0u; count < REC_LISTEN_EVENTS_PER_LOOP; ++count) {
        if (!can_link_listen_take_event(&event)) {
            return;
        }
        if (state->rec_mode == REC_MODE_OFF && !state->trace_target_valid &&
            !state->stream_active && event.kind != CAN_LINK_LISTEN_STATUS) {
            // Événements résiduels d'une session arrêtée : aucune note fantôme.
            // Une trace ou un flux lancé par `e` hors REC reste servi.
            continue;
        }
        switch (event.kind) {
            case CAN_LINK_LISTEN_PULSE:
                handle_listen_pulse(state, &event.u.pulse);
                break;
            case CAN_LINK_LISTEN_PULSE_TIMING:
                handle_listen_timing(state, &event.u.timing);
                break;
            case CAN_LINK_LISTEN_STATUS:
                handle_listen_status(state, &event.u.status, now_ms);
                break;
            case CAN_LINK_LISTEN_NOISE:
                handle_listen_noise(state, &event.u.noise);
                break;
            case CAN_LINK_LISTEN_TRACE_FRAME:
                handle_listen_trace(state, &event.u.trace);
                break;
            default:
                break;
        }
    }
}

int main(void) {
    clavier_state_t state = {0};

    g_console_state = &state;
    usb_log_init();
    serial_studio_init();
    diag_out_init();
    matrix_scan_init();
    keycode_state_init(&state.keycode);
    note_state_init(&state.notes);
    combo_init(&state.combo, keymap_combo_at(0u), keymap_combo_count());
    validate_combo_table();
    // ADR-0032 : les paramètres d'écoute sont compilés ici puis validés une
    // fois ; un jeu invalide rend REC indisponible et le dit au boot.
    state.listen_params = k_listen_config;
    {
        uint8_t bad_id = 0u;

        state.rec_params_valid =
            listen_params_validate(&state.listen_params, &bad_id) == LISTEN_PARAMS_OK;
        if (!state.rec_params_valid) {
            const char *name = listen_params_field_name(bad_id);

            diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                  "[LISTEN] config invalide champ=%s\n",
                                  (name != NULL) ? name : "?");
        }
    }
    rec_mode_set_params(&state.rec, &state.listen_params);
    midi_init();
    can_link_init();
    bringup_console_init();

    while (true) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        if (state.maintenance_mode && state.maintenance_active && !state.maintenance_timed_out &&
            (uint32_t)(now_ms - state.maintenance_started_ms) >= MAINTENANCE_TIMEOUT_MS) {
            (void)can_link_maintenance_push_stop(state.maintenance_midi_note, true);
            state.maintenance_timed_out = true;
            state.last_note_event_ms = now_ms;
        }
        // CAN et diagnostic texte avant le service USB/MIDI.
        can_link_set_note_activity(
            note_state_any_active(&state.notes) || state.combo.output_note_active,
            state.last_note_event_ms);
        // ADR-0032 : les événements LISTEN_* reçus au tour précédent sont
        // convertis en notes MIDI avant toute autre décision.
        process_listen_events(&state, now_ms);
        can_link_poll();
        bringup_console_poll();
        midi_task();
        usb_log_task();
        serial_studio_task();
        matrix_scan_poll(handle_matrix_transition, &state);
        process_event_queue(&state, now_ms);
        sleep_us(215);
    }
}