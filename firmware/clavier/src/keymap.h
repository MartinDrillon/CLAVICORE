#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "keycode.h"

#define KEYMAP_POSITION_COUNT 84u

// ADR-0031 : nombre maximal de positions dans un combo.
#define COMBO_MAX_POSITIONS 8u

// ADR-0031 : une entrée de la table de combos. `positions` pointe vers
// `count` positions physiques (1..KEYMAP_POSITION_COUNT) ; `binding` est
// encodé comme une case de base_layer : (kind << 8) | value.
typedef struct {
    const uint8_t *positions;
    uint8_t count;
    uint16_t timeout_ms;
    uint16_t binding;
} keymap_combo_t;

keycode_t keymap_keycode_for_position(uint8_t position);

// ADR-0031 : la table de combos est terminée par une entrée sentinelle
// { NULL, 0, 0, NONE }. Le nombre retourné exclut la sentinelle.
uint8_t keymap_combo_count(void);
const keymap_combo_t *keymap_combo_at(uint8_t index);
bool keymap_combo_is_member(uint8_t position);

#endif
