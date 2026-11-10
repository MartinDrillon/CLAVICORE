#include "rec_mode.h"

#include <stdio.h>
#include <string.h>

#include "listen_config.h"
#include "midi.h"

// ---------------------------------------------------------------------------
//  Journal de raisonnement
//
//  Deux sources peuvent tenir la même note MIDI :
//    - la matrice (clavier physique) : `matrix_ref[note]` compte les positions
//      qui la tiennent (0..255) ;
//    - l'écoute IPROPI (mode REC) : `captured_ref[note]` vaut 0 ou 1.
//  Le total décide seul des transitions MIDI : Note On au passage 0 -> 1,
//  Note Off au passage 1 -> 0. Une note tenue par la matrice puis captée (ou
//  l'inverse) ne produit donc jamais de doublon.
//
//  Une impulsion IPROPI est un appui OU un relâchement (le sens n'est pas
//  lisible). L'alternance `key_down[key]` est la seule information de sens :
//  première impulsion = appui, deuxième = relâchement, jusqu'à `e raz`.
// ---------------------------------------------------------------------------

static uint8_t clamp_velocity(uint32_t value) {
    if (value < 1u) {
        return 1u;
    }
    if (value > 127u) {
        return 127u;
    }
    return (uint8_t)value;
}

static bool off_list_add(rec_off_list_t *offs, uint8_t note) {
    if (offs == NULL || offs->count >= REC_OFF_LIST_MAX) {
        return false;
    }
    for (uint8_t i = 0u; i < offs->count; ++i) {
        if (offs->note[i] == note) {
            return true;
        }
    }
    offs->note[offs->count] = note;
    ++offs->count;
    return true;
}

const char *rec_mode_name(rec_mode_t mode) {
    switch (mode) {
        case REC_MODE_OFF:
            return "OFF";
        case REC_MODE_SOLE:
            return "SOLE";
        case REC_MODE_MUTE:
            return "MUTE";
        default:
            return "?";
    }
}

const char *rec_refusal_name(rec_refusal_t refusal) {
    switch (refusal) {
        case REC_OK:
            return "OK";
        case REC_NO_TABLE:
            return "REC_NO_TABLE";
        case REC_SELFTEST:
            return "REC_SELFTEST";
        case REC_PROFILE_SYNC:
            return "REC_PROFILE_SYNC";
        case REC_MAINTENANCE:
            return "REC_MAINTENANCE";
        default:
            return "REC_INCONNU";
    }
}

void rec_mode_set_params(rec_state_t *state, const listen_params_t *params) {
    if (state != NULL) {
        state->params = params;
    }
}

rec_refusal_t rec_mode_enter(rec_state_t *state, rec_mode_t mode,
                             bool table_valid, bool selftest_active,
                             bool profile_sync_active, bool maintenance,
                             rec_off_list_t *offs) {
    if (state == NULL || offs == NULL) {
        return REC_NO_TABLE;
    }

    offs->count = 0u;
    if (mode == REC_MODE_OFF) {
        return REC_OK;
    }
    if (mode != REC_MODE_SOLE && mode != REC_MODE_MUTE) {
        return REC_NO_TABLE;
    }

    // Ordre des refus : la table d'abord (sans elle aucune touche n'est
    // identifiable), puis les activités qui partagent le bus/le matériel.
    if (!table_valid) {
        return REC_NO_TABLE;
    }
    if (selftest_active) {
        return REC_SELFTEST;
    }
    if (profile_sync_active) {
        return REC_PROFILE_SYNC;
    }
    if (maintenance) {
        return REC_MAINTENANCE;
    }

    // Toutes les touches sont considérées relevées : l'alternance repart de
    // zéro (ADR-0032 §8). Aucune note captée n'est active à l'entrée.
    memset(state->captured_ref, 0, sizeof(state->captured_ref));
    memset(state->key_down, 0, sizeof(state->key_down));
    rec_mode_trace_reset(state);

    if (mode == REC_MODE_MUTE) {
        // La matrice devient muette : ses notes actives sont rendues à
        // l'appelant (Note Off MIDI) et son compteur repart de zéro. En SOLE,
        // le MIDI matrice est conservé : compteurs et notes restent en place.
        for (uint16_t note = 0u; note < 128u; ++note) {
            if (state->matrix_ref[note] > 0u) {
                (void)off_list_add(offs, (uint8_t)note);
                state->matrix_ref[note] = 0u;
            }
        }
    }
    return REC_OK;
}

void rec_mode_exit(rec_state_t *state, rec_off_list_t *offs) {
    if (state == NULL) {
        return;
    }
    if (offs != NULL) {
        offs->count = 0u;
        for (uint16_t note = 0u; note < 128u; ++note) {
            if (state->captured_ref[note] > 0u) {
                (void)off_list_add(offs, (uint8_t)note);
            }
        }
    }
    memset(state->captured_ref, 0, sizeof(state->captured_ref));
    // Hors REC, le compteur matrice n'est plus consulté : le vider évite de
    // compter deux fois une touche restée tenue à la prochaine entrée (elle est
    // réamorcée depuis l'état physique par rec_mode_seed_matrix_note).
    memset(state->matrix_ref, 0, sizeof(state->matrix_ref));
    memset(state->key_down, 0, sizeof(state->key_down));
    rec_mode_trace_reset(state);
}

void rec_mode_raz(rec_state_t *state) {
    if (state != NULL) {
        memset(state->key_down, 0, sizeof(state->key_down));
    }
}

void rec_mode_seed_matrix_note(rec_state_t *state, uint8_t midi_note) {
    if (state != NULL && midi_note < 128u && state->matrix_ref[midi_note] < 255u) {
        ++state->matrix_ref[midi_note];
    }
}

rec_action_t rec_mode_matrix_event(rec_state_t *state, rec_mode_t mode,
                                   uint8_t midi_note, bool pressed) {
    rec_action_t action = {false, false, 0u, 0u};
    uint16_t before;
    uint16_t after;

    if (state == NULL || midi_note > 127u) {
        return action;
    }

    before = (uint16_t)state->matrix_ref[midi_note] + state->captured_ref[midi_note];
    if (pressed) {
        if (state->matrix_ref[midi_note] < 255u) {
            ++state->matrix_ref[midi_note];
        }
    } else if (state->matrix_ref[midi_note] > 0u) {
        --state->matrix_ref[midi_note];
    }
    after = (uint16_t)state->matrix_ref[midi_note] + state->captured_ref[midi_note];

    // REC_MUTE : matrice muette. Les compteurs restent justes pour que la
    // fusion et la sortie du mode ne créent ni doublon ni note fantôme.
    if (mode == REC_MODE_MUTE) {
        return action;
    }
    if (before == 0u && after > 0u) {
        action.emit_on = true;
        action.midi_note = midi_note;
        action.velocity = MIDI_NOTE_VELOCITY;
    } else if (before > 0u && after == 0u) {
        action.emit_off = true;
        action.midi_note = midi_note;
    }
    return action;
}

rec_action_t rec_mode_captured_event(rec_state_t *state, rec_mode_t mode,
                                     uint8_t key, uint16_t delta) {
    rec_action_t action = {false, false, 0u, 0u};
    uint8_t midi_note;
    bool was_down;
    uint16_t before;
    uint16_t after;

    (void)mode;  // les notes captées sortent dans les deux modes REC
    if (state == NULL || key < COMMISSIONING_KEY_MIN || key > COMMISSIONING_KEY_MAX) {
        return action;
    }

    was_down = state->key_down[key] != 0u;
    state->key_down[key] = was_down ? 0u : 1u;
    midi_note = commissioning_key_to_midi_note(key);

    before = (uint16_t)state->matrix_ref[midi_note] + state->captured_ref[midi_note];
    if (!was_down) {
        state->captured_ref[midi_note] = 1u;
    } else {
        state->captured_ref[midi_note] = 0u;
    }
    after = (uint16_t)state->matrix_ref[midi_note] + state->captured_ref[midi_note];

    if (!was_down && before == 0u && after > 0u) {
        action.emit_on = true;
        action.midi_note = midi_note;
        action.velocity = (state->params != NULL)
                              ? rec_mode_velocity(state->params, delta)
                              : (uint8_t)MIDI_NOTE_VELOCITY;
    } else if (was_down && before > 0u && after == 0u) {
        action.emit_off = true;
        action.midi_note = midi_note;
    }
    return action;
}

bool rec_mode_lookup_uid_channel(const commissioning_runtime_t *runtime,
                                 uint16_t uid_tag, uint8_t channel, uint8_t *key) {
    if (runtime == NULL || key == NULL || channel >= CAN_IPROPI_CHANNEL_COUNT) {
        return false;
    }
    for (uint8_t candidate = COMMISSIONING_KEY_MIN; candidate <= COMMISSIONING_KEY_MAX;
         ++candidate) {
        const commissioning_note_target_t *target =
            &runtime->notes[candidate - COMMISSIONING_KEY_MIN];

        if (target->assigned && target->uid_tag == uid_tag &&
            target->channel == channel) {
            *key = candidate;
            return true;
        }
    }
    return false;
}

bool rec_mode_lookup_neighbor(const commissioning_runtime_t *runtime, uint8_t key,
                              int8_t delta, commissioning_note_target_t *out) {
    int16_t neighbor;

    if (runtime == NULL || out == NULL || delta == 0) {
        return false;
    }
    neighbor = (int16_t)key + (int16_t)delta;
    if (neighbor < (int16_t)COMMISSIONING_KEY_MIN ||
        neighbor > (int16_t)COMMISSIONING_KEY_MAX) {
        return false;
    }
    {
        const commissioning_note_target_t *target =
            &runtime->notes[neighbor - (int16_t)COMMISSIONING_KEY_MIN];
        if (!target->assigned) {
            return false;
        }
        *out = *target;
    }
    return true;
}

uint8_t rec_mode_velocity(const listen_params_t *params, uint16_t delta) {
    uint32_t min_delta;
    uint32_t max_delta;
    uint32_t value;

    if (params == NULL) {
        return (uint8_t)MIDI_NOTE_VELOCITY;
    }
    if (params->vel_min_delta == 0u || params->vel_max_delta <= params->vel_min_delta) {
        return clamp_velocity(params->vel_fixed);
    }

    min_delta = params->vel_min_delta;
    max_delta = params->vel_max_delta;
    if ((uint32_t)delta <= min_delta) {
        return 1u;
    }
    if ((uint32_t)delta >= max_delta) {
        return 127u;
    }
    value = 1u + (((uint32_t)delta - min_delta) * 126u) / (max_delta - min_delta);
    return clamp_velocity(value);
}

static bool name_matches(const char *text, size_t used, const char *name, size_t length) {
    if (used != length) {
        return false;
    }
    for (size_t i = 0u; i < length; ++i) {
        char c = text[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != name[i]) {
            return false;
        }
    }
    return true;
}

static bool parse_note_name(const char *text, uint8_t *midi_note) {
    static const struct {
        const char *name;
        uint8_t length;
        int8_t semitone;
    } k_names[] = {
        {"SOL", 3u, 7}, {"SI", 2u, 11}, {"DO", 2u, 0}, {"RE", 2u, 2},
        {"MI", 2u, 4},  {"FA", 2u, 5},  {"LA", 2u, 9}, {"UT", 2u, 0},
    };
    size_t used = 0u;
    int semitone = -1;
    int accidental = 0;
    int octave;
    int value;

    // Correspondance par préfixe, noms les plus longs d'abord : "SOL" avant
    // "SI", pour que le `b` de "SIb3" reste une altération et ne soit pas
    // avalé comme une lettre du nom.
    for (size_t i = 0u; i < sizeof(k_names) / sizeof(k_names[0]); ++i) {
        if (name_matches(text, k_names[i].length, k_names[i].name, k_names[i].length)) {
            semitone = k_names[i].semitone;
            used = k_names[i].length;
            break;
        }
    }
    if (semitone < 0) {
        return false;
    }

    if (text[used] == '#' || text[used] == 'b' || text[used] == 'B') {
        accidental = (text[used] == '#') ? 1 : -1;
        ++used;
    }
    if (text[used] < '0' || text[used] > '9') {
        return false;
    }
    octave = text[used] - '0';
    ++used;
    while (text[used] != '\0') {
        if (text[used] != ' ' && text[used] != '\t') {
            return false;
        }
        ++used;
    }

    value = 12 * (octave + 1) + semitone + accidental;
    if (value < 0 || value > 127) {
        return false;
    }
    *midi_note = (uint8_t)value;
    return true;
}

bool rec_mode_parse_note(const char *text, uint8_t *midi_note) {
    size_t used = 0u;

    if (text == NULL || midi_note == NULL) {
        return false;
    }
    while (text[used] == ' ' || text[used] == '\t') {
        ++used;
    }
    text += used;
    if (text[0] == '\0') {
        return false;
    }
    if (text[0] >= '0' && text[0] <= '9') {
        uint32_t value = 0u;
        size_t index = 0u;

        while (text[index] >= '0' && text[index] <= '9') {
            value = value * 10u + (uint32_t)(text[index] - '0');
            if (value > 127u) {
                return false;
            }
            ++index;
        }
        while (text[index] != '\0') {
            if (text[index] != ' ' && text[index] != '\t') {
                return false;
            }
            ++index;
        }
        *midi_note = (uint8_t)value;
        return true;
    }
    return parse_note_name(text, midi_note);
}

bool rec_mode_note_name(uint8_t midi_note, char *out, size_t capacity) {
    static const char *const k_semitones[] = {
        "DO", "DO#", "RE", "RE#", "MI", "FA", "FA#", "SOL", "SOL#", "LA", "LA#", "SI",
    };
    int written;

    if (out == NULL || capacity == 0u || midi_note < 12u) {
        return false;
    }
    written = snprintf(out, capacity, "%s%u", k_semitones[midi_note % 12u],
                       (unsigned)(midi_note / 12u - 1u));
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool rec_mode_resolve_touch(const commissioning_runtime_t *runtime, const char *text,
                            uint8_t *midi_note, commissioning_note_target_t *out) {
    uint8_t note;

    if (runtime == NULL || text == NULL || midi_note == NULL || out == NULL) {
        return false;
    }
    if (!rec_mode_parse_note(text, &note)) {
        return false;
    }
    *midi_note = note;
    return commissioning_lookup_midi_note(runtime, note, out) == COMMISSIONING_OK;
}

bool rec_mode_noise_toggle(rec_state_t *state, bool detail_enabled, bool *enabled_out) {
    if (state == NULL) {
        return false;
    }
    if (!detail_enabled) {
        if (enabled_out != NULL) {
            *enabled_out = state->noise_enabled;
        }
        return false;
    }
    state->noise_enabled = !state->noise_enabled;
    if (enabled_out != NULL) {
        *enabled_out = state->noise_enabled;
    }
    return true;
}

void rec_mode_trace_reset(rec_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->trace_min = 0xFFFFu;
    state->trace_max = 0u;
    state->trace_first = 0u;
    state->trace_last = 0u;
    state->trace_pic = 0u;
    state->trace_count = 0u;
    state->trace_peak_index = 0u;
}

void rec_mode_trace_feed(rec_state_t *state, const can_listen_trace_t *frame) {
    if (state == NULL || frame == NULL) {
        return;
    }
    for (uint8_t i = 0u; i < CAN_LISTEN_TRACE_FRAME_SAMPLES; ++i) {
        uint16_t sample;
        uint32_t index;

        if (state->trace_count >= CAN_LISTEN_TRACE_SAMPLES) {
            return;
        }
        sample = frame->samples[i];
        index = state->trace_count;
        if (index == 0u) {
            state->trace_first = sample;
        }
        state->trace_last = sample;
        if (index < REC_TRACE_PRE_TRIGGER) {
            if (sample < state->trace_min) {
                state->trace_min = sample;
            }
            if (sample > state->trace_max) {
                state->trace_max = sample;
            }
        }
        if (sample > state->trace_pic) {
            state->trace_pic = sample;
            state->trace_peak_index = index;
        }
        ++state->trace_count;
    }
}

bool rec_mode_trace_summary(const rec_state_t *state, uint32_t trace_period_us,
                            rec_trace_summary_t *out) {
    if (state == NULL || out == NULL || state->trace_count < CAN_LISTEN_TRACE_SAMPLES) {
        return false;
    }
    out->base = state->trace_min;
    out->pic = state->trace_pic;
    out->delta = (out->pic >= out->base) ? (uint16_t)(out->pic - out->base) : 0u;
    out->bruit_cc = (state->trace_max >= state->trace_min)
                        ? (uint16_t)(state->trace_max - state->trace_min)
                        : 0u;
    out->t_pic_ms = (uint32_t)(((uint64_t)state->trace_peak_index * trace_period_us) / 1000u);
    out->count = state->trace_count;
    return true;
}

bool rec_mode_param_get(const listen_params_t *params, const char *name, uint32_t *value) {
    uint8_t id;

    if (params == NULL || name == NULL || value == NULL) {
        return false;
    }
    if (!listen_params_field_id(name, &id)) {
        return false;
    }
    return listen_params_get(params, id, value);
}

bool rec_mode_param_set(listen_params_t *params, const char *name, uint32_t value) {
    uint8_t id;

    if (params == NULL || name == NULL) {
        return false;
    }
    if (!listen_params_field_id(name, &id)) {
        return false;
    }
    return listen_params_set(params, id, value);
}

listen_params_result_t rec_mode_params_validate(const listen_params_t *params,
                                                const char **bad_name) {
    uint8_t bad_id = 0u;
    listen_params_result_t result;

    if (bad_name != NULL) {
        *bad_name = NULL;
    }
    result = listen_params_validate(params, &bad_id);
    if (result == LISTEN_PARAMS_ERR_FIELD && bad_name != NULL) {
        const char *name = listen_params_field_name(bad_id);

        *bad_name = (name != NULL) ? name : "?";
    }
    return result;
}

size_t rec_mode_params_diff(const listen_params_t *params, char *out, size_t capacity) {
    size_t used = 0u;
    size_t lines = 0u;

    if (out == NULL || capacity == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (params == NULL) {
        return 0u;
    }

    for (uint8_t id = 1u; id <= LISTEN_PARAMS_COUNT; ++id) {
        uint32_t current;
        uint32_t reference;
        const char *name = listen_params_field_name(id);
        int written;

        if (name == NULL ||
            !listen_params_get(params, id, &current) ||
            !listen_params_get(&k_listen_config, id, &reference)) {
            continue;
        }
        if (current == reference) {
            continue;
        }
        if (used >= capacity) {
            break;
        }
        written = snprintf(out + used, capacity - used, ".%s = %lu,\n", name,
                           (unsigned long)current);
        if (written < 0 || (size_t)written >= capacity - used) {
            out[used] = '\0';
            break;
        }
        used += (size_t)written;
        ++lines;
    }
    return lines;
}
