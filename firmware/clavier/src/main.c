#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "can_link.h"
#include "bringup_console.h"
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
    keycode_state_t keycode;
    note_state_t notes;
    uint32_t last_note_event_ms;
    bool maintenance_mode;
    bool maintenance_active;
    bool maintenance_timed_out;
    uint8_t maintenance_position;
    uint8_t maintenance_midi_note;
    uint32_t maintenance_started_ms;
} clavier_state_t;

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

static void enter_maintenance(clavier_state_t *state) {
    if (state == NULL) {
        return;
    }
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
        if (!note_state_press(&state->notes, &state->keycode, position, keycode.value, &midi_note)) {
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

static void handle_matrix_transition(uint8_t position, bool pressed, void *context) {
    clavier_state_t *state = context;
    const keycode_t keycode = keymap_keycode_for_position(position);

    if (keycode.kind == KEYCODE_NONE) {
#if CLAVIER_DIAG_TRACE
        diag_out_printf_class(DIAG_CLASS_TRACE,
                              "[KEY] pos=%u behavior=unused state=%s\n", position,
                              pressed ? "pressed" : "released");
#endif
        return;
    }

    if (state->maintenance_mode) {
        handle_maintenance_transition(state, position, pressed, keycode);
        return;
    }

    if (keycode.kind == KEYCODE_NOTE) {
        uint8_t midi_note;

        if (pressed) {
            if (!note_state_press(&state->notes, &state->keycode, position,
                                  keycode.value, &midi_note)) {
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
    // Test au même niveau que GET_UID : imbriqué dans le bloc précédent, il était
    // inatteignable et le mode maintenance ne pouvait jamais être activé.
    if (keycode.kind == KEYCODE_MAINTENANCE_TOGGLE) {
        if (pressed) {
            handle_maintenance_toggle(state);
        }
        return;
    }

    if (pressed) {
        keycode_press(&state->keycode, keycode);
    }
    if (keycode.kind == KEYCODE_ALTERATION) {
#if CLAVIER_DIAG_TRACE
        diag_out_printf_class(DIAG_CLASS_TRACE,
                              "[KEY] pos=%u behavior=%s state=%u event=%s\n", position,
                              keycode_name(keycode),
                              keycode_alteration_state(&state->keycode, keycode),
                              pressed ? "pressed" : "released");
#endif
        return;
    }
#if CLAVIER_DIAG_TRACE
    diag_out_printf_class(DIAG_CLASS_TRACE,
                          "[KEY] pos=%u behavior=%s event=%s\n", position,
                          keycode_name(keycode), pressed ? "pressed" : "released");
#endif
}

int main(void) {
    clavier_state_t state = {0};

    usb_log_init();
    diag_out_init();
    matrix_scan_init();
    keycode_state_init(&state.keycode);
    note_state_init(&state.notes);
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
        can_link_set_note_activity(note_state_any_active(&state.notes), state.last_note_event_ms);
        can_link_poll();
        bringup_console_poll();
        midi_task();
        usb_log_task();
        matrix_scan_poll(handle_matrix_transition, &state);
        sleep_us(215);
    }
}