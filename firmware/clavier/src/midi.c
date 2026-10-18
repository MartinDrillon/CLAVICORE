#include "midi.h"

#include "pico/time.h"
#include "tusb.h"

// Si un SysEx reste bloqué plus longtemps (hôte qui ne lit plus), il est
// abandonné pour ne pas refuser les notes indéfiniment.
#define SYSEX_STALL_TIMEOUT_MS 200u

static uint8_t sysex_pending[MIDI_SYSEX_MAX_LEN];
static uint16_t sysex_pending_len;    // 0 = aucun SysEx en cours
static uint16_t sysex_pending_offset;  // octets déjà consommés par TinyUSB
static uint32_t sysex_started_ms;
static midi_stats_t stats;

void midi_init(void) {
    tusb_init();
    sysex_pending_len = 0u;
    sysex_pending_offset = 0u;
    stats = (midi_stats_t){0};
}

static void sysex_abort(void) {
    // Meilleur effort pour refermer un SysEx partiellement écrit ; sans effet si
    // le FIFO est plein.
    if (sysex_pending_offset > 0u && tud_mounted()) {
        const uint8_t end = 0xF7u;
        (void)tud_midi_stream_write(0u, &end, 1u);
    }
    sysex_pending_len = 0u;
    sysex_pending_offset = 0u;
    ++stats.sysex_aborted;
}

void midi_task(void) {
    tud_task();

    if (sysex_pending_len == 0u) {
        return;
    }
    if (!tud_mounted()) {
        sysex_abort();
        return;
    }

    const uint32_t written = tud_midi_stream_write(
        0u, &sysex_pending[sysex_pending_offset],
        (uint32_t)(sysex_pending_len - sysex_pending_offset));
    sysex_pending_offset = (uint16_t)(sysex_pending_offset + written);

    if (sysex_pending_offset >= sysex_pending_len) {
        sysex_pending_len = 0u;
        sysex_pending_offset = 0u;
    } else if ((uint32_t)(to_ms_since_boot(get_absolute_time()) - sysex_started_ms) >
               SYSEX_STALL_TIMEOUT_MS) {
        sysex_abort();
    }
}

midi_result_t midi_send_sysex(const uint8_t *data, uint16_t len) {
    if (data == NULL || len < 2u || data[0] != 0xF0u || data[len - 1u] != 0xF7u) {
        return MIDI_SYSEX_INVALID;
    }
    if (len > MIDI_SYSEX_MAX_LEN) {
        return MIDI_SYSEX_TOO_LONG;
    }
    for (uint16_t i = 1u; i + 1u < len; ++i) {
        if (data[i] > 0x7Fu) {
            return MIDI_SYSEX_INVALID;
        }
    }
    if (!tud_mounted()) {
        ++stats.sysex_lost_not_mounted;
        return MIDI_NOT_MOUNTED;
    }
    if (sysex_pending_len != 0u) {
        return MIDI_SYSEX_BUSY;
    }

    for (uint16_t i = 0u; i < len; ++i) {
        sysex_pending[i] = data[i];
    }
    sysex_pending_offset = 0u;
    sysex_pending_len = len;
    sysex_started_ms = to_ms_since_boot(get_absolute_time());
    return MIDI_OK;
}

bool midi_sysex_in_progress(void) {
    return sysex_pending_len != 0u;
}

const midi_stats_t *midi_stats(void) {
    return &stats;
}

static bool midi_send_message(uint8_t status, uint8_t note, uint8_t velocity) {
    const uint8_t message[] = {status, note, velocity};

    // Un message de note ne doit pas s'intercaler dans un SysEx en cours.
    if (sysex_pending_len != 0u) {
        ++stats.notes_refused_during_sysex;
        return false;
    }
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