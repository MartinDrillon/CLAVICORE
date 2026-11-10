#include "usb_log.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

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

// ---------------------------------------------------------------------------
//  Port 2 : Serial Studio Quick Plot (ADR-0032 §11.3)
// ---------------------------------------------------------------------------
#if CLAVIER_SERIAL_STUDIO

#define STUDIO_RING_SIZE 4096u  // puissance de 2
#define STUDIO_RING_MASK (STUDIO_RING_SIZE - 1u)

static char studio_ring[STUDIO_RING_SIZE];
static uint32_t studio_head;
static uint32_t studio_tail;
static uint32_t studio_dropped_bytes;
static uint32_t studio_reported_dropped;

static void studio_push(char c) {
    if (studio_head - studio_tail >= STUDIO_RING_SIZE) {
        // Tampon borné : on perd le plus ancien octet et on le compte, jamais
        // de blocage de la boucle principale.
        ++studio_tail;
        ++studio_dropped_bytes;
    }
    studio_ring[studio_head++ & STUDIO_RING_MASK] = c;
}

void serial_studio_init(void) {
    studio_head = 0u;
    studio_tail = 0u;
    studio_dropped_bytes = 0u;
    studio_reported_dropped = 0u;
}

void serial_studio_write(const char *text, size_t length) {
    if (text == NULL) {
        return;
    }
    for (size_t i = 0u; i < length; ++i) {
        studio_push(text[i]);
    }
}

void serial_studio_printf(const char *format, ...) {
    char line[96];
    va_list args;
    int written;

    if (format == NULL) {
        return;
    }
    va_start(args, format);
    written = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (written <= 0) {
        return;
    }
    if ((size_t)written >= sizeof(line)) {
        written = (int)(sizeof(line) - 1u);
    }
    serial_studio_write(line, (size_t)written);
}

void serial_studio_task(void) {
    // Débordement signalé une fois par valeur sur le port 1 (PuTTY), jamais
    // sur le port 2 : le flux Quick Plot reste lisible.
    if (studio_dropped_bytes != studio_reported_dropped) {
        printf("[STUDIO] debordement port2: %lu octets perdus\n",
               (unsigned long)studio_dropped_bytes);
        studio_reported_dropped = studio_dropped_bytes;
    }
    if (!tud_cdc_n_connected(1u)) {
        return;
    }
    while (studio_tail != studio_head) {
        const uint32_t available = tud_cdc_n_write_available(1u);

        if (available == 0u) {
            break;
        }
        uint32_t count = studio_head - studio_tail;

        if (count > available) {
            count = available;
        }
        for (uint32_t i = 0u; i < count; ++i) {
            tud_cdc_n_write_char(1u, studio_ring[studio_tail++ & STUDIO_RING_MASK]);
        }
    }
    tud_cdc_n_write_flush(1u);
}

#endif  // CLAVIER_SERIAL_STUDIO
