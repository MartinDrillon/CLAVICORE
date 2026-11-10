#include "keycode.h"

#include <stddef.h>

#define MIDI_NOTE_MIN 0
#define MIDI_NOTE_MAX 127
#define OCTAVE_SEMITONES 12

// ADR-0026 : racines naturelles, indexées par l'état d'altération.
static const uint8_t alteration_root_pitch_class[KEYCODE_ALTERATION_ROOT_COUNT] = {
    0u, 2u, 4u, 5u, 7u, 9u, 11u,
};

// ADR-0029 : table des 14 armures. Index [mode][tonique][classe] avec classes
// {DO, RE, MI, FA, SOL, LA, SI} ; valeur 0 = naturel, 1 = dièse, 2 = bémol.
// mode 0 = CLE_B (mineur), mode 1 = CLE_D (majeur).
static const uint8_t armure_table[2][KEYCODE_ALTERATION_ROOT_COUNT][KEYCODE_ALTERATION_ROOT_COUNT] = {
    {
        {0u, 0u, 2u, 0u, 0u, 2u, 2u}, // DO mineur : sib, mib, lab
        {0u, 0u, 0u, 0u, 0u, 0u, 2u}, // RE mineur : sib
        {0u, 0u, 0u, 1u, 0u, 0u, 0u}, // MI mineur : fa#
        {0u, 2u, 2u, 0u, 0u, 2u, 2u}, // FA mineur : sib, mib, lab, réb
        {0u, 0u, 2u, 0u, 0u, 0u, 2u}, // SOL mineur : sib, mib
        {0u, 0u, 0u, 0u, 0u, 0u, 0u}, // LA mineur : (aucune)
        {1u, 0u, 0u, 1u, 0u, 0u, 0u}, // SI mineur : fa#, do#
    },
    {
        {0u, 0u, 0u, 0u, 0u, 0u, 0u}, // DO majeur : (aucune)
        {1u, 0u, 0u, 1u, 0u, 0u, 0u}, // RE majeur : fa#, do#
        {1u, 1u, 0u, 1u, 1u, 0u, 0u}, // MI majeur : fa#, do#, sol#, ré#
        {0u, 0u, 0u, 0u, 0u, 0u, 2u}, // FA majeur : sib
        {0u, 0u, 0u, 1u, 0u, 0u, 0u}, // SOL majeur : fa#
        {1u, 0u, 0u, 1u, 1u, 0u, 0u}, // LA majeur : fa#, do#, sol#
        {1u, 1u, 0u, 1u, 1u, 1u, 0u}, // SI majeur : fa#, do#, sol#, ré#, la#
    },
};

// ADR-0029 : delta TEMP_B par pitch class (0..11). TEMP_D est l'opposé exact.
static const int8_t temp_flat_delta[OCTAVE_SEMITONES] = {
    -1, +1, -1, +1, -1, -1, +1, -1, +1, -1, +1, -1,
};

bool keycode_position_is_left(uint8_t position) {
    if (position >= 1u && position <= 60u) {
        return ((position - 1u) % 12u) < 6u;
    }

    return position >= 62u && position <= 66u;
}

// Directions d'une famille : ALT/CLE/TEMP n'en utilisent que deux.
static uint8_t keycode_modifier_direction_limit(uint8_t family) {
    return (family <= (uint8_t)KEYCODE_MOD_FAMILY_TEMP) ? 2u : KEYCODE_MODIFIER_DIRECTIONS;
}

void keycode_modifiers_init(keycode_modifiers_t *modifiers) {
    if (modifiers == NULL) {
        return;
    }

    *modifiers = (keycode_modifiers_t){0};
}

void keycode_modifiers_update(keycode_modifiers_t *modifiers, uint8_t family,
                              uint8_t direction, bool pressed) {
    if (modifiers == NULL || family >= (uint8_t)KEYCODE_MOD_FAMILY_COUNT ||
        direction >= keycode_modifier_direction_limit(family)) {
        return;
    }

    // L'horodatage porte sur l'appui comme sur le relâchement ; seule la
    // dernière direction encore tenue est retenue par keycode_modifiers_active.
    modifiers->seq[family][direction] = ++modifiers->counter;
    modifiers->held[family][direction] = pressed;
}

bool keycode_modifiers_active(const keycode_modifiers_t *modifiers, uint8_t family,
                              uint8_t first_direction, uint8_t direction_count,
                              uint8_t *direction) {
    bool found = false;
    uint32_t latest = 0u;

    if (modifiers == NULL || direction == NULL ||
        family >= (uint8_t)KEYCODE_MOD_FAMILY_COUNT || direction_count == 0u ||
        first_direction >= KEYCODE_MODIFIER_DIRECTIONS ||
        (uint16_t)first_direction + (uint16_t)direction_count >
            (uint16_t)KEYCODE_MODIFIER_DIRECTIONS) {
        return false;
    }

    for (uint8_t index = 0u; index < direction_count; ++index) {
        const uint8_t candidate = (uint8_t)(first_direction + index);
        if (modifiers->held[family][candidate] &&
            modifiers->seq[family][candidate] > latest) {
            latest = modifiers->seq[family][candidate];
            *direction = candidate;
            found = true;
        }
    }

    return found;
}

int16_t keycode_momentary_delta(const keycode_modifiers_t *modifiers, uint8_t position) {
    int16_t delta = 0;
    uint8_t direction = 0u;
    uint8_t first_direction;

    if (modifiers == NULL || position == 0u) {
        return 0;
    }

    // Côtés disjoints : les directions L_* (0,1) ou R_* (2,3) uniquement.
    first_direction = keycode_position_is_left(position) ? 0u : 2u;

    if (keycode_modifiers_active(modifiers, (uint8_t)KEYCODE_MOD_FAMILY_TEMP_ST,
                                 first_direction, 2u, &direction)) {
        delta += (direction == first_direction) ? 1 : -1;
    }
    if (keycode_modifiers_active(modifiers, (uint8_t)KEYCODE_MOD_FAMILY_TEMP_OCT,
                                 first_direction, 2u, &direction)) {
        delta += ((direction == first_direction) ? 1 : -1) * OCTAVE_SEMITONES;
    }

    return delta;
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
        case KEYCODE_CLE:
        case KEYCODE_TEMP:
            // ADR-0029 : armure et TEMP sont suivis dans main.c hors de cet
            // état ; un appui seul n'a ici aucun effet.
            break;
        case KEYCODE_TEMP_TRANSPOSE_ST:
        case KEYCODE_TEMP_TRANSPOSE_OCT:
            // ADR-0030 : transpositions temporaires suivies dans main.c hors
            // de cet état (UNTRANSPOSE ne les annule pas) ; inertes ici.
            break;
        case KEYCODE_REC_SOLE:
        case KEYCODE_REC_MUTE:
            // ADR-0032 : bascules du mode REC traitées dans main.c ; un appui
            // seul n'a ici aucun effet.
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

int8_t keycode_temp_delta(keycode_temp_t direction, int16_t resolved_note) {
    int32_t pitch_class = (int32_t)resolved_note % OCTAVE_SEMITONES;
    if (pitch_class < 0) {
        pitch_class += OCTAVE_SEMITONES;
    }

    const int8_t delta = temp_flat_delta[pitch_class];
    return (direction == KEYCODE_TEMP_SHARP) ? (int8_t)(-delta) : delta;
}

int8_t keycode_alteration_for_note(const keycode_state_t *state, uint8_t base_midi_note) {
    if (state == NULL || base_midi_note > MIDI_NOTE_MAX) {
        return 0;
    }

    const uint8_t pitch_class = (uint8_t)(base_midi_note % 12u);
    uint32_t latest_change = 0u;
    int8_t selected_delta = 0;

    for (uint8_t alteration = 0u; alteration < KEYCODE_ALTERATION_ROOT_COUNT; ++alteration) {
        // État effectif (ADR-0029 §4) : surcouche ALT si non nulle, sinon
        // armure, sinon naturel ; la règle « dernier changement gagne » porte
        // sur l'horodatage de la couche réellement active.
        uint8_t effective_state = state->alteration_state[alteration];
        uint32_t effective_change = state->alteration_change[alteration];
        if (effective_state == 0u) {
            effective_state = state->armure_state[alteration];
            effective_change = state->armure_change;
        }

        const int8_t delta = keycode_alteration_delta(
            effective_state, alteration_root_pitch_class[alteration], pitch_class);
        if (delta != 0 && effective_change > latest_change) {
            latest_change = effective_change;
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

bool keycode_armure_apply(keycode_state_t *state, keycode_cle_t mode,
                          uint16_t base_midi_note, bool *applied) {
    uint8_t pitch_class;
    uint8_t root_index;

    if (state == NULL || applied == NULL || base_midi_note > MIDI_NOTE_MAX ||
        (mode != KEYCODE_CLE_MINOR && mode != KEYCODE_CLE_MAJOR)) {
        return false;
    }

    pitch_class = (uint8_t)(base_midi_note % 12u);
    *applied = false;

    // Touche noire : aucune tonique naturelle, l'armure reste inchangée. Ce
    // n'est pas une erreur (ADR-0029 §3).
    if (!keycode_pitch_class_is_natural(pitch_class)) {
        return true;
    }
    if (!keycode_root_index_for_pitch_class(pitch_class, &root_index)) {
        return true;
    }

    for (uint8_t root = 0u; root < KEYCODE_ALTERATION_ROOT_COUNT; ++root) {
        state->armure_state[root] = armure_table[(uint8_t)mode][root_index][root];
        // Une nouvelle armure efface les surcouches ALT (ADR-0029 §4).
        state->alteration_state[root] = 0u;
    }
    state->armure_change = ++state->change_counter;
    *applied = true;
    return true;
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
        case KEYCODE_CLE:
            switch (keycode.value) {
                case KEYCODE_CLE_MINOR:
                    return "CLE_b";
                case KEYCODE_CLE_MAJOR:
                    return "CLE_d";
                default:
                    return "CLE_invalid";
            }
        case KEYCODE_TEMP:
            switch (keycode.value) {
                case KEYCODE_TEMP_FLAT:
                    return "TEMP_b";
                case KEYCODE_TEMP_SHARP:
                    return "TEMP_d";
                default:
                    return "TEMP_invalid";
            }
        case KEYCODE_TEMP_TRANSPOSE_ST:
            switch (keycode.value) {
                case KEYCODE_TEMP_TRANSPOSE_L_UP:
                    return "T_TEMP_ST_L_UP";
                case KEYCODE_TEMP_TRANSPOSE_L_DOWN:
                    return "T_TEMP_ST_L_DOWN";
                case KEYCODE_TEMP_TRANSPOSE_R_UP:
                    return "T_TEMP_ST_R_UP";
                case KEYCODE_TEMP_TRANSPOSE_R_DOWN:
                    return "T_TEMP_ST_R_DOWN";
                default:
                    return "T_TEMP_ST_invalid";
            }
        case KEYCODE_TEMP_TRANSPOSE_OCT:
            switch (keycode.value) {
                case KEYCODE_TEMP_TRANSPOSE_L_UP:
                    return "T_TEMP_OCT_L_UP";
                case KEYCODE_TEMP_TRANSPOSE_L_DOWN:
                    return "T_TEMP_OCT_L_DOWN";
                case KEYCODE_TEMP_TRANSPOSE_R_UP:
                    return "T_TEMP_OCT_R_UP";
                case KEYCODE_TEMP_TRANSPOSE_R_DOWN:
                    return "T_TEMP_OCT_R_DOWN";
                default:
                    return "T_TEMP_OCT_invalid";
            }
        case KEYCODE_GET_UID:
            return "GET_UID";
        case KEYCODE_MAINTENANCE_TOGGLE:
            return "MAINT";
        case KEYCODE_UNTRANSPOSE:
            return "UNTRANSPOSE";
        case KEYCODE_REC_SOLE:
            return "REC_SOLE";
        case KEYCODE_REC_MUTE:
            return "REC_MUTE";
        default:
            return "invalid";
    }
}