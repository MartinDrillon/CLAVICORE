// Tests unitaires hôte du moteur de combos de positions (ADR-0031), avec une
// horloge factice. Aucune position de keymap n'est normative : les tables
// utilisées ici sont locales au test.
#include <stdio.h>

#include "combo.h"
#include "keymap.h"

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

static const uint8_t k_pos_1_2[] = { 1u, 2u };
static const uint8_t k_pos_1_2_3[] = { 1u, 2u, 3u };
static const uint8_t k_pos_3_4[] = { 3u, 4u };
static const uint8_t k_pos_1_3[] = { 1u, 3u };
static const uint8_t k_pos_2_3[] = { 2u, 3u };

static const keymap_combo_t k_table_basic[] = {
    { k_pos_1_2, 2u, 50u, N_DO4 },
};

static const keymap_combo_t k_table_three[] = {
    { k_pos_1_2_3, 3u, 50u, N_DO4 },
};

static const keymap_combo_t k_table_precedence[] = {
    { k_pos_1_2_3, 3u, 50u, N_RE4 },
    { k_pos_3_4, 2u, 50u, N_DO4 },
};

static const keymap_combo_t k_table_tie[] = {
    { k_pos_1_3, 2u, 50u, N_DO4 },
    { k_pos_2_3, 2u, 50u, N_RE4 },
};

static const keymap_combo_t k_table_momentary[] = {
    { k_pos_1_2, 2u, 50u, ALT_B },
};

static const keymap_combo_t k_table_shared[] = {
    { k_pos_1_3, 2u, 50u, N_DO4 },
    { k_pos_2_3, 2u, 50u, N_RE4 },
};

static const keymap_combo_t k_table_two_active[] = {
    { k_pos_1_2, 2u, 50u, N_DO4 },
    { k_pos_3_4, 2u, 50u, N_RE4 },
};

static combo_state_t g_state;
static combo_action_t g_actions[COMBO_POSITION_SLOTS];

static void begin(const keymap_combo_t *table, uint8_t count) {
    combo_init(&g_state, table, count);
}

static uint8_t press(uint8_t position, uint32_t now_ms, uint8_t max_out) {
    return combo_on_press(&g_state, position, now_ms, g_actions, max_out);
}

static uint8_t release(uint8_t position, uint8_t max_out) {
    return combo_on_release(&g_state, position, g_actions, max_out);
}

static uint8_t expire(uint32_t now_ms, uint8_t max_out) {
    return combo_expire(&g_state, now_ms, g_actions, max_out);
}

static bool note_is(keycode_t keycode, uint8_t midi_note) {
    return keycode.kind == KEYCODE_NOTE && keycode.value == midi_note;
}

static void test_trigger_in_window(void) {
    begin(k_table_basic, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(g_state.pending[1]);
    CHECK(press(2u, 10u, 8u) == 1u);
    CHECK(g_actions[0].kind == COMBO_ACTION_OUTPUT_PRESS);
    CHECK(g_actions[0].position == 0u);
    CHECK(note_is(g_actions[0].keycode, 60u));
    CHECK(g_state.active);
    CHECK(g_state.output_held);
    CHECK(g_state.active_combo == 0u);
    CHECK(g_state.armed[1] && g_state.armed[2]);
    CHECK(g_state.armed_count == 2u);
    CHECK(!g_state.pending[1] && !g_state.pending[2]);
    CHECK(g_state.pending_count == 0u);
}

static void test_same_batch(void) {
    begin(k_table_basic, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 1u);
    CHECK(g_actions[0].kind == COMBO_ACTION_OUTPUT_PRESS);
    CHECK(g_state.active);
}

static void test_window_exceeded_no_trigger(void) {
    begin(k_table_basic, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 50u, 8u) == 0u);
    CHECK(!g_state.active);
    CHECK(g_state.pending[1] && g_state.pending[2]);
    CHECK(g_state.pending_count == 2u);
}

static void test_expire_order(void) {
    // Combo à trois positions : deux appuis pending, aucun complet.
    begin(k_table_three, 1u);

    CHECK(press(2u, 0u, 8u) == 0u);
    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(g_state.pending_count == 2u);

    CHECK(expire(49u, 8u) == 0u);
    CHECK(g_state.pending_count == 2u);

    CHECK(expire(50u, 8u) == 2u);
    CHECK(g_actions[0].kind == COMBO_ACTION_PENDING_PRESS);
    CHECK(g_actions[0].position == 2u);
    CHECK(g_actions[1].kind == COMBO_ACTION_PENDING_PRESS);
    CHECK(g_actions[1].position == 1u);
    CHECK(g_state.pending_count == 0u);
    CHECK(!g_state.pending[1] && !g_state.pending[2]);
}

static void test_release_before_completion_flushes(void) {
    begin(k_table_basic, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(release(1u, 8u) == 1u);
    CHECK(g_actions[0].kind == COMBO_ACTION_PENDING_PRESS);
    CHECK(g_actions[0].position == 1u);
    CHECK(g_state.pending_count == 0u);
    CHECK(!g_state.active);
}

static void test_precedence_more_positions(void) {
    begin(k_table_precedence, 2u);

    // Les deux combos ne sont complets qu'au dernier appui (position 3,
    // partagée) : le combo à trois positions l'emporte sur celui à deux.
    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 0u);
    CHECK(press(4u, 0u, 8u) == 0u);
    CHECK(press(3u, 0u, 8u) == 1u);
    CHECK(g_state.active_combo == 0u);
    CHECK(note_is(g_actions[0].keycode, 62u));
}

static void test_precedence_smallest_index_on_tie(void) {
    begin(k_table_tie, 2u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 0u);
    CHECK(press(3u, 0u, 8u) == 1u);
    // Deux combos complets de même taille : le premier de la table gagne.
    CHECK(g_state.active_combo == 0u);
    CHECK(note_is(g_actions[0].keycode, 60u));
}

static void test_member_releases_are_removed(void) {
    begin(k_table_basic, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 1u);

    // Premier relâchement : sortie relâchée ; les suivants ne produisent rien.
    CHECK(release(2u, 8u) == 1u);
    CHECK(g_actions[0].kind == COMBO_ACTION_OUTPUT_RELEASE);
    CHECK(note_is(g_actions[0].keycode, 60u));
    CHECK(!g_state.output_held);
    CHECK(g_state.active);

    CHECK(release(1u, 8u) == 0u);
    CHECK(!g_state.active);
    CHECK(g_state.armed_count == 0u);
}

static void test_momentary_output(void) {
    begin(k_table_momentary, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 1u);
    CHECK(g_actions[0].kind == COMBO_ACTION_OUTPUT_PRESS);
    CHECK(g_actions[0].keycode.kind == KEYCODE_ALTERATION);
    CHECK(g_actions[0].keycode.value == 0u);

    CHECK(release(1u, 8u) == 1u);
    CHECK(g_actions[0].kind == COMBO_ACTION_OUTPUT_RELEASE);
    CHECK(g_actions[0].keycode.kind == KEYCODE_ALTERATION);
    CHECK(g_actions[0].keycode.value == 0u);
}

static void test_note_literal_on_off(void) {
    begin(k_table_basic, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 1u);
    const keycode_t on = g_actions[0].keycode;
    CHECK(note_is(on, 60u));
    CHECK(g_state.output_note_active);
    CHECK(g_state.output_note == 60u);

    CHECK(release(1u, 8u) == 1u);
    CHECK(g_actions[0].keycode.kind == on.kind);
    CHECK(g_actions[0].keycode.value == on.value);
    CHECK(!g_state.output_note_active);
}

static void test_shared_member(void) {
    begin(k_table_shared, 2u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 0u);
    CHECK(press(3u, 0u, 8u) == 1u);
    CHECK(g_state.active_combo == 0u);
    CHECK(g_state.armed[3]);
    CHECK(g_state.armed_count == 2u);
}

// Membre d'une table locale de test : aucune dépendance au contenu de keymap.c.
static bool local_table_is_member(const keymap_combo_t *table, uint8_t count,
                                  uint8_t position) {
    for (uint8_t combo_index = 0u; combo_index < count; ++combo_index) {
        const keymap_combo_t *combo = &table[combo_index];

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

static void test_non_member_not_delayed(void) {
    // Table locale construite par le test : la position 9 n'est membre
    // d'aucune entrée.
    begin(k_table_basic, 1u);

    // Le moteur ignore une position non membre ; main ne l'appelle d'ailleurs
    // jamais pour elle (chemin immédiat).
    CHECK(press(9u, 0u, 8u) == 0u);
    CHECK(!g_state.pending[9]);
    CHECK(g_state.pending_count == 0u);
    CHECK(!local_table_is_member(k_table_basic, 1u, 9u));
}

static void test_three_positions(void) {
    begin(k_table_three, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 5u, 8u) == 0u);
    CHECK(!g_state.active);
    CHECK(press(3u, 10u, 8u) == 1u);
    CHECK(g_state.active);
    CHECK(g_state.armed_count == 3u);
    CHECK(g_state.armed[1] && g_state.armed[2] && g_state.armed[3]);
}

static void test_armed_press_ignored(void) {
    begin(k_table_basic, 1u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 1u);

    // Appui d'une position armed pendant le combo : ignoré, pas de pending.
    CHECK(press(1u, 20u, 8u) == 0u);
    CHECK(!g_state.pending[1]);

    // Premier relâchement : la sortie est relâchée, puis le membre est relâché.
    CHECK(release(2u, 8u) == 1u);
    CHECK(!g_state.output_held);
    CHECK(release(1u, 8u) == 0u);
    CHECK(!g_state.active);

    // L'ex-membre redevenu libre redevient un appui membre normal.
    CHECK(press(1u, 30u, 8u) == 0u);
    CHECK(g_state.pending[1]);
}

static void test_no_retrigger_while_active(void) {
    begin(k_table_two_active, 2u);

    CHECK(press(1u, 0u, 8u) == 0u);
    CHECK(press(2u, 0u, 8u) == 1u);
    CHECK(g_state.active_combo == 0u);

    // La sortie est relâchée mais le combo reste actif (membre 1 armé).
    CHECK(release(2u, 8u) == 1u);
    CHECK(!g_state.output_held);
    CHECK(g_state.active);

    // Un autre combo ne peut pas se déclencher tant que le premier est actif :
    // ses appuis sont retenus mais aucun OUTPUT_PRESS n'est produit.
    CHECK(press(3u, 10u, 8u) == 0u);
    CHECK(press(4u, 10u, 8u) == 0u);
    CHECK(g_state.active_combo == 0u);
    CHECK(!g_state.armed[3] && !g_state.armed[4]);
    CHECK(g_state.pending[3] && g_state.pending[4]);

    CHECK(release(1u, 8u) == 0u);
    CHECK(!g_state.active);
}

static void test_max_out_bound(void) {
    begin(k_table_three, 1u);

    CHECK(press(2u, 0u, 8u) == 0u);
    CHECK(press(1u, 5u, 8u) == 0u);

    // Borne : une seule action par appel, le reste demeure pending, sans perte.
    CHECK(expire(60u, 1u) == 1u);
    CHECK(g_actions[0].position == 2u);
    CHECK(g_state.pending_count == 1u);

    CHECK(expire(60u, 1u) == 1u);
    CHECK(g_actions[0].position == 1u);
    CHECK(g_state.pending_count == 0u);

    // max_out = 0 : rien n'est écrit ni consommé.
    begin(k_table_three, 1u);
    CHECK(press(2u, 0u, 8u) == 0u);
    CHECK(expire(60u, 0u) == 0u);
    CHECK(g_state.pending_count == 1u);
}

static void test_reset_preserves_table(void) {
    begin(k_table_basic, 1u);
    CHECK(press(1u, 0u, 8u) == 0u);
    combo_reset(&g_state);
    CHECK(g_state.table == k_table_basic);
    CHECK(g_state.table_count == 1u);
    CHECK(g_state.pending_count == 0u);
    CHECK(!g_state.active);
    CHECK(!g_state.output_held);
}

// Contrôle structurel d'une entrée de combo, indépendant du contenu : il doit
// accepter aussi bien une table vide (aucune entrée) qu'une table non vide.
static void check_combo_entry(const keymap_combo_t *combo) {
    CHECK(combo->positions != NULL);
    CHECK(combo->count >= 2u);
    CHECK(combo->count <= COMBO_MAX_POSITIONS);
    CHECK(combo->timeout_ms > 0u);

    // Ne pas parcourir une entrée malformée : bornes vérifiées ci-dessus.
    if (combo->positions == NULL || combo->count < 2u ||
        combo->count > COMBO_MAX_POSITIONS) {
        return;
    }
    for (uint8_t member = 0u; member < combo->count; ++member) {
        CHECK(combo->positions[member] >= 1u);
        CHECK(combo->positions[member] <= KEYMAP_POSITION_COUNT);
        for (uint8_t other = 0u; other < member; ++other) {
            CHECK(combo->positions[other] != combo->positions[member]);
        }
    }
}

static void check_combo_table(const keymap_combo_t *table, uint8_t count) {
    for (uint8_t index = 0u; index < count; ++index) {
        check_combo_entry(&table[index]);
    }
}

static void test_table_structure_accepts_non_empty(void) {
    // Prouve que la validation structurelle de test_delivered_table passe avec
    // des combos valides, sans toucher au contenu livré de keymap.c.
    check_combo_table(k_table_precedence, 2u);
    check_combo_table(k_table_basic, 1u);
    check_combo_table(k_table_three, 1u);
}

static void test_delivered_table(void) {
    const uint8_t count = keymap_combo_count();

    CHECK(COMBO_MAX_POSITIONS == 8u);
    CHECK(KEYMAP_POSITION_COUNT == 84u);
    // La table livrée se termine par la sentinelle : l'index count n'existe pas.
    CHECK(keymap_combo_at(count) == NULL);

    // Contrôle structurel de chaque entrée livrée. Ce test ne présume rien du
    // contenu : il passe avec une table vide comme avec des combos valides.
    for (uint8_t index = 0u; index < count; ++index) {
        const keymap_combo_t *combo = keymap_combo_at(index);

        CHECK(combo != NULL);
        if (combo != NULL) {
            check_combo_entry(combo);
        }
    }
}

int main(void) {
    test_trigger_in_window();
    test_same_batch();
    test_window_exceeded_no_trigger();
    test_expire_order();
    test_release_before_completion_flushes();
    test_precedence_more_positions();
    test_precedence_smallest_index_on_tie();
    test_member_releases_are_removed();
    test_momentary_output();
    test_note_literal_on_off();
    test_shared_member();
    test_non_member_not_delayed();
    test_three_positions();
    test_armed_press_ignored();
    test_no_retrigger_while_active();
    test_max_out_bound();
    test_reset_preserves_table();
    test_table_structure_accepts_non_empty();
    test_delivered_table();

    if (g_failures != 0) {
        printf("%d echec(s) sur %d verifications\n", g_failures, g_checks);
        return 1;
    }
    printf("OK: %d verifications\n", g_checks);
    return 0;
}
