#include "keymap.h"

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
    [0] = KEYCODE_STORAGE_NONE,

    // Manual keys, ordered by canonical position.
    [1] = N_SOLD4, [2] = N_FAD4, [3] = N_MI4, [4] = N_RE4,
    [5] = N_DO4, [6] = N_LAD3, [7] = N_LAD5, [8] = N_DO5,
    [9] = N_RE5, [10] = N_MI5, [11] = N_FAD5, [12] = N_SOLD5,

    [13] = N_DOD3, [14] = N_SI2, [15] = N_LA2, [16] = N_SOL2,
    [17] = N_FA2, [18] = N_RED2,
    [19] = N_RED3, [20] = N_FA3, [21] = N_SOL3,
    [22] = N_LA3, [23] = N_SI3, [24] = N_DOD4,

    [25] = N_SOLD3, [26] = N_FAD3, [27] = N_MI3, [28] = N_RE3,
    [29] = N_DO3, [30] = N_LAD2,
    [31] = N_LAD3, [32] = N_DO4, [33] = N_RE4,
    [34] = N_MI4, [35] = N_FAD4, [36] = N_SOLD4,

    [37] = N_DOD2, [38] = N_SI1, [39] = N_LA1, [40] = N_SOL1,
    [41] = N_FA1, [42] = N_RED1,
    [43] = N_RED2, [44] = N_FA2, [45] = N_SOL2,
    [46] = N_LA2, [47] = N_SI2, [48] = N_DOD3,

    [49] = N_SOLD2, [50] = N_FAD2, [51] = N_MI2, [52] = N_RE2,
    [53] = N_DO2, [54] = N_LAD1,
    [55] = N_LAD2, [56] = N_DO3, [57] = N_RE3,
    [58] = N_MI3, [59] = N_FAD3, [60] = N_SOLD3,

    [61] = KEYCODE_STORAGE_NONE,
    [62] = N_DOD1, [63] = N_SI0, [64] = N_LA0,
    [65] = N_SOL0, [66] = N_FA0,
    [67] = N_FA2, [68] = N_SOL2, [69] = N_LA2,
    [70] = N_SI2, [71] = N_DOD3, [72] = KEYCODE_STORAGE_NONE,

    // Control keys, ordered by canonical position.
    [73] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_DO),
    [74] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_RE),
    [75] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_MI),
    [76] = KEYCODE_STORAGE_BEHAVIOR(KEYCODE_TRANSPOSE_GLOBAL_UP),
    [77] = KEYCODE_STORAGE_BEHAVIOR(KEYCODE_TRANSPOSE_LEFT_DOWN),
    [78] = KEYCODE_STORAGE_BEHAVIOR(KEYCODE_TRANSPOSE_LEFT_UP),
    [79] = KEYCODE_STORAGE_BEHAVIOR(KEYCODE_TRANSPOSE_RIGHT_UP),
    [80] = KEYCODE_STORAGE_BEHAVIOR(KEYCODE_TRANSPOSE_RIGHT_DOWN),
    [81] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_SI),
    [82] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_LA),
    [83] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_SOL),
    [84] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_FA),
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