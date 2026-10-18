#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>
#include <stdint.h>

#define MIDI_NOTE_VELOCITY 100u

void midi_init(void);
void midi_task(void);
bool midi_note_on(uint8_t note, uint8_t velocity);
bool midi_note_off(uint8_t note);

#endif