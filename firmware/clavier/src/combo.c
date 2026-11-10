#include "combo.h"

#include <stddef.h>

// ADR-0031 : moteur pur, sans horloge matérielle. Toutes les durées sont
// calculées par soustraction uint32_t (robuste au rebouclage).

static keycode_t combo_decode_binding(uint16_t binding) {
    return (keycode_t){
        .kind = (keycode_kind_t)(binding >> 8u),
        .value = (uint8_t)binding,
    };
}

static bool combo_is_member(const combo_state_t *state, uint8_t position) {
    if (state == NULL || state->table == NULL) {
        return false;
    }

    for (uint8_t combo_index = 0u; combo_index < state->table_count; ++combo_index) {
        const keymap_combo_t *combo = &state->table[combo_index];

        if (combo->positions == NULL) {
            continue;
        }
        for (uint8_t member = 0u; member < combo->count; ++member) {
            if (combo->positions[member] == position) {
                return true;
            }
        }
    }
    return false;
}

static void combo_pending_remove(combo_state_t *state, uint8_t position) {
    uint8_t write = 0u;

    if (!state->pending[position]) {
        return;
    }

    state->pending[position] = false;
    for (uint8_t index = 0u; index < state->pending_count; ++index) {
        if (state->pending_order[index] != position) {
            state->pending_order[write] = state->pending_order[index];
            ++write;
        }
    }
    state->pending_count = write;
}

// Choisit le combo complet dont la fenêtre n'est pas dépassée : le plus de
// positions, puis le plus petit index de table (parcours croissant).
static int combo_find_trigger(const combo_state_t *state, uint32_t now_ms) {
    int best = -1;
    uint8_t best_count = 0u;

    if (state->table == NULL) {
        return -1;
    }

    for (uint8_t combo_index = 0u; combo_index < state->table_count; ++combo_index) {
        const keymap_combo_t *combo = &state->table[combo_index];
        bool complete = true;
        uint32_t elapsed = 0u;

        if (combo->positions == NULL || combo->count < 2u ||
            combo->count > COMBO_MAX_POSITIONS || combo->timeout_ms == 0u) {
            continue;
        }

        for (uint8_t member = 0u; member < combo->count; ++member) {
            const uint8_t position = combo->positions[member];

            if (position == 0u || position > KEYMAP_POSITION_COUNT ||
                !state->pending[position]) {
                complete = false;
                break;
            }
            const uint32_t member_elapsed = now_ms - state->pressed_at_ms[position];
            if (member_elapsed > elapsed) {
                elapsed = member_elapsed;
            }
        }

        if (!complete || elapsed >= combo->timeout_ms) {
            continue;
        }
        if (combo->count > best_count) {
            best = (int)combo_index;
            best_count = combo->count;
        }
    }

    return best;
}

// Vrai si la fenêtre du combo est dépassée : le plus ancien de ses membres
// encore pending a atteint timeout_ms. Un combo sans membre pending ne bloque
// plus aucune expiration.
static bool combo_window_exceeded(const combo_state_t *state,
                                  const keymap_combo_t *combo, uint32_t now_ms) {
    bool found = false;
    uint32_t oldest_elapsed = 0u;

    if (combo->positions == NULL) {
        return true;
    }

    for (uint8_t member = 0u; member < combo->count; ++member) {
        const uint8_t position = combo->positions[member];

        if (position == 0u || position > KEYMAP_POSITION_COUNT ||
            !state->pending[position]) {
            continue;
        }
        {
            const uint32_t elapsed = now_ms - state->pressed_at_ms[position];
            if (!found || elapsed > oldest_elapsed) {
                oldest_elapsed = elapsed;
            }
        }
        found = true;
    }

    if (!found) {
        return true;
    }
    return oldest_elapsed >= combo->timeout_ms;
}

// Vrai si la position peut quitter la file pending : pour TOUS les combos qui
// la contiennent, leur fenêtre (mesurée depuis leur plus ancien membre pending)
// est dépassée. Une position hors combo est émise immédiatement.
static bool combo_position_expired(const combo_state_t *state, uint8_t position,
                                   uint32_t now_ms) {
    if (state->table == NULL) {
        return true;
    }

    for (uint8_t combo_index = 0u; combo_index < state->table_count; ++combo_index) {
        const keymap_combo_t *combo = &state->table[combo_index];
        bool contains = false;

        if (combo->positions == NULL) {
            continue;
        }
        for (uint8_t member = 0u; member < combo->count; ++member) {
            if (combo->positions[member] == position) {
                contains = true;
                break;
            }
        }
        if (!contains) {
            continue;
        }
        if (!combo_window_exceeded(state, combo, now_ms)) {
            return false;
        }
    }
    return true;
}

static keycode_t combo_active_binding(const combo_state_t *state) {
    if (state->table != NULL && state->active_combo < state->table_count) {
        return combo_decode_binding(state->table[state->active_combo].binding);
    }
    return KEYCODE_NONE_VALUE;
}

static uint8_t combo_emit(combo_action_t *out, uint8_t max_out, uint8_t count,
                          uint8_t kind, uint8_t position, keycode_t keycode) {
    if (out == NULL || count >= max_out) {
        return count;
    }
    out[count].kind = kind;
    out[count].position = position;
    out[count].keycode = keycode;
    return (uint8_t)(count + 1u);
}

void combo_reset(combo_state_t *state) {
    const keymap_combo_t *table;
    uint8_t table_count;

    if (state == NULL) {
        return;
    }

    table = state->table;
    table_count = state->table_count;
    *state = (combo_state_t){0};
    state->table = table;
    state->table_count = table_count;
}

void combo_init(combo_state_t *state, const keymap_combo_t *table, uint8_t table_count) {
    if (state == NULL) {
        return;
    }

    state->table = table;
    state->table_count = (table == NULL) ? 0u : table_count;
    combo_reset(state);
}

uint8_t combo_on_press(combo_state_t *state, uint8_t position, uint32_t now_ms,
                       combo_action_t *out, uint8_t max_out) {
    uint8_t written = 0u;

    if (state == NULL || position == 0u || position > KEYMAP_POSITION_COUNT) {
        return 0u;
    }
    // Un appui non-membre n'entre jamais dans le moteur (chemin immédiat).
    if (!combo_is_member(state, position)) {
        return 0u;
    }
    // Pendant qu'un combo est actif, un appui sur un de ses membres armés est
    // ignoré. Après le relâchement de la sortie, la position redevient un
    // appui membre normal.
    if (state->active && state->armed[position]) {
        return 0u;
    }
    if (state->pending[position] || state->pending_count >= COMBO_POSITION_SLOTS) {
        return 0u;
    }

    state->pending[position] = true;
    state->pressed_at_ms[position] = now_ms;
    state->pending_order[state->pending_count] = position;
    ++state->pending_count;
    ++state->seq;

    // Aucun nouveau déclenchement tant qu'un combo reste actif (membres
    // armés) : un nouveau déclenchement écraserait le suivi des anciens
    // membres et fausserait armed_count.
    if (state->active) {
        return 0u;
    }

    {
        const int best = combo_find_trigger(state, now_ms);

        if (best < 0) {
            return 0u;
        }

        const keymap_combo_t *combo = &state->table[best];
        const keycode_t binding = combo_decode_binding(combo->binding);

        for (uint8_t member = 0u; member < combo->count; ++member) {
            const uint8_t member_position = combo->positions[member];

            combo_pending_remove(state, member_position);
            state->armed[member_position] = true;
        }

        state->armed_count = combo->count;
        state->active = true;
        state->active_combo = (uint8_t)best;
        state->output_held = true;
        if (binding.kind == KEYCODE_NOTE) {
            state->output_note = binding.value;
            state->output_note_active = true;
        }
        written = combo_emit(out, max_out, written, COMBO_ACTION_OUTPUT_PRESS, 0u,
                             binding);
    }

    return written;
}

uint8_t combo_on_release(combo_state_t *state, uint8_t position,
                         combo_action_t *out, uint8_t max_out) {
    uint8_t written = 0u;

    if (state == NULL || position == 0u || position > KEYMAP_POSITION_COUNT) {
        return 0u;
    }

    if (state->armed[position]) {
        state->armed[position] = false;
        if (state->armed_count > 0u) {
            --state->armed_count;
        }

        // La sortie est relâchée au premier relâchement d'un membre.
        if (state->output_held) {
            const keycode_t binding = combo_active_binding(state);

            state->output_held = false;
            state->output_note_active = false;
            written = combo_emit(out, max_out, written, COMBO_ACTION_OUTPUT_RELEASE,
                                 0u, binding);
        }
        if (state->armed_count == 0u) {
            state->active = false;
            state->active_combo = 0u;
        }
        return written;
    }

    // Relâchement d'un appui pending avant complétion : vider immédiatement
    // tous les appuis retenus, dans leur ordre d'appui. Le relâchement
    // physique de `position` est ensuite traité par l'appelant.
    if (state->pending[position]) {
        for (uint8_t index = 0u; index < state->pending_count; ++index) {
            const uint8_t pending_position = state->pending_order[index];

            if (written < max_out && out != NULL) {
                out[written].kind = COMBO_ACTION_PENDING_PRESS;
                out[written].position = pending_position;
                out[written].keycode = KEYCODE_NONE_VALUE;
                ++written;
                state->pending[pending_position] = false;
            }
        }

        if (written == state->pending_count) {
            state->pending_count = 0u;
        } else {
            // Borne max_out atteinte : conserver le reste dans l'ordre.
            uint8_t write = 0u;

            for (uint8_t index = 0u; index < state->pending_count; ++index) {
                const uint8_t pending_position = state->pending_order[index];

                if (state->pending[pending_position]) {
                    state->pending_order[write] = pending_position;
                    ++write;
                }
            }
            state->pending_count = write;
        }
    }

    return written;
}

uint8_t combo_expire(combo_state_t *state, uint32_t now_ms,
                     combo_action_t *out, uint8_t max_out) {
    bool expired[COMBO_POSITION_SLOTS] = {false};
    uint8_t written = 0u;
    uint8_t write = 0u;

    if (state == NULL) {
        return 0u;
    }

    // Décision prise sur l'ensemble pending du début du passage : émettre une
    // position ne doit pas raccourcir la fenêtre des suivantes.
    for (uint8_t index = 0u; index < state->pending_count; ++index) {
        const uint8_t position = state->pending_order[index];

        expired[position] = combo_position_expired(state, position, now_ms);
    }

    for (uint8_t index = 0u; index < state->pending_count; ++index) {
        const uint8_t position = state->pending_order[index];

        if (!expired[position]) {
            state->pending_order[write] = position;
            ++write;
            continue;
        }

        if (written < max_out && out != NULL) {
            out[written].kind = COMBO_ACTION_PENDING_PRESS;
            out[written].position = position;
            out[written].keycode = KEYCODE_NONE_VALUE;
            ++written;
            state->pending[position] = false;
            continue;
        }

        state->pending_order[write] = position;
        ++write;
    }

    state->pending_count = write;
    return written;
}

uint8_t combo_release_output(combo_state_t *state, combo_action_t *out, uint8_t max_out) {
    if (state == NULL || !state->output_held) {
        return 0u;
    }

    {
        const keycode_t binding = combo_active_binding(state);

        state->output_held = false;
        state->output_note_active = false;
        return combo_emit(out, max_out, 0u, COMBO_ACTION_OUTPUT_RELEASE, 0u, binding);
    }
}

bool combo_position_is_armed(const combo_state_t *state, uint8_t position) {
    if (state == NULL || position == 0u || position > KEYMAP_POSITION_COUNT) {
        return false;
    }
    return state->armed[position];
}
