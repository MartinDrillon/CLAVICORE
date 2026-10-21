#include "usb_log.h"

#include <stdint.h>

#include "pico/stdio/driver.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define LOG_RING_SIZE 4096u  // puissance de 2
#define LOG_RING_MASK (LOG_RING_SIZE - 1u)

static char ring[LOG_RING_SIZE];
static uint32_t head;  // prochain octet écrit
static uint32_t tail;  // prochain octet lu
static usb_log_stats_t stats;

static void ring_push(char c) {
    if (head - tail >= LOG_RING_SIZE) {
        ++tail;
        ++stats.dropped_bytes;
    }
    ring[head++ & LOG_RING_MASK] = c;
}

static void log_out_chars(const char *buf, int len) {
    for (int i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            ring_push('\r');
        }
        ring_push(buf[i]);
    }
}

static stdio_driver_t log_driver = {
    .out_chars = log_out_chars,
};

void usb_log_init(void) {
    head = 0u;
    tail = 0u;
    stats = (usb_log_stats_t){0};
    stdio_set_driver_enabled(&log_driver, true);
}

void usb_log_task(void) {
    if (!tud_cdc_connected()) {
        return;
    }
    while (tail != head) {
        const uint32_t avail = tud_cdc_write_available();
        if (avail == 0u) {
            break;
        }
        uint32_t n = head - tail;
        if (n > avail) {
            n = avail;
        }
        for (uint32_t i = 0; i < n; ++i) {
            tud_cdc_write_char(ring[tail++ & LOG_RING_MASK]);
        }
    }
    tud_cdc_write_flush();
}

usb_log_stats_t usb_log_stats(void) {
    return stats;
}

// Entrée CDC (ADR-0015). Le pilote stdio du journal n'a pas de `in_chars` et
// `pico_stdio_usb` n'est pas lié : `getchar_timeout_us()` ne peut donc jamais
// rien retourner. On lit directement le FIFO de réception de TinyUSB (alimenté
// par `tud_task()`, exécuté dans `midi_task()`).
int usb_log_read_char(void) {
    if (tud_cdc_available() == 0u) {
        return -1;
    }
    return (int)tud_cdc_read_char();
}
