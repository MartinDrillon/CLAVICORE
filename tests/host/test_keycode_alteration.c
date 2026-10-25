// Tests unitaires hôte de la résolution d'altération à deux modificateurs
// (ADR-0026) : bascule par racine, échange de couple, conflit « dernière
// pression gagne », bornes MIDI et keymap.
#include <stdio.h>
#include <string.h>

#include "keycode.h"
#include "keymap.h"
#include "note_state.h"

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

static void test_no_modifier(void) {
    keycode_state_t state;

    keycode_state_init(&state);
    CHECK(keycode_alteration_for_note(&state, 62u) == 0);
}

static void test_natural_root(void) {
    keycode_state_t state;
    uint8_t root;
    uint8_t new_state;

    // B + RE (62) : racine RE (index 1), etat 2 (bemol), RE -> -1, DO#/REb -> +1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(root == 1u);
    CHECK(new_state == 2u);
    CHECK(keycode_alteration_for_note(&state, 62u) == -1);
    CHECK(keycode_alteration_for_note(&state, 61u) == 1);

    // D + RE (62) : racine RE, etat 1 (diese), RE -> +1, RE# -> -1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(root == 1u);
    CHECK(new_state == 1u);
    CHECK(keycode_alteration_for_note(&state, 62u) == 1);
    CHECK(keycode_alteration_for_note(&state, 63u) == -1);
}

static void test_black_keys(void) {
    keycode_state_t state;
    uint8_t root;
    uint8_t new_state;

    // D + DO# (61) : la naturelle sous la touche noire -> racine DO.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 61u, &root, &new_state));
    CHECK(root == 0u);
    CHECK(new_state == 1u);
    CHECK(keycode_alteration_for_note(&state, 60u) == 1);
    CHECK(keycode_alteration_for_note(&state, 61u) == -1);

    // B + DO# (61) : la naturelle au-dessus -> racine RE, etat 2.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 61u, &root, &new_state));
    CHECK(root == 1u);
    CHECK(new_state == 2u);

    // D + RE# (63) : racine RE.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 63u, &root, &new_state));
    CHECK(root == 1u);
    CHECK(new_state == 1u);

    // B + RE# (63) : racine MI, MI -> +1, FA -> -1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 63u, &root, &new_state));
    CHECK(root == 2u);
    CHECK(new_state == 2u);
    CHECK(keycode_alteration_for_note(&state, 63u) == 1);
    CHECK(keycode_alteration_for_note(&state, 64u) == -1);
}

static void test_toggle_round_trip(void) {
    keycode_state_t state;
    uint8_t root;
    uint8_t new_state;

    // D deux fois sur RE : +1 puis retour a 0.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 1);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 0);

    // B puis D sur RE : etat 2 puis 1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == -1);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 1);

    // D puis B sur RE : etat 1 puis 2.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 1);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == -1);

    // B deux fois : retour a 0.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == -1);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 0);
}

static void test_conflict_last_wins(void) {
    keycode_state_t state;
    uint8_t root;
    uint8_t new_state;

    // RE# (63) est revendique par RE (diese) et par MI (bemol).
    // D + RE puis B + MI : MI est le plus recent -> +1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 63u) == 1);

    // Ordre inverse : RE est le plus recent -> -1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 63u) == -1);

    // D + RE, puis B + MI deux fois : MI revient a l'etat 0, RE l'emporte -> -1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 63u) == -1);
}

static void test_si_do_swap(void) {
    keycode_state_t state;
    uint8_t root;
    uint8_t new_state;

    // D + SI (71) : SI -> +1, DO -> -1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 71u, &root, &new_state));
    CHECK(root == 6u);
    CHECK(keycode_alteration_for_note(&state, 71u) == 1);
    CHECK(keycode_alteration_for_note(&state, 72u) == -1);

    // D + SI d'une octave plus bas (59).
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 59u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 59u) == 1);
    CHECK(keycode_alteration_for_note(&state, 60u) == -1);

    // B + DO (60) : DO -> -1, SI -> +1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 60u, &root, &new_state));
    CHECK(root == 0u);
    CHECK(keycode_alteration_for_note(&state, 60u) == -1);
    CHECK(keycode_alteration_for_note(&state, 59u) == 1);
}

static void test_midi_bounds_and_order(void) {
    keycode_state_t state;
    note_state_t notes;
    uint8_t root;
    uint8_t new_state;
    uint8_t midi_note;

    // Base 12 (DO), B + DO, global -20 : alteration avant transposition, clamp a 0.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 12u, &root, &new_state));
    state.global_semitones = -20;
    CHECK(note_state_press(&notes, &state, 62u, 12u, &midi_note));
    CHECK(midi_note == 0u);

    // Base 119 (SI), D + SI, global +10 : clamp a 127.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 119u, &root, &new_state));
    state.global_semitones = 10;
    CHECK(note_state_press(&notes, &state, 62u, 119u, &midi_note));
    CHECK(midi_note == 127u);

    // Alteration avant transposition : D + SI sur 71 avec +2 -> 74.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 71u, &root, &new_state));
    state.global_semitones = 2;
    CHECK(note_state_press(&notes, &state, 62u, 71u, &midi_note));
    CHECK(midi_note == 74u);
}

static void test_keycode_press_is_inert(void) {
    keycode_state_t state;

    keycode_state_init(&state);
    keycode_press(&state, KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_B));
    keycode_press(&state, KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_D));

    for (uint8_t root = 0u; root < KEYCODE_ALTERATION_ROOT_COUNT; ++root) {
        CHECK(state.alteration_state[root] == 0u);
        CHECK(state.alteration_change[root] == 0u);
    }
    CHECK(state.change_counter == 0u);
}

static void test_untranspose_clears_alteration(void) {
    keycode_state_t state;
    uint8_t root;
    uint8_t new_state;

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 64u, &root, &new_state));
    keycode_press(&state, KEYCODE_BEHAVIOR(KEYCODE_UNTRANSPOSE));

    CHECK(keycode_alteration_for_note(&state, 62u) == 0);
    CHECK(keycode_alteration_for_note(&state, 63u) == 0);
    CHECK(keycode_alteration_for_note(&state, 64u) == 0);
}

static void test_held_note_is_not_modified(void) {
    keycode_state_t state;
    note_state_t notes;
    uint8_t root;
    uint8_t new_state;
    uint8_t midi_note;

    // Note tenue avant le modificateur : son Note Off reste la note memorisee.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(note_state_press(&notes, &state, 62u, 62u, &midi_note));
    CHECK(midi_note == 62u);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(note_state_release(&notes, 62u, &midi_note));
    CHECK(midi_note == 62u);
}

static void test_keymap(void) {
    CHECK(KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_B) == 0x0800u);
    CHECK(KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_D) == 0x0801u);

    CHECK(keymap_keycode_for_position(74u).kind == KEYCODE_ALTERATION);
    CHECK(keymap_keycode_for_position(74u).value == KEYCODE_ALTERATION_B);
    CHECK(keymap_keycode_for_position(75u).kind == KEYCODE_ALTERATION);
    CHECK(keymap_keycode_for_position(75u).value == KEYCODE_ALTERATION_D);

    CHECK(keymap_keycode_for_position(81u).kind == KEYCODE_NONE);
    CHECK(keymap_keycode_for_position(82u).kind == KEYCODE_NONE);
    CHECK(keymap_keycode_for_position(83u).kind == KEYCODE_NONE);
    CHECK(keymap_keycode_for_position(61u).kind == KEYCODE_NONE);
    CHECK(keymap_keycode_for_position(72u).kind == KEYCODE_NONE);
    CHECK(keymap_keycode_for_position(73u).kind == KEYCODE_UNTRANSPOSE);

    for (uint8_t position = 1u; position <= KEYMAP_POSITION_COUNT; ++position) {
        if (keymap_keycode_for_position(position).kind == KEYCODE_ALTERATION) {
            CHECK(position == 74u || position == 75u);
        }
    }

    CHECK(strcmp(keycode_name(KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_B)), "A_b") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_D)), "A_d") == 0);
}

int main(void) {
    test_no_modifier();
    test_natural_root();
    test_black_keys();
    test_toggle_round_trip();
    test_conflict_last_wins();
    test_si_do_swap();
    test_midi_bounds_and_order();
    test_keycode_press_is_inert();
    test_untranspose_clears_alteration();
    test_held_note_is_not_modified();
    test_keymap();

    printf("test_keycode_alteration: %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
