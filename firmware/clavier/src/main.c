#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "can_link.h"
#include "bringup_console.h"
#include "combo.h"
#include "diag_log.h"
#include "diag_out.h"
#include "keycode.h"
#include "keymap.h"
#include "matrix_scan.h"
#include "midi.h"
#include "note_state.h"
#include "can_protocol.h"
#include "usb_log.h"

typedef struct {
    uint8_t position;
    bool pressed;
} matrix_event_t;

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
} clavier_state_t;

static void dispatch_keycode(clavier_state_t *state, uint8_t position,
                             keycode_t keycode, bool pressed);

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
            if (pressed) {
                state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
                (void)can_link_note_on(keycode.value, MIDI_NOTE_VELOCITY);
                if (!midi_note_on(keycode.value, MIDI_NOTE_VELOCITY)) {
                    diag_out_printf_class(
                        DIAG_CLASS_ALWAYS,
                        "[KEY] pos=0 note=%u state=pressed usb_midi=unavailable\n",
                        keycode.value);
                }
            } else {
                state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
                (void)can_link_note_off(keycode.value);
                if (!midi_note_off(keycode.value)) {
                    diag_out_printf_class(
                        DIAG_CLASS_ALWAYS,
                        "[KEY] pos=0 note=%u state=released usb_midi=unavailable\n",
                        keycode.value);
                }
            }
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
            (void)can_link_note_on(midi_note, MIDI_NOTE_VELOCITY);
            if (!midi_note_on(midi_note, MIDI_NOTE_VELOCITY)) {
                diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                      "[KEY] pos=%u note=%u state=pressed usb_midi=unavailable\n",
                                      position, midi_note);
            }
        } else if (note_state_release(&state->notes, position, &midi_note)) {
            state->last_note_event_ms = to_ms_since_boot(get_absolute_time());
            (void)can_link_note_off(midi_note);
            if (!midi_note_off(midi_note)) {
                diag_out_printf_class(DIAG_CLASS_ALWAYS,
                                      "[KEY] pos=%u note=%u state=released usb_midi=unavailable\n",
                                      position, midi_note);
            }
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

int main(void) {
    clavier_state_t state = {0};

    usb_log_init();
    diag_out_init();
    matrix_scan_init();
    keycode_state_init(&state.keycode);
    note_state_init(&state.notes);
    combo_init(&state.combo, keymap_combo_at(0u), keymap_combo_count());
    validate_combo_table();
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
        can_link_poll();
        bringup_console_poll();
        midi_task();
        usb_log_task();
        matrix_scan_poll(handle_matrix_transition, &state);
        process_event_queue(&state, now_ms);
        sleep_us(215);
    }
}