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

static void log_out_chars(const char *buf, int len) {
    for (int i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            if (head - tail >= LOG_RING_SIZE) {
                ++tail;
            }
            ring[head++ & LOG_RING_MASK] = '\r';
        }
        if (head - tail >= LOG_RING_SIZE) {
            ++tail;  // écrase le plus ancien
        }
        ring[head++ & LOG_RING_MASK] = buf[i];
    }
}

static stdio_driver_t log_driver = {
    .out_chars = log_out_chars,
};

void usb_log_init(void) {
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
