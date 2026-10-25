#include "keycode.h"

#include <stddef.h>

#define MIDI_NOTE_MIN 0
#define MIDI_NOTE_MAX 127
#define OCTAVE_SEMITONES 12

// ADR-0026 : racines naturelles, indexées par l'état d'altération.
static const uint8_t alteration_root_pitch_class[KEYCODE_ALTERATION_ROOT_COUNT] = {
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
        case KEYCODE_UNTRANSPOSE:
            keycode_state_init(state);
            break;
        case KEYCODE_ALTERATION:
            // ADR-0026 : les modificateurs B/D sont maintenus hors de cet état
            // (clavier_state_t) ; un appui seul n'a ici aucun effet.
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

    for (uint8_t alteration = 0u; alteration < KEYCODE_ALTERATION_ROOT_COUNT; ++alteration) {
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

static bool keycode_pitch_class_is_natural(uint8_t pitch_class) {
    for (uint8_t root = 0u; root < KEYCODE_ALTERATION_ROOT_COUNT; ++root) {
        if (alteration_root_pitch_class[root] == pitch_class) {
            return true;
        }
    }
    return false;
}

static bool keycode_root_index_for_pitch_class(uint8_t pitch_class, uint8_t *root_index) {
    if (root_index == NULL) {
        return false;
    }

    for (uint8_t root = 0u; root < KEYCODE_ALTERATION_ROOT_COUNT; ++root) {
        if (alteration_root_pitch_class[root] == pitch_class) {
            *root_index = root;
            return true;
        }
    }
    return false;
}

bool keycode_alteration_toggle(keycode_state_t *state, uint8_t direction,
                               uint8_t base_midi_note, uint8_t *root_index,
                               uint8_t *new_state) {
    uint8_t pitch_class;
    uint8_t root_pitch_class;
    uint8_t index;
    uint8_t target;
    uint8_t next;

    if (state == NULL || root_index == NULL || new_state == NULL ||
        base_midi_note > MIDI_NOTE_MAX ||
        (direction != (uint8_t)KEYCODE_ALTERATION_B &&
         direction != (uint8_t)KEYCODE_ALTERATION_D)) {
        return false;
    }

    pitch_class = (uint8_t)(base_midi_note % 12u);
    if (keycode_pitch_class_is_natural(pitch_class)) {
        root_pitch_class = pitch_class;
    } else if (direction == (uint8_t)KEYCODE_ALTERATION_D) {
        root_pitch_class = (uint8_t)((pitch_class + 11u) % 12u);
    } else {
        root_pitch_class = (uint8_t)((pitch_class + 1u) % 12u);
    }

    if (!keycode_root_index_for_pitch_class(root_pitch_class, &index)) {
        return false;
    }

    // Cible : état 1 (dièse) pour D, état 2 (bémol) pour B. Un second appui sur
    // la même cible ramène la racine à l'état de base ; l'horodatage est
    // actualisé dans tous les cas (ADR-0026 §4).
    target = (direction == (uint8_t)KEYCODE_ALTERATION_D) ? 1u : 2u;
    next = (state->alteration_state[index] == target) ? 0u : target;
    state->alteration_state[index] = next;
    state->alteration_change[index] = ++state->change_counter;

    *root_index = index;
    *new_state = next;
    return true;
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
                case KEYCODE_ALTERATION_B:
                    return "A_b";
                case KEYCODE_ALTERATION_D:
                    return "A_d";
                default:
                    return "A_invalid";
            }
        case KEYCODE_GET_UID:
            return "GET_UID";
        case KEYCODE_MAINTENANCE_TOGGLE:
            return "MAINT";
        case KEYCODE_UNTRANSPOSE:
            return "UNTRANSPOSE";
        default:
            return "invalid";
    }
}