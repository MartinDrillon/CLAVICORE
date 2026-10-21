#include "commissioning.h"

#include <string.h>

#include "hardware/address_mapped.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/platform.h"

#define COMMISSIONING_FLASH_TIMEOUT_MS 1000u
#define COMMISSIONING_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t offset;
    const uint8_t *data;
} commissioning_flash_op_t;

static const uint8_t *commissioning_flash_base(void) {
    return (const uint8_t *)(XIP_BASE + COMMISSIONING_FLASH_OFFSET);
}

bool commissioning_flash_table_is_blank(void) {
    const uint8_t *flash = commissioning_flash_base();

    for (size_t i = 0u; i < FLASH_SECTOR_SIZE; ++i) {
        if (flash[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static void __not_in_flash_func(commissioning_flash_write_callback)(void *param) {
    const commissioning_flash_op_t *op = (const commissioning_flash_op_t *)param;

    flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    flash_range_program(op->offset, op->data, FLASH_SECTOR_SIZE);
}

void commissioning_state_init(commissioning_state_t *state) {
    if (state != NULL) {
        *state = (commissioning_state_t){0};
        state->last_error = COMMISSIONING_BAD_MAGIC;
    }
}

commissioning_result_t commissioning_state_load(commissioning_state_t *state) {
    commissioning_entry_t entries[UID_POSITION_COUNT];
    commissioning_table_state_t table_state = COMMISSIONING_TABLE_STATE_FINAL;
    uint8_t table_flags = 0u;
    uint16_t count = 0u;
    commissioning_result_t result;
    commissioning_runtime_t runtime;

    if (state == NULL) {
        return COMMISSIONING_BAD_ARG;
    }

    result = commissioning_flash_decode(commissioning_flash_base(), FLASH_SECTOR_SIZE, entries,
                                        UID_POSITION_COUNT, &count, &table_state, &table_flags);
    if (result != COMMISSIONING_OK) {
        state->valid = false;
        state->entry_count = 0u;
        state->table_state = COMMISSIONING_TABLE_STATE_FINAL;
        state->table_flags = 0u;
        memset(&state->runtime, 0, sizeof(state->runtime));
        memset(state->board_online, 0, sizeof(state->board_online));
        state->last_error = result;
        return result;
    }

    result = commissioning_build_runtime(entries, count, &runtime);
    if (result != COMMISSIONING_OK) {
        state->valid = false;
        state->entry_count = 0u;
        state->table_state = COMMISSIONING_TABLE_STATE_FINAL;
        state->table_flags = 0u;
        memset(&state->runtime, 0, sizeof(state->runtime));
        memset(state->board_online, 0, sizeof(state->board_online));
        state->last_error = result;
        return result;
    }

    memset(state, 0, sizeof(*state));
    state->valid = true;
    state->entry_count = count;
    state->table_state = table_state;
    state->table_flags = table_flags;
    state->runtime = runtime;
    for (uint16_t i = 0u; i < count; ++i) {
        state->entries[i] = entries[i];
    }
    state->last_error = COMMISSIONING_OK;
    return COMMISSIONING_OK;
}

commissioning_result_t commissioning_state_store(commissioning_state_t *state,
                                                 const commissioning_entry_t *entries,
                                                 uint16_t count,
                                                 commissioning_table_state_t table_state,
                                                 uint8_t table_flags) {
    uint8_t sector[FLASH_SECTOR_SIZE];
    commissioning_runtime_t runtime;
    commissioning_flash_op_t op;
    commissioning_result_t result;
    int flash_rc;

    if (state == NULL || entries == NULL) {
        return COMMISSIONING_BAD_ARG;
    }

    result = commissioning_build_runtime(entries, count, &runtime);
    if (result != COMMISSIONING_OK) {
        state->last_error = result;
        return result;
    }
    result = commissioning_flash_encode(sector, sizeof(sector), entries, count, table_state,
                                        table_flags);
    if (result != COMMISSIONING_OK) {
        state->last_error = result;
        return result;
    }

    op.offset = COMMISSIONING_FLASH_OFFSET;
    op.data = sector;
    flash_rc = flash_safe_execute(commissioning_flash_write_callback, &op,
                                  COMMISSIONING_FLASH_TIMEOUT_MS);
    if (flash_rc != PICO_OK) {
        state->last_error = COMMISSIONING_BAD_ARG;
        return COMMISSIONING_BAD_ARG;
    }

    memset(state, 0, sizeof(*state));
    state->valid = true;
    state->entry_count = count;
    state->table_state = table_state;
    state->table_flags = table_flags;
    state->runtime = runtime;
    for (uint16_t i = 0u; i < count; ++i) {
        state->entries[i] = entries[i];
    }
    state->last_error = COMMISSIONING_OK;
    return COMMISSIONING_OK;
}

void commissioning_state_set_all_boards_offline(commissioning_state_t *state) {
    if (state != NULL) {
        memset(state->board_online, 0, sizeof(state->board_online));
    }
}

bool commissioning_state_mark_uid_online(commissioning_state_t *state, const uint8_t uid[CAN_UID_SIZE],
                                         uint8_t *position_out) {
    clavicore_uid64_t uid64;

    if (state == NULL || uid == NULL || !state->valid) {
        return false;
    }
    memcpy(uid64.id, uid, CAN_UID_SIZE);
    for (uint16_t i = 0u; i < state->entry_count; ++i) {
        if (uid_position_uid_equal(&state->entries[i].identity.uid, &uid64)) {
            const uint8_t position = state->entries[i].identity.position;

            if (position < UID_POSITION_COUNT) {
                state->board_online[position] = true;
                if (position_out != NULL) {
                    *position_out = position;
                }
                return true;
            }
        }
    }
    return false;
}

bool commissioning_state_contains_uid(const commissioning_state_t *state, const uint8_t uid[CAN_UID_SIZE],
                                      uint8_t *position_out) {
    clavicore_uid64_t uid64;

    if (state == NULL || uid == NULL || !state->valid) {
        return false;
    }
    memcpy(uid64.id, uid, CAN_UID_SIZE);
    for (uint16_t i = 0u; i < state->entry_count; ++i) {
        if (uid_position_uid_equal(&state->entries[i].identity.uid, &uid64)) {
            if (position_out != NULL) {
                *position_out = state->entries[i].identity.position;
            }
            return true;
        }
    }
    return false;
}

bool commissioning_state_is_board_online(const commissioning_state_t *state, uint8_t position) {
    return state != NULL && state->valid && position < UID_POSITION_COUNT && state->board_online[position];
}

commissioning_result_t commissioning_state_lookup_note(const commissioning_state_t *state,
                                                       uint8_t midi_note,
                                                       commissioning_note_target_t *target) {
    commissioning_result_t result;

    if (state == NULL || target == NULL) {
        return COMMISSIONING_BAD_ARG;
    }
    if (!state->valid) {
        return COMMISSIONING_BAD_MAGIC;
    }
    result = commissioning_lookup_midi_note(&state->runtime, midi_note, target);
    if (result != COMMISSIONING_OK) {
        return result;
    }
    if (!commissioning_state_is_board_online(state, target->position)) {
        return COMMISSIONING_INVALID_ENTRY;
    }
    return COMMISSIONING_OK;
}

const commissioning_entry_t *commissioning_state_entry_by_position(const commissioning_state_t *state,
                                                                   uint8_t position) {
    if (state == NULL || !state->valid) {
        return NULL;
    }
    return commissioning_find_entry_by_position(state->entries, state->entry_count, position);
}
