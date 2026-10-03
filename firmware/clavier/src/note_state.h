#ifndef NOTE_STATE_H
#define NOTE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "keycode.h"
#include "keymap.h"

typedef struct {
    bool active[KEYMAP_POSITION_COUNT + 1u];
    uint8_t midi_note[KEYMAP_POSITION_COUNT + 1u];
} note_state_t;

void note_state_init(note_state_t *state);
bool note_state_press(note_state_t *state, const keycode_state_t *keycode_state,
                      uint8_t position, uint8_t base_midi_note, uint8_t *midi_note);
bool note_state_release(note_state_t *state, uint8_t position, uint8_t *midi_note);

#endif