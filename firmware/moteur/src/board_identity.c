#include "board_identity.h"

#include <string.h>

#include "pico/unique_id.h"

bool board_identity_read(uint8_t uid[BOARD_IDENTITY_UID_SIZE]) {
    if (uid == NULL) {
        return false;
    }

    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);

    bool all_zero = true;
    bool all_ff = true;
    for (uint8_t i = 0u; i < BOARD_IDENTITY_UID_SIZE; ++i) {
        uid[i] = id.id[i];
        if (id.id[i] != 0x00u) {
            all_zero = false;
        }
        if (id.id[i] != 0xFFu) {
            all_ff = false;
        }
    }
    if (all_zero || all_ff) {
        memset(uid, 0, BOARD_IDENTITY_UID_SIZE);
        return false;
    }
    return true;
}
