#ifndef KEYCODE_H
#define KEYCODE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KEYCODE_NONE = 0,
    KEYCODE_NOTE,
    KEYCODE_TRANSPOSE_GLOBAL_UP,
    KEYCODE_TRANSPOSE_GLOBAL_DOWN,
    KEYCODE_TRANSPOSE_LEFT_UP,
    KEYCODE_TRANSPOSE_LEFT_DOWN,
    KEYCODE_TRANSPOSE_RIGHT_UP,
    KEYCODE_TRANSPOSE_RIGHT_DOWN,
    KEYCODE_ALTERATION,
} keycode_kind_t;

typedef enum {
    KEYCODE_ALTERATION_DO = 0,
    KEYCODE_ALTERATION_RE,
    KEYCODE_ALTERATION_MI,
    KEYCODE_ALTERATION_FA,
    KEYCODE_ALTERATION_SOL,
    KEYCODE_ALTERATION_LA,
    KEYCODE_ALTERATION_SI,
    KEYCODE_ALTERATION_COUNT,
} keycode_alteration_t;

typedef struct {
    keycode_kind_t kind;
    uint8_t value;
} keycode_t;

typedef struct {
    int16_t global_semitones;
    int16_t left_octaves;
    int16_t right_octaves;
    uint8_t alteration_state[KEYCODE_ALTERATION_COUNT];
    uint32_t alteration_change[KEYCODE_ALTERATION_COUNT];
    uint32_t change_counter;
} keycode_state_t;

#define KEYCODE_NOTE_VALUE(midi_note) ((keycode_t){KEYCODE_NOTE, (midi_note)})
#define KEYCODE_BEHAVIOR(kind_value) ((keycode_t){(kind_value), 0u})
#define KEYCODE_ALTERATION_VALUE(alteration) ((keycode_t){KEYCODE_ALTERATION, (alteration)})
#define KEYCODE_NONE_VALUE ((keycode_t){KEYCODE_NONE, 0u})

#define KEYCODE_STORAGE_PACK(kind_value, value) \
    (((kind_value) << 8u) | (value))
#define KEYCODE_STORAGE_NOTE(midi_note) KEYCODE_STORAGE_PACK(KEYCODE_NOTE, (midi_note))
#define KEYCODE_STORAGE_BEHAVIOR(kind_value) KEYCODE_STORAGE_PACK((kind_value), 0u)
#define KEYCODE_STORAGE_ALTERATION(alteration) KEYCODE_STORAGE_PACK(KEYCODE_ALTERATION, (alteration))
#define KEYCODE_STORAGE_NONE KEYCODE_STORAGE_PACK(KEYCODE_NONE, 0u)

// Layout note names map directly to their MIDI note numbers.
#define N_FA0 KEYCODE_STORAGE_NOTE(17u)
#define N_SOL0 KEYCODE_STORAGE_NOTE(19u)
#define N_LA0 KEYCODE_STORAGE_NOTE(21u)
#define N_SI0 KEYCODE_STORAGE_NOTE(23u)
#define N_DOD1 KEYCODE_STORAGE_NOTE(25u)
#define N_RED1 KEYCODE_STORAGE_NOTE(27u)
#define N_FA1 KEYCODE_STORAGE_NOTE(29u)
#define N_SOL1 KEYCODE_STORAGE_NOTE(31u)
#define N_LA1 KEYCODE_STORAGE_NOTE(33u)
#define N_LAD1 KEYCODE_STORAGE_NOTE(34u)
#define N_SI1 KEYCODE_STORAGE_NOTE(35u)
#define N_DO2 KEYCODE_STORAGE_NOTE(36u)
#define N_DOD2 KEYCODE_STORAGE_NOTE(37u)
#define N_RE2 KEYCODE_STORAGE_NOTE(38u)
#define N_RED2 KEYCODE_STORAGE_NOTE(39u)
#define N_MI2 KEYCODE_STORAGE_NOTE(40u)
#define N_FA2 KEYCODE_STORAGE_NOTE(41u)
#define N_FAD2 KEYCODE_STORAGE_NOTE(42u)
#define N_SOL2 KEYCODE_STORAGE_NOTE(43u)
#define N_SOLD2 KEYCODE_STORAGE_NOTE(44u)
#define N_LA2 KEYCODE_STORAGE_NOTE(45u)
#define N_LAD2 KEYCODE_STORAGE_NOTE(46u)
#define N_SI2 KEYCODE_STORAGE_NOTE(47u)
#define N_DO3 KEYCODE_STORAGE_NOTE(48u)
#define N_DOD3 KEYCODE_STORAGE_NOTE(49u)
#define N_RE3 KEYCODE_STORAGE_NOTE(50u)
#define N_RED3 KEYCODE_STORAGE_NOTE(51u)
#define N_MI3 KEYCODE_STORAGE_NOTE(52u)
#define N_FA3 KEYCODE_STORAGE_NOTE(53u)
#define N_FAD3 KEYCODE_STORAGE_NOTE(54u)
#define N_SOL3 KEYCODE_STORAGE_NOTE(55u)
#define N_SOLD3 KEYCODE_STORAGE_NOTE(56u)
#define N_LA3 KEYCODE_STORAGE_NOTE(57u)
#define N_LAD3 KEYCODE_STORAGE_NOTE(58u)
#define N_SI3 KEYCODE_STORAGE_NOTE(59u)
#define N_DO4 KEYCODE_STORAGE_NOTE(60u)
#define N_DOD4 KEYCODE_STORAGE_NOTE(61u)
#define N_RE4 KEYCODE_STORAGE_NOTE(62u)
#define N_MI4 KEYCODE_STORAGE_NOTE(64u)
#define N_FAD4 KEYCODE_STORAGE_NOTE(66u)
#define N_SOLD4 KEYCODE_STORAGE_NOTE(68u)
#define N_LAD5 KEYCODE_STORAGE_NOTE(70u)
#define N_DO5 KEYCODE_STORAGE_NOTE(72u)
#define N_RE5 KEYCODE_STORAGE_NOTE(74u)
#define N_MI5 KEYCODE_STORAGE_NOTE(76u)
#define N_FAD5 KEYCODE_STORAGE_NOTE(78u)
#define N_SOLD5 KEYCODE_STORAGE_NOTE(80u)

void keycode_state_init(keycode_state_t *state);
bool keycode_is_behavior(keycode_t keycode);
void keycode_press(keycode_state_t *state, keycode_t keycode);
int16_t keycode_transposition_for_position(const keycode_state_t *state, uint8_t position);
int8_t keycode_alteration_for_note(const keycode_state_t *state, uint8_t base_midi_note);
uint8_t keycode_alteration_state(const keycode_state_t *state, keycode_t keycode);
const char *keycode_name(keycode_t keycode);

#endif