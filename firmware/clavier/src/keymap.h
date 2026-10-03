#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdint.h>

#include "keycode.h"

#define KEYMAP_POSITION_COUNT 84u

keycode_t keymap_keycode_for_position(uint8_t position);

#endif