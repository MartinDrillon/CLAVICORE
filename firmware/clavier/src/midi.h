#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>
#include <stdint.h>

#define MIDI_NOTE_VELOCITY 100u

// Longueur maximale d'un SysEx (F0 et F7 inclus), ADR-0003 §6.
#define MIDI_SYSEX_MAX_LEN 32u

typedef enum {
    MIDI_OK = 0,
    MIDI_NOT_MOUNTED,      // USB non monté : message perdu (compté)
    MIDI_SYSEX_TOO_LONG,   // len > MIDI_SYSEX_MAX_LEN
    MIDI_SYSEX_BUSY,       // un SysEx est déjà en cours d'émission
    MIDI_SYSEX_INVALID,    // pointeur nul, ou format hors F0 ... F7 (octets 7 bits)
} midi_result_t;

typedef struct {
    uint32_t notes_refused_during_sysex;  // notes refusées car un SysEx est en cours
    uint32_t sysex_lost_not_mounted;      // SysEx perdus car USB non monté
    uint32_t sysex_aborted;               // SysEx abandonnés (USB démonté ou blocage)
} midi_stats_t;

void midi_init(void);
void midi_task(void);
bool midi_note_on(uint8_t note, uint8_t velocity);
bool midi_note_off(uint8_t note);

// Copie le message dans un tampon et le met en attente ; midi_task() le pousse
// vers TinyUSB (écriture partielle gérée par un offset). Retourne MIDI_OK si le
// message est accepté, un code d'erreur explicite sinon (rien n'est copié).
midi_result_t midi_send_sysex(const uint8_t *data, uint16_t len);
bool midi_sysex_in_progress(void);
const midi_stats_t *midi_stats(void);

#endif