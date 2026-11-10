// Tests unitaires hôte de la résolution d'altération à deux modificateurs
// (ADR-0026) et des armures / altérations temporaires (ADR-0029).
//
// Aucune assertion de POSITION de keymap : ADR-0029 §7, les positions ne sont
// normatives nulle part. On teste les valeurs de keycode et les comportements.
#include <stdio.h>
#include <string.h>

#include "keycode.h"
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

// Racines naturelles dans l'ordre des classes {DO,RE,MI,FA,SOL,LA,SI}.
static const uint8_t k_natural_pitch[7] = { 0u, 2u, 4u, 5u, 7u, 9u, 11u };

// Table de référence indépendante des 14 armures (mode x tonique x classe) :
// 0 naturel, 1 dièse, 2 bémol. Identique à celle de l'ADR-0029 §3.
static const uint8_t k_armure_ref[2][7][7] = {
    {
        {0u, 0u, 2u, 0u, 0u, 2u, 2u}, // DO mineur
        {0u, 0u, 0u, 0u, 0u, 0u, 2u}, // RE mineur
        {0u, 0u, 0u, 1u, 0u, 0u, 0u}, // MI mineur
        {0u, 2u, 2u, 0u, 0u, 2u, 2u}, // FA mineur
        {0u, 0u, 2u, 0u, 0u, 0u, 2u}, // SOL mineur
        {0u, 0u, 0u, 0u, 0u, 0u, 0u}, // LA mineur
        {1u, 0u, 0u, 1u, 0u, 0u, 0u}, // SI mineur
    },
    {
        {0u, 0u, 0u, 0u, 0u, 0u, 0u}, // DO majeur
        {1u, 0u, 0u, 1u, 0u, 0u, 0u}, // RE majeur
        {1u, 1u, 0u, 1u, 1u, 0u, 0u}, // MI majeur
        {0u, 0u, 0u, 0u, 0u, 0u, 2u}, // FA majeur
        {0u, 0u, 0u, 1u, 0u, 0u, 0u}, // SOL majeur
        {1u, 0u, 0u, 1u, 1u, 0u, 0u}, // LA majeur
        {1u, 1u, 0u, 1u, 1u, 1u, 0u}, // SI majeur
    },
};

static void modifier_hold(keycode_modifiers_t *modifiers, uint8_t family, uint8_t direction) {
    keycode_modifiers_update(modifiers, family, direction, true);
}

static void modifier_release(keycode_modifiers_t *modifiers, uint8_t family,
                             uint8_t direction) {
    keycode_modifiers_update(modifiers, family, direction, false);
}

static int16_t delta_at(const keycode_modifiers_t *modifiers, uint8_t position) {
    return keycode_momentary_delta(modifiers, position);
}

static void test_keycode_values(void) {
    CHECK(NONE == 0x0000u);
    CHECK(ALT_B == 0x0800u);
    CHECK(ALT_D == 0x0801u);
    CHECK(UNTRANSPOSE == 0x0B00u);
    CHECK(MAINT == 0x0A00u);
    CHECK(GET_UID == 0x0900u);
    CHECK(T_GLOB_UP == 0x0200u);
    CHECK(T_GLOB_DOWN == 0x0300u);
    CHECK(T_OCT_L_UP == 0x0400u);
    CHECK(T_OCT_L_DOWN == 0x0500u);
    CHECK(T_OCT_R_UP == 0x0600u);
    CHECK(T_OCT_R_DOWN == 0x0700u);
    CHECK(CLE_B == 0x0C00u);
    CHECK(CLE_D == 0x0C01u);
    CHECK(TEMP_B == 0x0D00u);
    CHECK(TEMP_D == 0x0D01u);
    CHECK(KEYCODE_CLE == 12);
    CHECK(KEYCODE_TEMP == 13);
}

static void test_temp_transpose_values(void) {
    // ADR-0030 : kinds en fin d'enum, valeurs de la table §1.
    CHECK(KEYCODE_TEMP_TRANSPOSE_ST == 14);
    CHECK(KEYCODE_TEMP_TRANSPOSE_OCT == 15);
    CHECK(KEYCODE_TEMP_TRANSPOSE_L_UP == 0);
    CHECK(KEYCODE_TEMP_TRANSPOSE_L_DOWN == 1);
    CHECK(KEYCODE_TEMP_TRANSPOSE_R_UP == 2);
    CHECK(KEYCODE_TEMP_TRANSPOSE_R_DOWN == 3);

    CHECK(T_TEMP_ST_L_UP == 0x0E00u);
    CHECK(T_TEMP_ST_L_DOWN == 0x0E01u);
    CHECK(T_TEMP_ST_R_UP == 0x0E02u);
    CHECK(T_TEMP_ST_R_DOWN == 0x0E03u);
    CHECK(T_TEMP_OCT_L_UP == 0x0F00u);
    CHECK(T_TEMP_OCT_L_DOWN == 0x0F01u);
    CHECK(T_TEMP_OCT_R_UP == 0x0F02u);
    CHECK(T_TEMP_OCT_R_DOWN == 0x0F03u);

    CHECK(((T_TEMP_ST_L_UP >> 8u) & 0xFFu) == (uint16_t)KEYCODE_TEMP_TRANSPOSE_ST);
    CHECK((T_TEMP_ST_L_UP & 0xFFu) == (uint16_t)KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(((T_TEMP_OCT_R_DOWN >> 8u) & 0xFFu) == (uint16_t)KEYCODE_TEMP_TRANSPOSE_OCT);
    CHECK((T_TEMP_OCT_R_DOWN & 0xFFu) == (uint16_t)KEYCODE_TEMP_TRANSPOSE_R_DOWN);
}

static void test_armure_table_and_audible(void) {
    for (uint8_t mode = 0u; mode < 2u; ++mode) {
        const keycode_cle_t cle = (mode == 0u) ? KEYCODE_CLE_MINOR : KEYCODE_CLE_MAJOR;
        for (uint8_t tonic = 0u; tonic < 7u; ++tonic) {
            keycode_state_t state;
            bool applied = false;
            const uint8_t base = (uint8_t)(60u + k_natural_pitch[tonic]);

            keycode_state_init(&state);
            CHECK(keycode_armure_apply(&state, cle, base, &applied));
            CHECK(applied);

            // État d'armure exact par classe.
            for (uint8_t c = 0u; c < 7u; ++c) {
                CHECK(state.armure_state[c] == k_armure_ref[mode][tonic][c]);
            }

            // Notes audibles : chaque naturelle est altérée selon sa classe.
            for (uint8_t c = 0u; c < 7u; ++c) {
                const uint8_t note = (uint8_t)(60u + k_natural_pitch[c]);
                int8_t expected = 0;
                if (k_armure_ref[mode][tonic][c] == 1u) {
                    expected = 1;
                } else if (k_armure_ref[mode][tonic][c] == 2u) {
                    expected = -1;
                }
                CHECK(keycode_alteration_for_note(&state, note) == expected);
            }
        }
    }
}

static void test_armure_black_tonic_ignored(void) {
    keycode_state_t state;
    bool applied = true;

    // DO# (61) n'est pas une tonique naturelle : armure inchangée, pas d'erreur.
    keycode_state_init(&state);
    CHECK(keycode_armure_apply(&state, KEYCODE_CLE_MAJOR, 61u, &applied));
    CHECK(!applied);
    for (uint8_t c = 0u; c < 7u; ++c) {
        CHECK(state.armure_state[c] == 0u);
    }
    CHECK(state.change_counter == 0u);
    CHECK(keycode_alteration_for_note(&state, 62u) == 0);
}

static void test_armure_persists_and_untranspose(void) {
    keycode_state_t state;
    bool applied = false;

    // Armure SOL mineur (sib, mib) : persiste, UNTRANSPOSE remet tout à zéro.
    keycode_state_init(&state);
    CHECK(keycode_armure_apply(&state, KEYCODE_CLE_MINOR, 67u, &applied));
    CHECK(applied);
    CHECK(keycode_alteration_for_note(&state, 71u) == -1); // SI -> sib
    CHECK(keycode_alteration_for_note(&state, 64u) == -1); // MI -> mib

    // Le relâchement n'existe pas côté state : l'armure reste.
    CHECK(keycode_alteration_for_note(&state, 71u) == -1);

    keycode_press(&state, KEYCODE_BEHAVIOR(KEYCODE_UNTRANSPOSE));
    CHECK(keycode_alteration_for_note(&state, 71u) == 0);
    CHECK(keycode_alteration_for_note(&state, 64u) == 0);
    CHECK(state.armure_change == 0u);
}

static void test_alt_overlay_on_armure(void) {
    keycode_state_t state;
    bool applied = false;
    uint8_t root;
    uint8_t new_state;

    // Armure SOL majeur : FA# (classe FA dièse). keycode_alteration_for_note
    // sur FA (65) donne +1.
    keycode_state_init(&state);
    CHECK(keycode_armure_apply(&state, KEYCODE_CLE_MAJOR, 67u, &applied));
    CHECK(applied);
    CHECK(keycode_alteration_for_note(&state, 65u) == 1);

    // Pose d'une surcouche ALT_B sur FA : FA devient bémol (état 2).
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 65u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 65u) == -1);

    // Retrait (second appui de même sens) : retour à l'armure (FA#).
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 65u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 65u) == 1);
}

static void test_enharmonic_conflict_with_armure(void) {
    keycode_state_t state;
    bool applied = false;
    uint8_t root;
    uint8_t new_state;

    // MI majeur : classe RE dièse -> RE# (63) ramenée d'un demi-ton (63 -> 62).
    keycode_state_init(&state);
    CHECK(keycode_armure_apply(&state, KEYCODE_CLE_MAJOR, 64u, &applied));
    CHECK(applied);
    CHECK(keycode_alteration_for_note(&state, 63u) == -1);

    // ALT_B sur MI : la classe MI (bémol) revendique aussi 63 -> +1, plus
    // récente -> elle gagne.
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 63u) == 1);

    // Retrait de la surcouche : la classe RE (armure) reprend la main -> -1.
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 63u) == -1);
}

static void test_new_armure_clears_overlay(void) {
    keycode_state_t state;
    bool applied = false;
    uint8_t root;
    uint8_t new_state;

    // Surcouche ALT_B sur DO, puis nouvelle armure : la surcouche disparaît.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 60u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 60u) == -1);

    CHECK(keycode_armure_apply(&state, KEYCODE_CLE_MAJOR, 67u, &applied)); // SOL majeur
    CHECK(applied);
    CHECK(keycode_alteration_for_note(&state, 60u) == 0);  // DO naturel
    CHECK(keycode_alteration_for_note(&state, 65u) == 1);  // FA#
    for (uint8_t c = 0u; c < 7u; ++c) {
        CHECK(state.alteration_state[c] == 0u);
    }
}

static void test_temp_delta_table(void) {
    static const int8_t flat[12] = { -1, +1, -1, +1, -1, -1, +1, -1, +1, -1, +1, -1 };

    for (int16_t pc = 0; pc < 12; ++pc) {
        CHECK(keycode_temp_delta(KEYCODE_TEMP_FLAT, pc) == flat[pc]);
        CHECK(keycode_temp_delta(KEYCODE_TEMP_SHARP, pc) == -flat[pc]);
    }

    // Octaves et note négative (bornage géré plus loin dans note_state).
    CHECK(keycode_temp_delta(KEYCODE_TEMP_FLAT, 60) == flat[0]);
    CHECK(keycode_temp_delta(KEYCODE_TEMP_FLAT, 71) == flat[11]);
    CHECK(keycode_temp_delta(KEYCODE_TEMP_FLAT, -1) == flat[11]);
    CHECK(keycode_temp_delta(KEYCODE_TEMP_SHARP, 70) == -flat[10]); // sib -> la
}

static void test_temp_notes(void) {
    keycode_state_t state;
    note_state_t notes;
    uint8_t midi_note;

    // TEMP_B : fa->mi, fa#->sol, sol#->la, la->lab (aucune armure).
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(note_state_press(&notes, &state, 62u, 65u, (int8_t)KEYCODE_TEMP_FLAT, 0, &midi_note));
    CHECK(midi_note == 64u); // fa -> mi
    CHECK(note_state_press(&notes, &state, 62u, 66u, (int8_t)KEYCODE_TEMP_FLAT, 0, &midi_note));
    CHECK(midi_note == 67u); // fa# -> sol
    CHECK(note_state_press(&notes, &state, 62u, 68u, (int8_t)KEYCODE_TEMP_FLAT, 0, &midi_note));
    CHECK(midi_note == 69u); // sol# -> la
    CHECK(note_state_press(&notes, &state, 62u, 69u, (int8_t)KEYCODE_TEMP_FLAT, 0, &midi_note));
    CHECK(midi_note == 68u); // la -> lab

    // TEMP_D : mi->fa, fa->fa#, sib->la.
    CHECK(note_state_press(&notes, &state, 62u, 64u, (int8_t)KEYCODE_TEMP_SHARP, 0, &midi_note));
    CHECK(midi_note == 65u); // mi -> fa
    CHECK(note_state_press(&notes, &state, 62u, 65u, (int8_t)KEYCODE_TEMP_SHARP, 0, &midi_note));
    CHECK(midi_note == 66u); // fa -> fa#
    CHECK(note_state_press(&notes, &state, 62u, 70u, (int8_t)KEYCODE_TEMP_SHARP, 0, &midi_note));
    CHECK(midi_note == 69u); // sib -> la
}

static void test_temp_chain_and_bounds(void) {
    keycode_state_t state;
    note_state_t notes;
    bool applied = false;
    uint8_t midi_note;

    // Armure RE mineur (sib) + TEMP_D sur SI (71) : 71 -> 70 (sib) -> 69 (la).
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(keycode_armure_apply(&state, KEYCODE_CLE_MINOR, 62u, &applied));
    CHECK(applied);
    CHECK(note_state_press(&notes, &state, 62u, 71u, (int8_t)KEYCODE_TEMP_SHARP, 0, &midi_note));
    CHECK(midi_note == 69u);

    // LA (69) + TEMP_B + transposition globale +2 : 69 -> 68 -> 70.
    keycode_state_init(&state);
    note_state_init(&notes);
    state.global_semitones = 2;
    CHECK(note_state_press(&notes, &state, 62u, 69u, (int8_t)KEYCODE_TEMP_FLAT, 0, &midi_note));
    CHECK(midi_note == 70u);

    // Base 0 + TEMP_B : -1 borné à 0.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(note_state_press(&notes, &state, 62u, 0u, (int8_t)KEYCODE_TEMP_FLAT, 0, &midi_note));
    CHECK(midi_note == 0u);

    // Base 127 + TEMP_D : +1 borné à 127.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(note_state_press(&notes, &state, 62u, 127u, (int8_t)KEYCODE_TEMP_SHARP, 0, &midi_note));
    CHECK(midi_note == 127u);
}

static void test_held_note_is_not_modified(void) {
    keycode_state_t state;
    note_state_t notes;
    uint8_t midi_note;

    // Une note tenue garde la note mémorisée à l'appui, même si TEMP change.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(note_state_press(&notes, &state, 62u, 65u, (int8_t)KEYCODE_TEMP_FLAT, 0, &midi_note));
    CHECK(midi_note == 64u);
    CHECK(note_state_release(&notes, 62u, &midi_note));
    CHECK(midi_note == 64u);
}

// --- Règles ADR-0026 conservées (altération seule, sans armure) ---------------

static void test_no_modifier(void) {
    keycode_state_t state;

    keycode_state_init(&state);
    CHECK(keycode_alteration_for_note(&state, 62u) == 0);
}

static void test_natural_root(void) {
    keycode_state_t state;
    uint8_t root;
    uint8_t new_state;

    // B + RE (62) : racine RE (index 1), état 2, RE -> -1, DO#/REb -> +1.
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(root == 1u);
    CHECK(new_state == 2u);
    CHECK(keycode_alteration_for_note(&state, 62u) == -1);
    CHECK(keycode_alteration_for_note(&state, 61u) == 1);

    // D + RE (62) : racine RE, état 1, RE -> +1, RE# -> -1.
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

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 61u, &root, &new_state));
    CHECK(root == 0u);
    CHECK(new_state == 1u);
    CHECK(keycode_alteration_for_note(&state, 60u) == 1);
    CHECK(keycode_alteration_for_note(&state, 61u) == -1);

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 61u, &root, &new_state));
    CHECK(root == 1u);
    CHECK(new_state == 2u);

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 63u, &root, &new_state));
    CHECK(root == 1u);
    CHECK(new_state == 1u);

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

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 1);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 0);

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == -1);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 1);

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == 1);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 62u) == -1);

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

    // RE# (63) revendiqué par RE (dièse) et par MI (bémol).
    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 63u) == 1);

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 64u, &root, &new_state));
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 62u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 63u) == -1);

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

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 71u, &root, &new_state));
    CHECK(root == 6u);
    CHECK(keycode_alteration_for_note(&state, 71u) == 1);
    CHECK(keycode_alteration_for_note(&state, 72u) == -1);

    keycode_state_init(&state);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 59u, &root, &new_state));
    CHECK(keycode_alteration_for_note(&state, 59u) == 1);
    CHECK(keycode_alteration_for_note(&state, 60u) == -1);

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

    // Base 12 (DO), B + DO, global -20 : altération avant transposition, 0.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_B, 12u, &root, &new_state));
    state.global_semitones = -20;
    CHECK(note_state_press(&notes, &state, 62u, 12u, -1, 0, &midi_note));
    CHECK(midi_note == 0u);

    // Base 119 (SI), D + SI, global +10 : borné à 127.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 119u, &root, &new_state));
    state.global_semitones = 10;
    CHECK(note_state_press(&notes, &state, 62u, 119u, -1, 0, &midi_note));
    CHECK(midi_note == 127u);

    // Altération avant transposition : D + SI sur 71 avec +2 -> 74.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(keycode_alteration_toggle(&state, KEYCODE_ALTERATION_D, 71u, &root, &new_state));
    state.global_semitones = 2;
    CHECK(note_state_press(&notes, &state, 62u, 71u, -1, 0, &midi_note));
    CHECK(midi_note == 74u);
}

static void test_keycode_press_is_inert(void) {
    keycode_state_t state;

    keycode_state_init(&state);
    keycode_press(&state, KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_B));
    keycode_press(&state, KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_D));
    keycode_press(&state, KEYCODE_BEHAVIOR(KEYCODE_CLE));
    keycode_press(&state, KEYCODE_BEHAVIOR(KEYCODE_TEMP));

    for (uint8_t root = 0u; root < KEYCODE_ALTERATION_ROOT_COUNT; ++root) {
        CHECK(state.alteration_state[root] == 0u);
        CHECK(state.alteration_change[root] == 0u);
        CHECK(state.armure_state[root] == 0u);
    }
    CHECK(state.change_counter == 0u);
    CHECK(state.armure_change == 0u);
}

static void test_keycode_name(void) {
    CHECK(strcmp(keycode_name(KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_B)), "A_b") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_ALTERATION_VALUE(KEYCODE_ALTERATION_D)), "A_d") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_CLE)), "CLE_b") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_CLE, KEYCODE_CLE_MAJOR}), "CLE_d") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_TEMP)), "TEMP_b") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP, KEYCODE_TEMP_SHARP}), "TEMP_d") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_GET_UID)), "GET_UID") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_MAINTENANCE_TOGGLE)), "MAINT") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_UNTRANSPOSE)), "UNTRANSPOSE") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_GLOBAL_UP)), "T_GlobUp") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_GLOBAL_DOWN)), "T_GlobDown") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_LEFT_UP)), "T_OctupG") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_LEFT_DOWN)), "T_OctdownG") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_RIGHT_UP)), "T_OctupD") == 0);
    CHECK(strcmp(keycode_name(KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_RIGHT_DOWN)), "T_OctdownD") == 0);
}

// --- ADR-0030 : transpositions temporaires par côté -------------------------

static void test_position_side(void) {
    // Même définition que T_OCT_L/R (positions limites).
    CHECK(!keycode_position_is_left(60u)); // dernière touche gauche de la 5e rangée
    CHECK(!keycode_position_is_left(61u)); // case libre, côté droit
    CHECK(keycode_position_is_left(62u));
    CHECK(keycode_position_is_left(66u));
    CHECK(!keycode_position_is_left(67u));
}

static void test_momentary_delta_basic(void) {
    keycode_modifiers_t m;

    keycode_modifiers_init(&m);
    CHECK(delta_at(&m, 62u) == 0);
    CHECK(delta_at(&m, 67u) == 0);

    // Demi-ton, côté gauche.
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(delta_at(&m, 62u) == 1);
    CHECK(delta_at(&m, 67u) == 0); // côté opposé : aucun effet
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);

    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_DOWN);
    CHECK(delta_at(&m, 62u) == -1);
    CHECK(delta_at(&m, 67u) == 0);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_DOWN);

    // Demi-ton, côté droit.
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_R_UP);
    CHECK(delta_at(&m, 67u) == 1);
    CHECK(delta_at(&m, 62u) == 0);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_R_UP);

    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_R_DOWN);
    CHECK(delta_at(&m, 67u) == -1);
    CHECK(delta_at(&m, 62u) == 0);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_R_DOWN);

    // Octave, les deux côtés et les deux sens.
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(delta_at(&m, 62u) == 12);
    CHECK(delta_at(&m, 67u) == 0);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);

    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_DOWN);
    CHECK(delta_at(&m, 62u) == -12);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_DOWN);

    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_R_UP);
    CHECK(delta_at(&m, 67u) == 12);
    CHECK(delta_at(&m, 62u) == 0);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_R_UP);

    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_R_DOWN);
    CHECK(delta_at(&m, 67u) == -12);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_R_DOWN);

    // Deux axes additifs : +1 demi-ton +1 octave = +13, et -13.
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(delta_at(&m, 62u) == 13);
    CHECK(delta_at(&m, 67u) == 0);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);

    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_R_DOWN);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_R_DOWN);
    CHECK(delta_at(&m, 67u) == -13);
    CHECK(delta_at(&m, 62u) == 0);
}

static void test_momentary_conflict_last_wins(void) {
    keycode_modifiers_t m;

    // Même axe, même côté : la dernière pressée gagne ; relâcher la dernière
    // rend la main à l'autre si elle est toujours tenue, sinon à zéro.
    keycode_modifiers_init(&m);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_DOWN);
    CHECK(delta_at(&m, 62u) == -1);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_DOWN);
    CHECK(delta_at(&m, 62u) == 1);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(delta_at(&m, 62u) == 0);

    keycode_modifiers_init(&m);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_DOWN);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(delta_at(&m, 62u) == 12);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(delta_at(&m, 62u) == -12);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_DOWN);
    CHECK(delta_at(&m, 62u) == 0);

    // Côtés disjoints : maintien simultané L_UP et R_DOWN.
    keycode_modifiers_init(&m);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_R_DOWN);
    CHECK(delta_at(&m, 62u) == 1);
    CHECK(delta_at(&m, 67u) == -1);
}

static void test_momentary_with_persistent(void) {
    keycode_state_t state;
    note_state_t notes;
    keycode_modifiers_t m;
    uint8_t midi_note;

    // T_GLOB +1 et octave gauche persistante +1, plus momentané +13 : 86.
    keycode_state_init(&state);
    note_state_init(&notes);
    keycode_modifiers_init(&m);
    keycode_press(&state, KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_GLOBAL_UP));
    keycode_press(&state, KEYCODE_BEHAVIOR(KEYCODE_TRANSPOSE_LEFT_UP));
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(keycode_momentary_delta(&m, 62u) == 13);
    CHECK(note_state_press(&notes, &state, 62u, 60u, -1,
                           keycode_momentary_delta(&m, 62u), &midi_note));
    CHECK(midi_note == 86u); // 60 + 1 + 12 + 13

    // Côté droit : ni la transposition persistante gauche ni le momentané
    // gauche ne s'appliquent.
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_OCT, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(keycode_momentary_delta(&m, 67u) == 0);
    CHECK(note_state_press(&notes, &state, 67u, 60u, -1,
                           keycode_momentary_delta(&m, 67u), &midi_note));
    CHECK(midi_note == 61u); // 60 + T_GLOB seul
}

static void test_momentary_bounds(void) {
    keycode_state_t state;
    note_state_t notes;
    uint8_t midi_note;

    // Base 0 -1 : borné à 0.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(note_state_press(&notes, &state, 62u, 0u, -1, -1, &midi_note));
    CHECK(midi_note == 0u);

    // Base 127 +12 : borné à 127.
    keycode_state_init(&state);
    note_state_init(&notes);
    CHECK(note_state_press(&notes, &state, 62u, 127u, -1, 12, &midi_note));
    CHECK(midi_note == 127u);
}

static void test_held_note_keeps_pitch_after_release(void) {
    keycode_state_t state;
    note_state_t notes;
    keycode_modifiers_t m;
    uint8_t midi_note;

    // Note pressée sous transposition momentanée : hauteur mémorisée à
    // l'appui, conservée au relâchement du keycode et au Note Off.
    keycode_state_init(&state);
    note_state_init(&notes);
    keycode_modifiers_init(&m);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(note_state_press(&notes, &state, 62u, 60u, -1,
                           keycode_momentary_delta(&m, 62u), &midi_note));
    CHECK(midi_note == 61u);

    modifier_release(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(keycode_momentary_delta(&m, 62u) == 0);
    CHECK(note_state_release(&notes, 62u, &midi_note));
    CHECK(midi_note == 61u);

    // La note suivante, keycode relâché, reprend la hauteur de base.
    CHECK(note_state_press(&notes, &state, 62u, 60u, -1,
                           keycode_momentary_delta(&m, 62u), &midi_note));
    CHECK(midi_note == 60u);
}

static void test_untranspose_keeps_momentary(void) {
    keycode_state_t state;
    keycode_modifiers_t m;

    keycode_state_init(&state);
    keycode_modifiers_init(&m);
    modifier_hold(&m, KEYCODE_MOD_FAMILY_TEMP_ST, KEYCODE_TEMP_TRANSPOSE_L_UP);
    CHECK(keycode_momentary_delta(&m, 62u) == 1);

    keycode_press(&state, KEYCODE_BEHAVIOR(KEYCODE_UNTRANSPOSE));
    CHECK(state.change_counter == 0u);
    CHECK(keycode_momentary_delta(&m, 62u) == 1);
}

static void test_temp_transpose_press_is_inert(void) {
    keycode_state_t state;
    static const uint8_t directions[4] = { 0u, 1u, 2u, 3u };

    keycode_state_init(&state);
    for (uint8_t i = 0u; i < 4u; ++i) {
        keycode_press(&state, (keycode_t){KEYCODE_TEMP_TRANSPOSE_ST, directions[i]});
        keycode_press(&state, (keycode_t){KEYCODE_TEMP_TRANSPOSE_OCT, directions[i]});
    }
    CHECK(state.change_counter == 0u);
    CHECK(state.global_semitones == 0);
    CHECK(state.left_octaves == 0);
    CHECK(state.right_octaves == 0);
}

static void test_temp_transpose_names(void) {
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_ST,
                                         KEYCODE_TEMP_TRANSPOSE_L_UP}), "T_TEMP_ST_L_UP") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_ST,
                                         KEYCODE_TEMP_TRANSPOSE_L_DOWN}), "T_TEMP_ST_L_DOWN") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_ST,
                                         KEYCODE_TEMP_TRANSPOSE_R_UP}), "T_TEMP_ST_R_UP") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_ST,
                                         KEYCODE_TEMP_TRANSPOSE_R_DOWN}), "T_TEMP_ST_R_DOWN") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_OCT,
                                         KEYCODE_TEMP_TRANSPOSE_L_UP}), "T_TEMP_OCT_L_UP") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_OCT,
                                         KEYCODE_TEMP_TRANSPOSE_L_DOWN}), "T_TEMP_OCT_L_DOWN") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_OCT,
                                         KEYCODE_TEMP_TRANSPOSE_R_UP}), "T_TEMP_OCT_R_UP") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_OCT,
                                         KEYCODE_TEMP_TRANSPOSE_R_DOWN}), "T_TEMP_OCT_R_DOWN") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_ST, 4u}),
                 "T_TEMP_ST_invalid") == 0);
    CHECK(strcmp(keycode_name((keycode_t){KEYCODE_TEMP_TRANSPOSE_OCT, 4u}),
                 "T_TEMP_OCT_invalid") == 0);
}

int main(void) {
    test_keycode_values();
    test_armure_table_and_audible();
    test_armure_black_tonic_ignored();
    test_armure_persists_and_untranspose();
    test_alt_overlay_on_armure();
    test_enharmonic_conflict_with_armure();
    test_new_armure_clears_overlay();
    test_temp_delta_table();
    test_temp_notes();
    test_temp_chain_and_bounds();
    test_held_note_is_not_modified();
    test_no_modifier();
    test_natural_root();
    test_black_keys();
    test_toggle_round_trip();
    test_conflict_last_wins();
    test_si_do_swap();
    test_midi_bounds_and_order();
    test_keycode_press_is_inert();
    test_keycode_name();
    test_temp_transpose_values();
    test_position_side();
    test_momentary_delta_basic();
    test_momentary_conflict_last_wins();
    test_momentary_with_persistent();
    test_momentary_bounds();
    test_held_note_keeps_pitch_after_release();
    test_untranspose_keeps_momentary();
    test_temp_transpose_press_is_inert();
    test_temp_transpose_names();

    printf("test_keycode_alteration: %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
