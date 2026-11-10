#ifndef NOTE_STATE_H
#define NOTE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "keycode.h"
#include "keymap.h"

typedef struct {
    bool active[KEYMAP_POSITION_COUNT + 1u];
    uint8_t midi_note[KEYMAP_POSITION_COUNT + 1u];
    uint8_t active_count;
} note_state_t;

void note_state_init(note_state_t *state);
// ADR-0029 : temp_direction = KEYCODE_TEMP_FLAT/_SHARP si TEMP est actif,
// -1 sinon. Le delta TEMP est appliqué après l'armure/ALT, avant transposition.
// ADR-0030 : momentary_delta est la transposition temporaire du côté de la
// position, ajoutée dans la même étape que la transposition persistante,
// avant le bornage (0 en maintenance).
bool note_state_press(note_state_t *state, const keycode_state_t *keycode_state,
                      uint8_t position, uint8_t base_midi_note, int8_t temp_direction,
                      int16_t momentary_delta, uint8_t *midi_note);
bool note_state_release(note_state_t *state, uint8_t position, uint8_t *midi_note);
bool note_state_any_active(const note_state_t *state);

#endif