#include "midi.h"

#include "tusb.h"

void midi_init(void) {
    tusb_init();
}

void midi_task(void) {
    tud_task();
}

static bool midi_send_message(uint8_t status, uint8_t note, uint8_t velocity) {
    const uint8_t message[] = {status, note, velocity};

    if (!tud_mounted()) {
        return false;
    }

    return tud_midi_stream_write(0u, message, sizeof(message)) == sizeof(message);
}

bool midi_note_on(uint8_t note, uint8_t velocity) {
    return midi_send_message(0x90u, note, velocity);
}

bool midi_note_off(uint8_t note) {
    return midi_send_message(0x80u, note, 0u);
}