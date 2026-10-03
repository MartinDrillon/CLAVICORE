#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "keycode.h"
#include "keymap.h"
#include "matrix_scan.h"
#include "midi.h"
#include "note_state.h"

typedef struct {
    keycode_state_t keycode;
    note_state_t notes;
} clavier_state_t;

static void handle_matrix_transition(uint8_t position, bool pressed, void *context) {
    clavier_state_t *state = context;
    const keycode_t keycode = keymap_keycode_for_position(position);

    if (keycode.kind == KEYCODE_NONE) {
        printf("position %u unused %s\n", position, pressed ? "pressed" : "released");
        return;
    }

    if (keycode.kind == KEYCODE_NOTE) {
        uint8_t midi_note;

        if (pressed) {
            if (!note_state_press(&state->notes, &state->keycode, position,
                                  keycode.value, &midi_note)) {
                printf("position %u note resolution failed\n", position);
                return;
            }
            if (!midi_note_on(midi_note, MIDI_NOTE_VELOCITY)) {
                printf("position %u note %u pressed, USB MIDI unavailable\n",
                       position, midi_note);
            }
        } else if (note_state_release(&state->notes, position, &midi_note)) {
            if (!midi_note_off(midi_note)) {
                printf("position %u note %u released, USB MIDI unavailable\n",
                       position, midi_note);
            }
        }
        return;
    }

    if (pressed) {
        keycode_press(&state->keycode, keycode);
    }
    if (keycode.kind == KEYCODE_ALTERATION) {
        printf("position %u behavior %s state %u %s\n", position, keycode_name(keycode),
               keycode_alteration_state(&state->keycode, keycode),
               pressed ? "pressed" : "released");
        return;
    }
    printf("position %u behavior %s %s\n", position, keycode_name(keycode),
           pressed ? "pressed" : "released");
}

int main(void) {
    clavier_state_t state;

    stdio_init_all();
    matrix_scan_init();
    keycode_state_init(&state.keycode);
    note_state_init(&state.notes);
    midi_init();

    while (true) {
        midi_task();
        matrix_scan_poll(handle_matrix_transition, &state);
        sleep_us(215);
    }
}