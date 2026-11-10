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
    // ADR-0003 : ajouté EN FIN pour ne pas décaler les valeurs empaquetées existantes.
    KEYCODE_GET_UID,
    KEYCODE_MAINTENANCE_TOGGLE,
    // Annule transpositions et altérations : retour au keymap d'origine.
    KEYCODE_UNTRANSPOSE,
    // ADR-0029 : armure (CLE) et altération temporaire (TEMP), ajoutés EN FIN
    // pour ne décaler aucune valeur empaquetée existante.
    KEYCODE_CLE = 12,
    KEYCODE_TEMP = 13,
    // ADR-0030 : transpositions temporaires par côté, ajoutées EN FIN pour ne
    // décaler aucune valeur empaquetée existante.
    KEYCODE_TEMP_TRANSPOSE_ST = 14,
    KEYCODE_TEMP_TRANSPOSE_OCT = 15,
    // ADR-0032 : bascules du mode REC, ajoutées EN FIN (aucune valeur existante
    // ne bouge ; invariant 6 : aucun keymap n'est modifié par cette ADR).
    KEYCODE_REC_SOLE = 16,
    KEYCODE_REC_MUTE = 17,
} keycode_kind_t;

// ADR-0026 : deux directions d'altération, portées par des modificateurs
// maintenus (bémol / dièse). Les cinq anciens codes par racine disparaissent.
typedef enum {
    KEYCODE_ALTERATION_B = 0,
    KEYCODE_ALTERATION_D = 1,
} keycode_alteration_t;

// Nombre de racines naturelles : l'état d'altération est indexé par racine.
#define KEYCODE_ALTERATION_ROOT_COUNT 7u

// ADR-0029 : mode d'armure (CLE_B = mineur, CLE_D = majeur) et direction
// d'altération temporaire (TEMP_B = bémol, TEMP_D = dièse).
typedef enum {
    KEYCODE_CLE_MINOR = 0,
    KEYCODE_CLE_MAJOR = 1,
} keycode_cle_t;

typedef enum {
    KEYCODE_TEMP_FLAT = 0,
    KEYCODE_TEMP_SHARP = 1,
} keycode_temp_t;

// ADR-0030 : direction d'une transposition temporaire par côté. L'axe (demi-ton
// ou octave) est porté par le kind ; la valeur commune est 0..3.
typedef enum {
    KEYCODE_TEMP_TRANSPOSE_L_UP = 0,
    KEYCODE_TEMP_TRANSPOSE_L_DOWN = 1,
    KEYCODE_TEMP_TRANSPOSE_R_UP = 2,
    KEYCODE_TEMP_TRANSPOSE_R_DOWN = 3,
} keycode_temp_transpose_t;

// ADR-0030 : familles de modificateurs maintenus et nombre de directions.
// ALT/CLE/TEMP n'utilisent que les directions 0 et 1 ; TEMP_ST/TEMP_OCT les
// quatre (L_UP, L_DOWN, R_UP, R_DOWN).
typedef enum {
    KEYCODE_MOD_FAMILY_ALT = 0,
    KEYCODE_MOD_FAMILY_CLE = 1,
    KEYCODE_MOD_FAMILY_TEMP = 2,
    KEYCODE_MOD_FAMILY_TEMP_ST = 3,
    KEYCODE_MOD_FAMILY_TEMP_OCT = 4,
    KEYCODE_MOD_FAMILY_COUNT = 5,
} keycode_modifier_family_t;

#define KEYCODE_MODIFIER_DIRECTIONS 4u

typedef struct {
    keycode_kind_t kind;
    uint8_t value;
} keycode_t;

typedef struct {
    int16_t global_semitones;
    int16_t left_octaves;
    int16_t right_octaves;
    uint8_t alteration_state[KEYCODE_ALTERATION_ROOT_COUNT];
    uint32_t alteration_change[KEYCODE_ALTERATION_ROOT_COUNT];
    // ADR-0029 : armure courante par classe (0 naturel, 1 dièse, 2 bémol) et
    // son horodatage (un seul par armure posée).
    uint8_t armure_state[KEYCODE_ALTERATION_ROOT_COUNT];
    uint32_t armure_change;
    uint32_t change_counter;
} keycode_state_t;

// ADR-0026 / ADR-0029 / ADR-0030 : état des modificateurs maintenus. Il vit
// hors de keycode_state_t car UNTRANSPOSE ne doit pas annuler un keycode
// momentané encore enfoncé. seq horodate le dernier appui/relâchement : la
// direction active est la dernière pressée encore tenue.
typedef struct {
    bool held[KEYCODE_MOD_FAMILY_COUNT][KEYCODE_MODIFIER_DIRECTIONS];
    uint32_t seq[KEYCODE_MOD_FAMILY_COUNT][KEYCODE_MODIFIER_DIRECTIONS];
    uint32_t counter;
} keycode_modifiers_t;

// ADR-0029 : noms publics des keycodes non-note, écrits directement dans le
// keymap (valeur = (kind << 8) | value). Les constructeurs KEYCODE_STORAGE_*
// du keymap disparaissent ; KEYCODE_STORAGE_PACK/_NOTE restent internes.
#define NONE 0x0000u
#define ALT_B 0x0800u
#define ALT_D 0x0801u
#define UNTRANSPOSE 0x0B00u
#define MAINT 0x0A00u
#define GET_UID 0x0900u
#define T_GLOB_UP 0x0200u
#define T_GLOB_DOWN 0x0300u
#define T_OCT_L_UP 0x0400u
#define T_OCT_L_DOWN 0x0500u
#define T_OCT_R_UP 0x0600u
#define T_OCT_R_DOWN 0x0700u
#define CLE_B 0x0C00u
#define CLE_D 0x0C01u
#define TEMP_B 0x0D00u
#define TEMP_D 0x0D01u
// ADR-0030 : transpositions temporaires par côté (demi-ton / octave).
#define T_TEMP_ST_L_UP 0x0E00u
#define T_TEMP_ST_L_DOWN 0x0E01u
#define T_TEMP_ST_R_UP 0x0E02u
#define T_TEMP_ST_R_DOWN 0x0E03u
#define T_TEMP_OCT_L_UP 0x0F00u
#define T_TEMP_OCT_L_DOWN 0x0F01u
#define T_TEMP_OCT_R_UP 0x0F02u
#define T_TEMP_OCT_R_DOWN 0x0F03u
// ADR-0032 : bascules du mode REC. Positions libres : le keymap n'est pas
// modifié (invariant 6) ; l'humain y place ces symboles quand il le souhaite.
#define REC_SOLE 0x1000u
#define REC_MUTE 0x1100u

#define KEYCODE_NOTE_VALUE(midi_note) ((keycode_t){KEYCODE_NOTE, (midi_note)})
#define KEYCODE_BEHAVIOR(kind_value) ((keycode_t){(kind_value), 0u})
#define KEYCODE_ALTERATION_VALUE(alteration) ((keycode_t){KEYCODE_ALTERATION, (alteration)})
#define KEYCODE_NONE_VALUE ((keycode_t){KEYCODE_NONE, 0u})

#define KEYCODE_STORAGE_PACK(kind_value, value) \
    (((kind_value) << 8u) | (value))
#define KEYCODE_STORAGE_NOTE(midi_note) KEYCODE_STORAGE_PACK(KEYCODE_NOTE, (midi_note))

// Layout note names map directly to their MIDI note numbers.
// A flat is an alias for its enharmonic sharp (same MIDI value).
// Octave 0
#define N_DO0 KEYCODE_STORAGE_NOTE(12u)
#define N_DOD0 KEYCODE_STORAGE_NOTE(13u)
#define N_REB0 KEYCODE_STORAGE_NOTE(13u)
#define N_RE0 KEYCODE_STORAGE_NOTE(14u)
#define N_RED0 KEYCODE_STORAGE_NOTE(15u)
#define N_MIB0 KEYCODE_STORAGE_NOTE(15u)
#define N_MI0 KEYCODE_STORAGE_NOTE(16u)
#define N_FA0 KEYCODE_STORAGE_NOTE(17u)
#define N_FAD0 KEYCODE_STORAGE_NOTE(18u)
#define N_SOLB0 KEYCODE_STORAGE_NOTE(18u)
#define N_SOL0 KEYCODE_STORAGE_NOTE(19u)
#define N_SOLD0 KEYCODE_STORAGE_NOTE(20u)
#define N_LAB0 KEYCODE_STORAGE_NOTE(20u)
#define N_LA0 KEYCODE_STORAGE_NOTE(21u)
#define N_LAD0 KEYCODE_STORAGE_NOTE(22u)
#define N_SIB0 KEYCODE_STORAGE_NOTE(22u)
#define N_SI0 KEYCODE_STORAGE_NOTE(23u)
// Octave 1
#define N_DO1 KEYCODE_STORAGE_NOTE(24u)
#define N_DOD1 KEYCODE_STORAGE_NOTE(25u)
#define N_REB1 KEYCODE_STORAGE_NOTE(25u)
#define N_RE1 KEYCODE_STORAGE_NOTE(26u)
#define N_RED1 KEYCODE_STORAGE_NOTE(27u)
#define N_MIB1 KEYCODE_STORAGE_NOTE(27u)
#define N_MI1 KEYCODE_STORAGE_NOTE(28u)
#define N_FA1 KEYCODE_STORAGE_NOTE(29u)
#define N_FAD1 KEYCODE_STORAGE_NOTE(30u)
#define N_SOLB1 KEYCODE_STORAGE_NOTE(30u)
#define N_SOL1 KEYCODE_STORAGE_NOTE(31u)
#define N_SOLD1 KEYCODE_STORAGE_NOTE(32u)
#define N_LAB1 KEYCODE_STORAGE_NOTE(32u)
#define N_LA1 KEYCODE_STORAGE_NOTE(33u)
#define N_LAD1 KEYCODE_STORAGE_NOTE(34u)
#define N_SIB1 KEYCODE_STORAGE_NOTE(34u)
#define N_SI1 KEYCODE_STORAGE_NOTE(35u)
// Octave 2
#define N_DO2 KEYCODE_STORAGE_NOTE(36u)
#define N_DOD2 KEYCODE_STORAGE_NOTE(37u)
#define N_REB2 KEYCODE_STORAGE_NOTE(37u)
#define N_RE2 KEYCODE_STORAGE_NOTE(38u)
#define N_RED2 KEYCODE_STORAGE_NOTE(39u)
#define N_MIB2 KEYCODE_STORAGE_NOTE(39u)
#define N_MI2 KEYCODE_STORAGE_NOTE(40u)
#define N_FA2 KEYCODE_STORAGE_NOTE(41u)
#define N_FAD2 KEYCODE_STORAGE_NOTE(42u)
#define N_SOLB2 KEYCODE_STORAGE_NOTE(42u)
#define N_SOL2 KEYCODE_STORAGE_NOTE(43u)
#define N_SOLD2 KEYCODE_STORAGE_NOTE(44u)
#define N_LAB2 KEYCODE_STORAGE_NOTE(44u)
#define N_LA2 KEYCODE_STORAGE_NOTE(45u)
#define N_LAD2 KEYCODE_STORAGE_NOTE(46u)
#define N_SIB2 KEYCODE_STORAGE_NOTE(46u)
#define N_SI2 KEYCODE_STORAGE_NOTE(47u)
// Octave 3
#define N_DO3 KEYCODE_STORAGE_NOTE(48u)
#define N_DOD3 KEYCODE_STORAGE_NOTE(49u)
#define N_REB3 KEYCODE_STORAGE_NOTE(49u)
#define N_RE3 KEYCODE_STORAGE_NOTE(50u)
#define N_RED3 KEYCODE_STORAGE_NOTE(51u)
#define N_MIB3 KEYCODE_STORAGE_NOTE(51u)
#define N_MI3 KEYCODE_STORAGE_NOTE(52u)
#define N_FA3 KEYCODE_STORAGE_NOTE(53u)
#define N_FAD3 KEYCODE_STORAGE_NOTE(54u)
#define N_SOLB3 KEYCODE_STORAGE_NOTE(54u)
#define N_SOL3 KEYCODE_STORAGE_NOTE(55u)
#define N_SOLD3 KEYCODE_STORAGE_NOTE(56u)
#define N_LAB3 KEYCODE_STORAGE_NOTE(56u)
#define N_LA3 KEYCODE_STORAGE_NOTE(57u)
#define N_LAD3 KEYCODE_STORAGE_NOTE(58u)
#define N_SIB3 KEYCODE_STORAGE_NOTE(58u)
#define N_SI3 KEYCODE_STORAGE_NOTE(59u)
// Octave 4 (DO4 = MIDI 60)
#define N_DO4 KEYCODE_STORAGE_NOTE(60u)
#define N_DOD4 KEYCODE_STORAGE_NOTE(61u)
#define N_REB4 KEYCODE_STORAGE_NOTE(61u)
#define N_RE4 KEYCODE_STORAGE_NOTE(62u)
#define N_RED4 KEYCODE_STORAGE_NOTE(63u)
#define N_MIB4 KEYCODE_STORAGE_NOTE(63u)
#define N_MI4 KEYCODE_STORAGE_NOTE(64u)
#define N_FA4 KEYCODE_STORAGE_NOTE(65u)
#define N_FAD4 KEYCODE_STORAGE_NOTE(66u)
#define N_SOLB4 KEYCODE_STORAGE_NOTE(66u)
#define N_SOL4 KEYCODE_STORAGE_NOTE(67u)
#define N_SOLD4 KEYCODE_STORAGE_NOTE(68u)
#define N_LAB4 KEYCODE_STORAGE_NOTE(68u)
#define N_LA4 KEYCODE_STORAGE_NOTE(69u)
#define N_LAD4 KEYCODE_STORAGE_NOTE(70u)
#define N_SIB4 KEYCODE_STORAGE_NOTE(70u)
#define N_SI4 KEYCODE_STORAGE_NOTE(71u)
// Octave 5
#define N_DO5 KEYCODE_STORAGE_NOTE(72u)
#define N_DOD5 KEYCODE_STORAGE_NOTE(73u)
#define N_REB5 KEYCODE_STORAGE_NOTE(73u)
#define N_RE5 KEYCODE_STORAGE_NOTE(74u)
#define N_RED5 KEYCODE_STORAGE_NOTE(75u)
#define N_MIB5 KEYCODE_STORAGE_NOTE(75u)
#define N_MI5 KEYCODE_STORAGE_NOTE(76u)
#define N_FA5 KEYCODE_STORAGE_NOTE(77u)
#define N_FAD5 KEYCODE_STORAGE_NOTE(78u)
#define N_SOLB5 KEYCODE_STORAGE_NOTE(78u)
#define N_SOL5 KEYCODE_STORAGE_NOTE(79u)
#define N_SOLD5 KEYCODE_STORAGE_NOTE(80u)
#define N_LAB5 KEYCODE_STORAGE_NOTE(80u)
#define N_LA5 KEYCODE_STORAGE_NOTE(81u)
#define N_LAD5 KEYCODE_STORAGE_NOTE(82u)
#define N_SIB5 KEYCODE_STORAGE_NOTE(82u)
#define N_SI5 KEYCODE_STORAGE_NOTE(83u)
// Octave 6
#define N_DO6 KEYCODE_STORAGE_NOTE(84u)
#define N_DOD6 KEYCODE_STORAGE_NOTE(85u)
#define N_REB6 KEYCODE_STORAGE_NOTE(85u)
#define N_RE6 KEYCODE_STORAGE_NOTE(86u)
#define N_RED6 KEYCODE_STORAGE_NOTE(87u)
#define N_MIB6 KEYCODE_STORAGE_NOTE(87u)
#define N_MI6 KEYCODE_STORAGE_NOTE(88u)
#define N_FA6 KEYCODE_STORAGE_NOTE(89u)
#define N_FAD6 KEYCODE_STORAGE_NOTE(90u)
#define N_SOLB6 KEYCODE_STORAGE_NOTE(90u)
#define N_SOL6 KEYCODE_STORAGE_NOTE(91u)
#define N_SOLD6 KEYCODE_STORAGE_NOTE(92u)
#define N_LAB6 KEYCODE_STORAGE_NOTE(92u)
#define N_LA6 KEYCODE_STORAGE_NOTE(93u)
#define N_LAD6 KEYCODE_STORAGE_NOTE(94u)
#define N_SIB6 KEYCODE_STORAGE_NOTE(94u)
#define N_SI6 KEYCODE_STORAGE_NOTE(95u)
// Octave 7
#define N_DO7 KEYCODE_STORAGE_NOTE(96u)
#define N_DOD7 KEYCODE_STORAGE_NOTE(97u)
#define N_REB7 KEYCODE_STORAGE_NOTE(97u)
#define N_RE7 KEYCODE_STORAGE_NOTE(98u)
#define N_RED7 KEYCODE_STORAGE_NOTE(99u)
#define N_MIB7 KEYCODE_STORAGE_NOTE(99u)
#define N_MI7 KEYCODE_STORAGE_NOTE(100u)
#define N_FA7 KEYCODE_STORAGE_NOTE(101u)
#define N_FAD7 KEYCODE_STORAGE_NOTE(102u)
#define N_SOLB7 KEYCODE_STORAGE_NOTE(102u)
#define N_SOL7 KEYCODE_STORAGE_NOTE(103u)
#define N_SOLD7 KEYCODE_STORAGE_NOTE(104u)
#define N_LAB7 KEYCODE_STORAGE_NOTE(104u)
#define N_LA7 KEYCODE_STORAGE_NOTE(105u)
#define N_LAD7 KEYCODE_STORAGE_NOTE(106u)
#define N_SIB7 KEYCODE_STORAGE_NOTE(106u)
#define N_SI7 KEYCODE_STORAGE_NOTE(107u)
// Octave 8
#define N_DO8 KEYCODE_STORAGE_NOTE(108u)
#define N_DOD8 KEYCODE_STORAGE_NOTE(109u)
#define N_REB8 KEYCODE_STORAGE_NOTE(109u)
#define N_RE8 KEYCODE_STORAGE_NOTE(110u)
#define N_RED8 KEYCODE_STORAGE_NOTE(111u)
#define N_MIB8 KEYCODE_STORAGE_NOTE(111u)
#define N_MI8 KEYCODE_STORAGE_NOTE(112u)
#define N_FA8 KEYCODE_STORAGE_NOTE(113u)
#define N_FAD8 KEYCODE_STORAGE_NOTE(114u)
#define N_SOLB8 KEYCODE_STORAGE_NOTE(114u)
#define N_SOL8 KEYCODE_STORAGE_NOTE(115u)
#define N_SOLD8 KEYCODE_STORAGE_NOTE(116u)
#define N_LAB8 KEYCODE_STORAGE_NOTE(116u)
#define N_LA8 KEYCODE_STORAGE_NOTE(117u)
#define N_LAD8 KEYCODE_STORAGE_NOTE(118u)
#define N_SIB8 KEYCODE_STORAGE_NOTE(118u)
#define N_SI8 KEYCODE_STORAGE_NOTE(119u)

void keycode_state_init(keycode_state_t *state);
bool keycode_is_behavior(keycode_t keycode);
void keycode_press(keycode_state_t *state, keycode_t keycode);
int16_t keycode_transposition_for_position(const keycode_state_t *state, uint8_t position);
int8_t keycode_alteration_for_note(const keycode_state_t *state, uint8_t base_midi_note);
// ADR-0026 : bascule l'état d'altération de la racine dérivée de la note.
// direction = KEYCODE_ALTERATION_B ou _D ; root_index = index de la racine
// naturelle dans la table ; new_state = nouvel état (0, 1 ou 2).
bool keycode_alteration_toggle(keycode_state_t *state, uint8_t direction,
                               uint8_t base_midi_note, uint8_t *root_index,
                               uint8_t *new_state);
// ADR-0029 : applique l'armure de la tonalité (mode mineur/majeur) sur la
// tonique = classe naturelle de la note physique de base. Touche noire :
// *applied = false, état intact (ce n'est pas une erreur). Une nouvelle armure
// efface les surcouches ALT.
bool keycode_armure_apply(keycode_state_t *state, keycode_cle_t mode,
                          uint16_t base_midi_note, bool *applied);
// ADR-0029 : delta d'altération temporaire pour la note résolue (par pitch
// class), appliqué en dernier avant la transposition.
int8_t keycode_temp_delta(keycode_temp_t direction, int16_t resolved_note);

// ADR-0030 : côté d'une position physique (même définition que T_OCT_L/R).
bool keycode_position_is_left(uint8_t position);

// État des modificateurs maintenus.
void keycode_modifiers_init(keycode_modifiers_t *modifiers);
// Horodate l'appui/relâchement d'une direction. Les familles ALT/CLE/TEMP
// n'acceptent que les directions 0 et 1 ; TEMP_ST/TEMP_OCT les quatre.
void keycode_modifiers_update(keycode_modifiers_t *modifiers, uint8_t family,
                              uint8_t direction, bool pressed);
// Direction active : dernière pressée encore enfoncée parmi
// [first_direction, first_direction + direction_count). false si aucune.
bool keycode_modifiers_active(const keycode_modifiers_t *modifiers, uint8_t family,
                              uint8_t first_direction, uint8_t direction_count,
                              uint8_t *direction);
// ADR-0030 : delta momentané total pour une position (demi-ton ±1 + octave ±12
// du côté) ; 0 si aucun modificateur momentané n'est actif pour ce côté.
int16_t keycode_momentary_delta(const keycode_modifiers_t *modifiers, uint8_t position);

const char *keycode_name(keycode_t keycode);

#endif