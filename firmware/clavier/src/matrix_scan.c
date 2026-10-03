#include "matrix_scan.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "hal/pinmap.generated.h"

#define MATRIX_DEBOUNCE_SAMPLES 5u
#define MATRIX_SETTLE_TIME_US 5u

static const uint row_pins[MATRIX_ROW_COUNT] = {
    PIN_L1, PIN_L2, PIN_L3, PIN_L4, PIN_L5, PIN_L6, PIN_L7,
};

static const uint column_pins[MATRIX_COLUMN_COUNT] = {
    PIN_C1, PIN_C2, PIN_C3, PIN_C4, PIN_C5, PIN_C6,
    PIN_C7, PIN_C8, PIN_C9, PIN_C10, PIN_C11, PIN_C12,
};

static bool stable_state[MATRIX_KEY_COUNT];
static bool candidate_state[MATRIX_KEY_COUNT];
static uint8_t candidate_samples[MATRIX_KEY_COUNT];

void matrix_scan_init(void) {
    for (uint row = 0; row < MATRIX_ROW_COUNT; ++row) {
        // Each switch connects a column through a diode (anode at the column,
        // cathode at the row). Rows idle high; a selected row sinks current
        // from a pull-up column through a pressed switch.
        gpio_init(row_pins[row]);
        gpio_put(row_pins[row], true);
        gpio_set_dir(row_pins[row], GPIO_OUT);
    }

    for (uint column = 0; column < MATRIX_COLUMN_COUNT; ++column) {
        gpio_init(column_pins[column]);
        gpio_set_dir(column_pins[column], GPIO_IN);
        gpio_pull_up(column_pins[column]);
    }
}

void matrix_scan_poll(matrix_scan_event_handler_t event_handler, void *context) {
    for (uint row = 0; row < MATRIX_ROW_COUNT; ++row) {
        gpio_put(row_pins[row], false);
        sleep_us(MATRIX_SETTLE_TIME_US);

        for (uint column = 0; column < MATRIX_COLUMN_COUNT; ++column) {
            const uint key_index = row * MATRIX_COLUMN_COUNT + column;
            const bool pressed = !gpio_get(column_pins[column]);

            if (pressed != candidate_state[key_index]) {
                candidate_state[key_index] = pressed;
                candidate_samples[key_index] = 1u;
            } else if (candidate_samples[key_index] < MATRIX_DEBOUNCE_SAMPLES) {
                ++candidate_samples[key_index];
            }

            if (candidate_samples[key_index] == MATRIX_DEBOUNCE_SAMPLES &&
                stable_state[key_index] != candidate_state[key_index]) {
                stable_state[key_index] = candidate_state[key_index];
                if (event_handler != NULL) {
                    event_handler((uint8_t)(key_index + 1u), stable_state[key_index], context);
                }
            }
        }

        gpio_put(row_pins[row], true);
    }
}