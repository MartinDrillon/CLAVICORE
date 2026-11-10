// Tests unitaires hôte du mode REC Clavier (ADR-0032 Lot 3a).
//
// Aucune position de keymap n'est normative : les touches utilisées ici sont
// arbitraires (1..49) et proviennent d'une table de commissioning locale au
// test. Le module rec_mode.c est compilé sans SDK, avec can_protocol.c,
// commissioning_table.c et listen_params.c.
#include <stdio.h>
#include <string.h>

#include "commissioning_table.h"
#include "listen_config.h"
#include "listen_params.h"
#include "rec_mode.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("ECHEC %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                    \
    } while (0)

static const uint8_t UID_A[8] = {0xE6, 0x60, 0x58, 0x38, 0x83, 0x12, 0x34, 0x56};
static const uint8_t UID_B[8] = {0xAA, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};

static commissioning_entry_t make_entry(const uint8_t uid[8], uint8_t position,
                                        uint16_t bitmap) {
    commissioning_entry_t entry = {0};

    memcpy(entry.identity.uid.id, uid, 8u);
    entry.identity.position = position;
    entry.occupation_bitmap = bitmap;
    return entry;
}

static commissioning_runtime_t g_runtime;

static void build_two_cards(void) {
    // Position 3 : canaux 0 et 1 -> touches 1 et 2.
    // Position 4 : canaux 0 et 1 -> touches 3 et 4.
    commissioning_entry_t entries[] = {
        make_entry(UID_A, 3u, 0x003u),
        make_entry(UID_B, 4u, 0x003u),
    };

    CHECK(commissioning_build_runtime(entries, 2u, &g_runtime) == COMMISSIONING_OK);
    CHECK(g_runtime.notes[0].assigned && g_runtime.notes[1].assigned);
    CHECK(g_runtime.notes[2].assigned && g_runtime.notes[3].assigned);
}

static listen_params_t fixed_params(uint32_t velocity) {
    listen_params_t params = k_listen_config;

    params.vel_fixed = velocity;
    params.vel_min_delta = 0u;
    params.vel_max_delta = 0u;
    return params;
}

static rec_state_t new_state(const listen_params_t *params) {
    rec_state_t state = {0};

    rec_mode_set_params(&state, params);
    return state;
}

static void test_alternation(void) {
    const listen_params_t params = fixed_params(100u);
    rec_state_t state = new_state(&params);
    rec_action_t action;

    action = rec_mode_captured_event(&state, REC_MODE_SOLE, 3u, 10u);
    CHECK(action.emit_on && !action.emit_off);
    CHECK(action.midi_note == commissioning_key_to_midi_note(3u));
    CHECK(action.velocity == 100u);
    CHECK(state.key_down[3u] == 1u);

    action = rec_mode_captured_event(&state, REC_MODE_SOLE, 3u, 10u);
    CHECK(!action.emit_on && action.emit_off);
    CHECK(state.key_down[3u] == 0u);

    action = rec_mode_captured_event(&state, REC_MODE_SOLE, 3u, 10u);
    CHECK(action.emit_on);
    action = rec_mode_captured_event(&state, REC_MODE_SOLE, 3u, 10u);
    CHECK(action.emit_off);
}

static void test_raz(void) {
    const listen_params_t params = fixed_params(100u);
    rec_state_t state = new_state(&params);
    rec_action_t action;

    action = rec_mode_captured_event(&state, REC_MODE_SOLE, 5u, 10u);
    CHECK(action.emit_on);
    action = rec_mode_captured_event(&state, REC_MODE_SOLE, 5u, 10u);
    CHECK(action.emit_off);
    rec_mode_raz(&state);
    CHECK(state.key_down[5u] == 0u);
    // Alternance remise à zéro : la prochaine impulsion est un appui.
    action = rec_mode_captured_event(&state, REC_MODE_SOLE, 5u, 10u);
    CHECK(action.emit_on);
}

static void test_fusion_with_matrix(void) {
    const listen_params_t params = fixed_params(100u);
    const uint8_t key = 25u;  // key 25 -> MIDI 60
    const uint8_t note = commissioning_key_to_midi_note(key);
    rec_state_t state = new_state(&params);
    rec_action_t action;

    // Matrice d'abord.
    action = rec_mode_matrix_event(&state, REC_MODE_SOLE, note, true);
    CHECK(action.emit_on);
    action = rec_mode_captured_event(&state, REC_MODE_SOLE, key, 10u);
    CHECK(!action.emit_on && !action.emit_off);
    action = rec_mode_matrix_event(&state, REC_MODE_SOLE, note, false);
    CHECK(!action.emit_on && !action.emit_off);
    action = rec_mode_captured_event(&state, REC_MODE_SOLE, key, 10u);
    CHECK(!action.emit_on && action.emit_off);

    // Écoute d'abord.
    memset(&state, 0, sizeof(state));
    rec_mode_set_params(&state, &params);
    action = rec_mode_captured_event(&state, REC_MODE_SOLE, key, 10u);
    CHECK(action.emit_on);
    action = rec_mode_matrix_event(&state, REC_MODE_SOLE, note, true);
    CHECK(!action.emit_on && !action.emit_off);
    action = rec_mode_captured_event(&state, REC_MODE_SOLE, key, 10u);
    CHECK(!action.emit_on && !action.emit_off);
    action = rec_mode_matrix_event(&state, REC_MODE_SOLE, note, false);
    CHECK(!action.emit_on && action.emit_off);
}

static void test_enter_mute_vs_sole(void) {
    rec_off_list_t offs;
    rec_state_t state = {0};
    rec_refusal_t refusal;
    rec_action_t action;

    // MUTE : les notes matrice actives sont rendues et le compteur remis à 0.
    rec_mode_seed_matrix_note(&state, 60u);
    rec_mode_seed_matrix_note(&state, 60u);
    rec_mode_seed_matrix_note(&state, 64u);
    refusal = rec_mode_enter(&state, REC_MODE_MUTE, true, false, false, false, &offs);
    CHECK(refusal == REC_OK);
    CHECK(offs.count == 2u);
    CHECK(offs.note[0] == 60u && offs.note[1] == 64u);
    CHECK(state.matrix_ref[60u] == 0u && state.matrix_ref[64u] == 0u);
    CHECK(state.key_down[1u] == 0u);
    action = rec_mode_matrix_event(&state, REC_MODE_MUTE, 60u, true);
    CHECK(!action.emit_on && !action.emit_off);

    // SOLE : MIDI matrice conservé, compteurs intacts.
    memset(&state, 0, sizeof(state));
    rec_mode_seed_matrix_note(&state, 60u);
    rec_mode_seed_matrix_note(&state, 60u);
    refusal = rec_mode_enter(&state, REC_MODE_SOLE, true, false, false, false, &offs);
    CHECK(refusal == REC_OK);
    CHECK(offs.count == 0u);
    CHECK(state.matrix_ref[60u] == 2u);
    action = rec_mode_matrix_event(&state, REC_MODE_SOLE, 60u, true);
    CHECK(!action.emit_on && !action.emit_off);  // déjà tenue
    action = rec_mode_matrix_event(&state, REC_MODE_SOLE, 61u, true);
    CHECK(action.emit_on);
}

static void test_refusals(void) {
    rec_off_list_t offs;
    rec_state_t state = {0};

    CHECK(rec_mode_enter(&state, REC_MODE_SOLE, false, false, false, false, &offs) ==
          REC_NO_TABLE);
    CHECK(rec_mode_enter(&state, REC_MODE_SOLE, true, true, false, false, &offs) ==
          REC_SELFTEST);
    CHECK(rec_mode_enter(&state, REC_MODE_SOLE, true, false, true, false, &offs) ==
          REC_PROFILE_SYNC);
    CHECK(rec_mode_enter(&state, REC_MODE_SOLE, true, false, false, true, &offs) ==
          REC_MAINTENANCE);
    CHECK(strcmp(rec_refusal_name(REC_NO_TABLE), "REC_NO_TABLE") == 0);
}

static void test_lookup_and_neighbors(void) {
    commissioning_note_target_t target;
    const commissioning_note_target_t *card_a_key1 = &g_runtime.notes[0];
    const commissioning_note_target_t *card_a_key2 = &g_runtime.notes[1];
    const commissioning_note_target_t *card_b_key3 = &g_runtime.notes[2];
    const commissioning_note_target_t *card_b_key4 = &g_runtime.notes[3];
    uint8_t key = 0u;

    CHECK(rec_mode_lookup_uid_channel(&g_runtime, card_a_key1->uid_tag, 1u, &key));
    CHECK(key == 1u);
    CHECK(rec_mode_lookup_uid_channel(&g_runtime, card_a_key2->uid_tag, 0u, &key));
    CHECK(key == 2u);
    CHECK(!rec_mode_lookup_uid_channel(&g_runtime, card_a_key1->uid_tag, 5u, &key));
    CHECK(!rec_mode_lookup_uid_channel(&g_runtime, card_a_key1->uid_tag, 12u, &key));
    CHECK(rec_mode_lookup_uid_channel(&g_runtime, card_b_key3->uid_tag, 1u, &key) && key == 3u);
    CHECK(rec_mode_lookup_uid_channel(&g_runtime, card_b_key4->uid_tag, 0u, &key) && key == 4u);

    // Voisins k-1 / k+1 sur la bonne carte et le bon canal.
    CHECK(rec_mode_lookup_neighbor(&g_runtime, 2u, -1, &target));
    CHECK(target.key == 1u && target.position == 3u && target.channel == 1u);
    CHECK(rec_mode_lookup_neighbor(&g_runtime, 2u, 1, &target));
    CHECK(target.key == 3u && target.position == 4u && target.channel == 1u);
    CHECK(rec_mode_lookup_neighbor(&g_runtime, 3u, 1, &target));
    CHECK(target.key == 4u && target.position == 4u && target.channel == 0u);

    // Bords et voisin non attribué.
    CHECK(!rec_mode_lookup_neighbor(&g_runtime, 1u, -1, &target));
    CHECK(!rec_mode_lookup_neighbor(&g_runtime, 4u, 1, &target));
    CHECK(!rec_mode_lookup_neighbor(&g_runtime, 49u, 1, &target));
    CHECK(!rec_mode_lookup_neighbor(&g_runtime, 2u, 0, &target));
}

static void test_velocity(void) {
    listen_params_t params = fixed_params(90u);

    CHECK(rec_mode_velocity(&params, 0u) == 90u);
    CHECK(rec_mode_velocity(&params, 500u) == 90u);

    params.vel_fixed = 100u;
    params.vel_min_delta = 10u;
    params.vel_max_delta = 110u;
    CHECK(rec_mode_velocity(&params, 0u) == 1u);
    CHECK(rec_mode_velocity(&params, 10u) == 1u);
    CHECK(rec_mode_velocity(&params, 60u) == 64u);
    CHECK(rec_mode_velocity(&params, 110u) == 127u);
    CHECK(rec_mode_velocity(&params, 500u) == 127u);

    // delta = min + (max-min)/2 -> 1 + 63 = 64 ci-dessus ; borne basse/haute.
    CHECK(rec_mode_velocity(NULL, 50u) != 0u);
}

static void test_parse_and_names(void) {
    uint8_t note = 0u;
    char text[16];

    CHECK(rec_mode_parse_note("MI3", &note) && note == 52u);
    CHECK(rec_mode_parse_note("DO2", &note) && note == 36u);
    CHECK(rec_mode_parse_note("SI8", &note) && note == 119u);
    CHECK(rec_mode_parse_note("RE#3", &note) && note == 51u);
    CHECK(rec_mode_parse_note("SIB3", &note) && note == 58u);
    CHECK(rec_mode_parse_note("52", &note) && note == 52u);
    CHECK(!rec_mode_parse_note("128", &note));
    CHECK(!rec_mode_parse_note("XYZ", &note));
    CHECK(!rec_mode_parse_note("", &note));
    CHECK(!rec_mode_parse_note(NULL, &note));

    CHECK(rec_mode_note_name(52u, text, sizeof(text)));
    CHECK(strcmp(text, "MI3") == 0);
    CHECK(rec_mode_note_name(51u, text, sizeof(text)));
    CHECK(strcmp(text, "RE#3") == 0);
    CHECK(rec_mode_parse_note(text, &note) && note == 51u);
    CHECK(!rec_mode_note_name(5u, text, sizeof(text)));
}

static void test_resolve_touch(void) {
    commissioning_note_target_t target;
    uint8_t note = 0u;

    CHECK(rec_mode_resolve_touch(&g_runtime, "RE2", &note, &target));
    CHECK(note == 38u && target.key == 3u);
    CHECK(rec_mode_resolve_touch(&g_runtime, "DO2", &note, &target));
    CHECK(note == 36u && target.key == 1u);
    CHECK(!rec_mode_resolve_touch(&g_runtime, "MI8", &note, &target));
    CHECK(!rec_mode_resolve_touch(&g_runtime, "XYZ", &note, &target));
}

static void test_noise_toggle(void) {
    rec_state_t state = {0};
    bool enabled = false;

    CHECK(!rec_mode_noise_toggle(&state, false, &enabled));
    CHECK(!enabled && !state.noise_enabled);
    CHECK(rec_mode_noise_toggle(&state, true, &enabled));
    CHECK(enabled && state.noise_enabled);
    CHECK(rec_mode_noise_toggle(&state, true, &enabled));
    CHECK(!enabled && !state.noise_enabled);
}

static void test_trace_summary(void) {
    rec_state_t state = {0};
    rec_trace_summary_t summary;
    can_listen_trace_t frame;
    uint32_t index = 0u;

    rec_mode_trace_reset(&state);
    state.params = NULL;

    while (index < CAN_LISTEN_TRACE_SAMPLES) {
        for (uint8_t i = 0u; i < CAN_LISTEN_TRACE_FRAME_SAMPLES; ++i) {
            uint32_t global = index + i;

            if (global < REC_TRACE_PRE_TRIGGER) {
                frame.samples[i] = (uint16_t)(10u + (global % 11u));  // 10..20
            } else {
                frame.samples[i] = 30u;
            }
            if (global == 2000u) {
                frame.samples[i] = 100u;
            }
        }
        index += CAN_LISTEN_TRACE_FRAME_SAMPLES;
        if (index < CAN_LISTEN_TRACE_SAMPLES) {
            frame.seq = (uint16_t)(index / CAN_LISTEN_TRACE_FRAME_SAMPLES);
            rec_mode_trace_feed(&state, &frame);
        }
    }
    // Il manque la dernière trame : le résumé doit encore être refusé.
    CHECK(!rec_mode_trace_summary(&state, 20u, &summary));

    frame.seq = 1023u;
    rec_mode_trace_feed(&state, &frame);
    CHECK(state.trace_count == CAN_LISTEN_TRACE_SAMPLES);
    CHECK(rec_mode_trace_summary(&state, 20u, &summary));
    CHECK(summary.base == 10u);
    CHECK(summary.pic == 100u);
    CHECK(summary.delta == 90u);
    CHECK(summary.bruit_cc == 10u);
    CHECK(summary.t_pic_ms == 40u);
    CHECK(summary.count == CAN_LISTEN_TRACE_SAMPLES);
}

static void test_params_helpers(void) {
    listen_params_t params = k_listen_config;
    const char *bad_name = NULL;
    char diff[512];
    uint32_t value = 0u;

    CHECK(rec_mode_param_get(&params, "rise_thr", &value) && value == 5u);
    CHECK(rec_mode_param_set(&params, "rise_thr", 6u));
    CHECK(rec_mode_param_get(&params, "rise_thr", &value) && value == 6u);
    CHECK(!rec_mode_param_set(&params, "inconnu", 1u));
    CHECK(!rec_mode_param_get(&params, "inconnu", &value));

    CHECK(rec_mode_params_validate(&params, &bad_name) == LISTEN_PARAMS_OK);
    CHECK(bad_name == NULL);

    CHECK(rec_mode_param_set(&params, "settle_us", 20u));
    CHECK(rec_mode_params_diff(&params, diff, sizeof(diff)) == 2u);
    CHECK(strstr(diff, ".rise_thr = 6,") != NULL);
    CHECK(strstr(diff, ".settle_us = 20,") != NULL);

    params.return_thr = params.rise_thr;
    CHECK(rec_mode_params_validate(&params, &bad_name) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_name != NULL && strcmp(bad_name, "return_thr") == 0);

    params = k_listen_config;
    CHECK(rec_mode_params_diff(&params, diff, sizeof(diff)) == 0u);
    CHECK(diff[0] == '\0');
}

int main(void) {
    build_two_cards();

    test_alternation();
    test_raz();
    test_fusion_with_matrix();
    test_enter_mute_vs_sole();
    test_refusals();
    test_lookup_and_neighbors();
    test_velocity();
    test_parse_and_names();
    test_resolve_touch();
    test_noise_toggle();
    test_trace_summary();
    test_params_helpers();

    printf("%d verification(s), %d echec(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
