#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "keycode.h"
#include "can_link.h"
#include "keymap.h"
#include "matrix_scan.h"
#include "midi.h"
#include "note_state.h"
#include "can_protocol.h"
#include "sysex_uid.h"
#include "usb_log.h"

typedef struct {
    keycode_state_t keycode;
    note_state_t notes;
} clavier_state_t;

static void handle_matrix_transition(uint8_t position, bool pressed, void *context) {
    clavier_state_t *state = context;
    const keycode_t keycode = keymap_keycode_for_position(position);

    if (keycode.kind == KEYCODE_NONE) {
        printf("position %u unused %s\n", position, pressed ? "pressed" : "released");
        return;
    }

    if (keycode.kind == KEYCODE_NOTE) {
        uint8_t midi_note;

        if (pressed) {
            if (!note_state_press(&state->notes, &state->keycode, position,
                                  keycode.value, &midi_note)) {
                printf("position %u note resolution failed\n", position);
                return;
            }
            if (!midi_note_on(midi_note, MIDI_NOTE_VELOCITY)) {
                printf("position %u note %u pressed, USB MIDI unavailable\n",
                       position, midi_note);
            }
        } else if (note_state_release(&state->notes, position, &midi_note)) {
            if (!midi_note_off(midi_note)) {
                printf("position %u note %u released, USB MIDI unavailable\n",
                       position, midi_note);
            }
        }
        return;
    }

    if (keycode.kind == KEYCODE_GET_UID) {
        // ADR-0003 : demande d'UID Moteur, à l'appui seulement (pas d'état dans keycode_state_t).
        if (pressed) {
            const can_link_request_result_t result = can_link_request_uid();
            printf("position %u GET_UID result %d (0=sent 1=busy 2=tx_fail)\n", position,
                   (int)result);
        }
        return;
    }

    if (pressed) {
        keycode_press(&state->keycode, keycode);
    }
    if (keycode.kind == KEYCODE_ALTERATION) {
        printf("position %u behavior %s state %u %s\n", position, keycode_name(keycode),
               keycode_alteration_state(&state->keycode, keycode),
               pressed ? "pressed" : "released");
        return;
    }
    printf("position %u behavior %s %s\n", position, keycode_name(keycode),
           pressed ? "pressed" : "released");
}

// Transmet à midi le prochain SysEx en file (un seul en vol à la fois).
static void sysex_pump(void) {
    can_link_sysex_t sysex;

    while (can_link_peek_sysex(&sysex)) {
        const midi_result_t result = midi_send_sysex(sysex.data, sysex.len);
        if (result == MIDI_SYSEX_BUSY) {
            return;  // on réessaie au tour suivant, le message reste en file
        }
        can_link_pop_sysex();  // accepté, ou perdu (USB non monté : compté par midi.c)
        if (result == MIDI_OK) {
            return;  // un SysEx est maintenant en cours : les suivants attendent
        }
        if (result != MIDI_NOT_MOUNTED) {
            printf("SysEx rejected by midi (%d)\n", (int)result);
        }
    }
}

int main(void) {
    clavier_state_t state;

    usb_log_init();
    matrix_scan_init();
    keycode_state_init(&state.keycode);
    note_state_init(&state.notes);
    midi_init();
    can_link_init();

    while (true) {
        // Ordre ADR-0003 : CAN, puis émission SysEx, puis midi_task().
        can_link_poll();
        sysex_pump();
        midi_task();
        usb_log_task();
        matrix_scan_poll(handle_matrix_transition, &state);
        sleep_us(215);
    }
}