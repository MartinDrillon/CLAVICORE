#include "note_state.h"

#include <stddef.h>

static uint8_t clamp_midi_note(int16_t note) {
    if (note < 0) {
        return 0u;
    }
    if (note > 127) {
        return 127u;
    }
    return (uint8_t)note;
}

void note_state_init(note_state_t *state) {
    if (state != NULL) {
        *state = (note_state_t){0};
    }
}

bool note_state_press(note_state_t *state, const keycode_state_t *keycode_state,
                      uint8_t position, uint8_t base_midi_note, int8_t temp_direction,
                      int16_t momentary_delta, uint8_t *midi_note) {
    int16_t resolved_note;

    if (state == NULL || keycode_state == NULL || midi_note == NULL || position == 0u ||
        position > KEYMAP_POSITION_COUNT || base_midi_note > 127u) {
        return false;
    }

    // ADR-0029 §6 : note physique → armure/surcouche ALT → TEMP → transposition
    // → bornage. Le delta TEMP porte sur la note déjà résolue (pitch class).
    resolved_note = (int16_t)base_midi_note +
                    keycode_alteration_for_note(keycode_state, base_midi_note);
    if (temp_direction >= 0) {
        resolved_note += keycode_temp_delta((keycode_temp_t)temp_direction, resolved_note);
    }
    // ADR-0030 : la transposition momentanée du côté s'ajoute à la
    // transposition persistante, avant le bornage.
    resolved_note += keycode_transposition_for_position(keycode_state, position);
    resolved_note += momentary_delta;
    *midi_note = clamp_midi_note(resolved_note);
    if (!state->active[position]) {
        ++state->active_count;
    }
    state->midi_note[position] = *midi_note;
    state->active[position] = true;
    return true;
}

bool note_state_release(note_state_t *state, uint8_t position, uint8_t *midi_note) {
    if (state == NULL || midi_note == NULL || position == 0u ||
        position > KEYMAP_POSITION_COUNT || !state->active[position]) {
        return false;
    }

    *midi_note = state->midi_note[position];
    state->active[position] = false;
    if (state->active_count > 0u) {
        --state->active_count;
    }
    return true;
}

bool note_state_any_active(const note_state_t *state) {
    return state != NULL && state->active_count > 0u;
}