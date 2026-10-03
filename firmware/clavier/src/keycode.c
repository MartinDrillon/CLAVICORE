#include "keycode.h"

#include <stddef.h>

#define MIDI_NOTE_MIN 0
#define MIDI_NOTE_MAX 127
#define OCTAVE_SEMITONES 12
#define ALTERATION_CYCLE_LENGTH 3u

static const uint8_t alteration_root_pitch_class[KEYCODE_ALTERATION_COUNT] = {
    0u, 2u, 4u, 5u, 7u, 9u, 11u,
};

static bool keycode_position_is_left(uint8_t position) {
    if (position >= 1u && position <= 60u) {
        return ((position - 1u) % 12u) < 6u;
    }

    return position >= 62u && position <= 66u;
}

void keycode_state_init(keycode_state_t *state) {
    if (state == NULL) {
        return;
    }

    *state = (keycode_state_t){0};
}

bool keycode_is_behavior(keycode_t keycode) {
    return keycode.kind != KEYCODE_NONE && keycode.kind != KEYCODE_NOTE;
}

void keycode_press(keycode_state_t *state, keycode_t keycode) {
    if (state == NULL) {
        return;
    }

    switch (keycode.kind) {
        case KEYCODE_TRANSPOSE_GLOBAL_UP:
            ++state->global_semitones;
            break;
        case KEYCODE_TRANSPOSE_GLOBAL_DOWN:
            --state->global_semitones;
            break;
        case KEYCODE_TRANSPOSE_LEFT_UP:
            ++state->left_octaves;
            break;
        case KEYCODE_TRANSPOSE_LEFT_DOWN:
            --state->left_octaves;
            break;
        case KEYCODE_TRANSPOSE_RIGHT_UP:
            ++state->right_octaves;
            break;
        case KEYCODE_TRANSPOSE_RIGHT_DOWN:
            --state->right_octaves;
            break;
        case KEYCODE_ALTERATION:
            if (keycode.value < KEYCODE_ALTERATION_COUNT) {
                state->alteration_state[keycode.value] =
                    (uint8_t)((state->alteration_state[keycode.value] + 1u) %
                              ALTERATION_CYCLE_LENGTH);
                state->alteration_change[keycode.value] = ++state->change_counter;
            }
            break;
        case KEYCODE_NONE:
        case KEYCODE_NOTE:
        default:
            break;
    }
}

int16_t keycode_transposition_for_position(const keycode_state_t *state, uint8_t position) {
    if (state == NULL || position == 0u || position > 84u) {
        return 0;
    }

    const int16_t octave_offset = keycode_position_is_left(position)
        ? state->left_octaves
        : state->right_octaves;
    return (int16_t)(state->global_semitones + octave_offset * OCTAVE_SEMITONES);
}

static int8_t keycode_alteration_delta(uint8_t state, uint8_t root_pitch_class,
                                       uint8_t note_pitch_class) {
    const uint8_t sharp_pitch_class = (uint8_t)((root_pitch_class + 1u) % 12u);
    const uint8_t flat_pitch_class = (uint8_t)((root_pitch_class + 11u) % 12u);

    if (state == 1u) {
        if (note_pitch_class == root_pitch_class) {
            return 1;
        }
        if (note_pitch_class == sharp_pitch_class) {
            return -1;
        }
    } else if (state == 2u) {
        if (note_pitch_class == root_pitch_class) {
            return -1;
        }
        if (note_pitch_class == flat_pitch_class) {
            return 1;
        }
    }

    return 0;
}

int8_t keycode_alteration_for_note(const keycode_state_t *state, uint8_t base_midi_note) {
    if (state == NULL || base_midi_note > MIDI_NOTE_MAX) {
        return 0;
    }

    const uint8_t pitch_class = (uint8_t)(base_midi_note % 12u);
    uint32_t latest_change = 0u;
    int8_t selected_delta = 0;

    for (uint8_t alteration = 0u; alteration < KEYCODE_ALTERATION_COUNT; ++alteration) {
        const int8_t delta = keycode_alteration_delta(
            state->alteration_state[alteration],
            alteration_root_pitch_class[alteration], pitch_class);
        if (delta != 0 && state->alteration_change[alteration] > latest_change) {
            latest_change = state->alteration_change[alteration];
            selected_delta = delta;
        }
    }

    return selected_delta;
}

uint8_t keycode_alteration_state(const keycode_state_t *state, keycode_t keycode) {
    if (state == NULL || keycode.kind != KEYCODE_ALTERATION ||
        keycode.value >= KEYCODE_ALTERATION_COUNT) {
        return 0u;
    }

    return state->alteration_state[keycode.value];
}

const char *keycode_name(keycode_t keycode) {
    switch (keycode.kind) {
        case KEYCODE_NONE:
            return "unused";
        case KEYCODE_NOTE:
            return "note";
        case KEYCODE_TRANSPOSE_GLOBAL_UP:
            return "T_GlobUp";
        case KEYCODE_TRANSPOSE_GLOBAL_DOWN:
            return "T_GlobDown";
        case KEYCODE_TRANSPOSE_LEFT_UP:
            return "T_OctupG";
        case KEYCODE_TRANSPOSE_LEFT_DOWN:
            return "T_OctdownG";
        case KEYCODE_TRANSPOSE_RIGHT_UP:
            return "T_OctupD";
        case KEYCODE_TRANSPOSE_RIGHT_DOWN:
            return "T_OctdownD";
        case KEYCODE_ALTERATION:
            switch (keycode.value) {
                case KEYCODE_ALTERATION_DO:
                    return "A_do";
                case KEYCODE_ALTERATION_RE:
                    return "A_re";
                case KEYCODE_ALTERATION_MI:
                    return "A_mi";
                case KEYCODE_ALTERATION_FA:
                    return "A_fa";
                case KEYCODE_ALTERATION_SOL:
                    return "A_sol";
                case KEYCODE_ALTERATION_LA:
                    return "A_la";
                case KEYCODE_ALTERATION_SI:
                    return "A_si";
                default:
                    return "A_invalid";
            }
        default:
            return "invalid";
    }
}