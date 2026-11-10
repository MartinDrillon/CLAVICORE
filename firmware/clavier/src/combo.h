#ifndef COMBO_H
#define COMBO_H

#include <stdbool.h>
#include <stdint.h>

#include "keycode.h"
#include "keymap.h"

// ADR-0031 : moteur pur des combos de positions. Aucune dépendance Pico :
// l'horloge est toujours un paramètre (millisecondes monotones).

// Index valides : 1..KEYMAP_POSITION_COUNT. L'index 0 est réservé aux sorties
// de combo (keycode de binding, sans position physique).
#define COMBO_POSITION_SLOTS (KEYMAP_POSITION_COUNT + 1u)

typedef enum {
    // Un appui retenu doit être émis avec sa position physique.
    COMBO_ACTION_PENDING_PRESS = 0,
    // La sortie du combo est émise (position = 0, keycode = binding).
    COMBO_ACTION_OUTPUT_PRESS,
    // La sortie du combo est relâchée (position = 0, keycode = binding).
    COMBO_ACTION_OUTPUT_RELEASE,
} combo_action_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t position;
    keycode_t keycode;
} combo_action_t;

typedef struct {
    bool pending[COMBO_POSITION_SLOTS];
    uint32_t pressed_at_ms[COMBO_POSITION_SLOTS];
    uint8_t pending_order[COMBO_POSITION_SLOTS];
    uint8_t pending_count;
    bool armed[COMBO_POSITION_SLOTS];
    uint8_t armed_count;
    bool active;
    uint8_t active_combo;
    bool output_held;
    // ADR-0031 IMP-004 : note littérale de la sortie active, mémorisée pour
    // son Note Off (elle ne passe pas par note_state).
    bool output_note_active;
    uint8_t output_note;
    uint32_t seq;
    const keymap_combo_t *table;
    uint8_t table_count;
} combo_state_t;

void combo_init(combo_state_t *state, const keymap_combo_t *table, uint8_t table_count);
void combo_reset(combo_state_t *state);

uint8_t combo_on_press(combo_state_t *state, uint8_t position, uint32_t now_ms,
                       combo_action_t *out, uint8_t max_out);
uint8_t combo_on_release(combo_state_t *state, uint8_t position,
                         combo_action_t *out, uint8_t max_out);
uint8_t combo_expire(combo_state_t *state, uint32_t now_ms,
                     combo_action_t *out, uint8_t max_out);

// Relâche la sortie encore tenue (bascule de mode maintenance). Retourne 1 si
// une action OUTPUT_RELEASE a été écrite, 0 sinon.
uint8_t combo_release_output(combo_state_t *state, combo_action_t *out, uint8_t max_out);

// Vrai si la position est membre armé du combo actif (utilisé par main pour
// savoir si un relâchement physique doit être exécuté).
bool combo_position_is_armed(const combo_state_t *state, uint8_t position);

#endif
