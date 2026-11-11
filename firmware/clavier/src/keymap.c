#include "keymap.h"

#include <stddef.h>

/*
 * Canonical physical positions (XX: unused):
 * | 1| 2| 3| 4| 5| 6|                     | 7| 8| 9|10|11|12|
 * |13|14|15|16|17|18|78|               |79|19|20|21|22|23|24|
 * |25|26|27|28|29|30|      |76|81|        |31|32|33|34|35|36|
 * |37|38|39|40|41|42|77    |75|82|     |80|43|44|45|46|47|48|
 * |49|50|51|52|53|54|      |74|83|        |55|56|57|58|59|60|
 *    |XX|62|63|64|65|66|   |73|84|     |67|68|69|70|71|XX|
 */
static const uint16_t base_layer[KEYMAP_POSITION_COUNT + 1u] = {
    [0] = NONE,

    // Manual keys, ordered by canonical position. NE PAS MODIFIER LA MISE EN FORME ! (indentation)

//  v3
//  [13] = N_SOLD3,  [14] = N_FAD3,   [15] = N_MI4,   [16] = N_RE4,   [17] = N_DO4,   [18] = N_LAD3,               [19] = N_LAD5,      [20] = N_DO6,   [21] = N_RE6,   [22] = N_MI6,   [23] = N_FAD5,   [24] = N_SOLD5,
//  [25] = N_DOD3,   [26] = N_SI3,    [27] = N_LA3,   [28] = N_SOL3,  [29] = N_FA3,   [30] = N_RED3,               [31] = N_RED5,     [32] = N_FA5,   [33] = N_SOL5,  [34] = N_LA5,   [35] = N_SI5,    [36] = N_DOD5,
//  [37] = N_SOLD2,  [38] = N_FAD2,   [39] = N_MI3,   [40] = N_RE3,   [41] = N_DO3,   [42] = N_LAD2,               [43] = N_LAD4,     [44] = N_DO5,   [45] = N_RE5,   [46] = N_MI5,   [47] = N_FAD4,   [48] = N_SOLD4,
//  [49] = N_DOD2,   [50] = N_SI2,    [51] = N_LA2,   [52] = N_SOL2,  [53] = N_FA2,   [54] = N_RED2,               [55] = N_RED4,     [56] = N_FA4,   [57] = N_SOL4,  [58] = N_LA4,   [59] = N_SI4,    [60] = N_DOD4,
//                   [62] = N_SOLD2,  [63] = N_FAD2,  [64] = N_MI2,   [65] = N_RE2,   [66] = N_DO2,                [67] = N_DO4,      [68] = N_RE4,   [69] = N_MI4,   [70] = N_FAD4,  [71] = N_SOLD4,    


                   
//v4
  [13] = N_RED4,  [14] = N_RE4,   [15] = N_DO4,   [16] = N_SI3,  [17] = N_LA3,   [18] = N_LAD3,                              [19] = N_LAD5,     [20] = N_LA5,   [21] = N_SI6,  [22] = N_DO6,   [23] = N_RE6,   [24] = N_RED6,
  [25] = N_SOLD3, [26] = N_SOL3,  [27] = N_FA3,   [28] = N_MI3,  [29] = N_RE3,   [30] = N_RED3,                              [31] = N_RED5,     [32] = N_RE5,   [33] = N_MI5,  [34] = N_FA5,   [35] = N_SOL5,    [36] = N_SOLD5,
  [37] = N_DOD3,  [38] = N_DO3,   [39] = N_SI2,   [40] = N_LA2,  [41] = N_SOL2,  [42] = N_SOLD2,                             [43] = N_SOLD4,    [44] = N_SOL4,  [45] = N_LA4,  [46] = N_SI4,   [47] = N_DO5,   [48] = N_DOD5,
  [49] = N_FAD2,  [50] = N_FA2,   [51] = N_MI2,   [52] = N_RE2,  [53] = N_DO2,   [54] = N_DOD2,                              [55] = N_DOD4,     [56] = N_DO4,   [57] = N_RE4,  [58] = N_MI4,   [59] = N_FA4,    [60] = N_FAD4,
                                  [62] = N_LAD1,  [63] = N_SI1,  [64] = N_LA1,   [65] = N_SOL1, [66] = N_FA1,  [67] = N_FA3, [68] = N_SOL3,     [69] = N_LA3,   [70] = N_SI3,  [71] = N_LAD4,    


    [78] = T_OCT_L_UP,
    [77] = T_OCT_L_DOWN,
   
    [79] = T_OCT_R_UP,
    [80] = T_OCT_R_DOWN,

    /*
 * Canonical physical positions (XX: unused):
 * | 1| 2| 3| 4| 5| 6|                     | 7| 8| 9|10|11|12|
 * |13|14|15|16|17|18|78|               |79|19|20|21|22|23|24|
 * |25|26|27|28|29|30|      |76|81|        |31|32|33|34|35|36|
 * |37|38|39|40|41|42|77    |75|82|     |80|43|44|45|46|47|48|
 * |49|50|51|52|53|54|      |74|83|        |55|56|57|58|59|60|
 *    |XX|62|63|64|65|66|   |73|84|     |67|68|69|70|71|XX|
 */


    // Control keys, ordered by canonical position.
    // Exemple (NON assigné : le choix de la position physique revient à l'utilisateur) :
    //   [XX] = GET_UID,   // XX = position libre de la matrice (ADR-0003)

    [76] = T_GLOB_UP,   [81] = T_GLOB_DOWN,
    [75] = ALT_B,       [82] = ALT_D,
    [74] = CLE_B,       [83] = CLE_D,
    [73] = UNTRANSPOSE, [84] = MAINT,

    
    [61] = NONE, 
    [72] = NONE,
};

keycode_t keymap_keycode_for_position(uint8_t position) {
    if (position == 0u || position > KEYMAP_POSITION_COUNT) {
        return KEYCODE_NONE_VALUE;
    }

    uint16_t encoded = base_layer[position];
    return (keycode_t){
        .kind = (keycode_kind_t)(encoded >> 8u),
        .value = (uint8_t)encoded,
    };
}

/*
 * Combos de positions (ADR-0031). La table est terminée par UNE entrée
 * sentinelle { NULL, 0u, 0u, NONE } : ne rien ajouter après elle, et ne jamais
 * déclarer un combo avec count = 0 (c'est le signal de fin de table).
 * Les tableaux de positions doivent rester vivants (static const).
 */
static const uint8_t combo_1_positions[] = { 76u, 81u };
static const uint8_t combo_2_positions[] = { 73u, 84u };

static const keymap_combo_t combo_table[] = {
    { .positions = combo_1_positions, .count = 2u,
      .timeout_ms = 50u,  .binding = UNTRANSPOSE },
    { .positions = combo_2_positions, .count = 2u,
      .timeout_ms = 200u, .binding = MAINT },
    { NULL, 0u, 0u, NONE },        // ← sentinelle, toujours en dernier
};

#define COMBO_TABLE_ENTRY_COUNT (sizeof(combo_table) / sizeof(combo_table[0]))

_Static_assert(COMBO_TABLE_ENTRY_COUNT >= 1u,
               "combo_table doit contenir au moins la sentinelle");
_Static_assert(COMBO_TABLE_ENTRY_COUNT <= 255u,
               "combo_table doit tenir dans un uint8_t (sentinelle incluse)");
_Static_assert(COMBO_MAX_POSITIONS >= 2u,
               "un combo doit avoir au moins deux positions");
_Static_assert(COMBO_MAX_POSITIONS <= 255u,
               "COMBO_MAX_POSITIONS doit tenir dans un uint8_t");
_Static_assert(KEYMAP_POSITION_COUNT <= 255u,
               "une position doit tenir dans un uint8_t");

uint8_t keymap_combo_count(void) {
    uint8_t count = 0u;

    while (count < (uint8_t)COMBO_TABLE_ENTRY_COUNT &&
           combo_table[count].count != 0u) {
        ++count;
    }
    return count;
}

const keymap_combo_t *keymap_combo_at(uint8_t index) {
    if (index >= keymap_combo_count()) {
        return NULL;
    }
    return &combo_table[index];
}

bool keymap_combo_is_member(uint8_t position) {
    const uint8_t combo_count = keymap_combo_count();

    if (position == 0u || position > KEYMAP_POSITION_COUNT) {
        return false;
    }

    for (uint8_t combo_index = 0u; combo_index < combo_count; ++combo_index) {
        const keymap_combo_t *combo = &combo_table[combo_index];

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