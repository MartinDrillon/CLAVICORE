#ifndef MATRIX_SCAN_H
#define MATRIX_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#define MATRIX_ROW_COUNT 7u
#define MATRIX_COLUMN_COUNT 12u
#define MATRIX_KEY_COUNT (MATRIX_ROW_COUNT * MATRIX_COLUMN_COUNT)

typedef void (*matrix_scan_event_handler_t)(uint8_t position, bool pressed, void *context);

void matrix_scan_init(void);
void matrix_scan_poll(matrix_scan_event_handler_t event_handler, void *context);

#endif