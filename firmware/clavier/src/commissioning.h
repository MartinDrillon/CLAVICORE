#ifndef CLAVIER_COMMISSIONING_H
#define CLAVIER_COMMISSIONING_H

#include <stdbool.h>
#include <stdint.h>

#include "commissioning_table.h"

typedef struct {
    bool valid;
    commissioning_entry_t entries[UID_POSITION_COUNT];
    uint16_t entry_count;
    // État persisté (ADR-0009 §3) : FINAL si et seulement si aucun motif.
    commissioning_table_state_t table_state;
    uint8_t table_flags;
    commissioning_runtime_t runtime;
    bool board_online[UID_POSITION_COUNT];
    commissioning_result_t last_error;
} commissioning_state_t;

void commissioning_state_init(commissioning_state_t *state);
commissioning_result_t commissioning_state_load(commissioning_state_t *state);
commissioning_result_t commissioning_state_store(commissioning_state_t *state,
                                                 const commissioning_entry_t *entries,
                                                 uint16_t count,
                                                 commissioning_table_state_t table_state,
                                                 uint8_t table_flags);
void commissioning_state_set_all_boards_offline(commissioning_state_t *state);
bool commissioning_state_mark_uid_online(commissioning_state_t *state, const uint8_t uid[CAN_UID_SIZE],
                                         uint8_t *position_out);
bool commissioning_state_contains_uid(const commissioning_state_t *state, const uint8_t uid[CAN_UID_SIZE],
                                      uint8_t *position_out);
bool commissioning_state_is_board_online(const commissioning_state_t *state, uint8_t position);
commissioning_result_t commissioning_state_lookup_note(const commissioning_state_t *state,
                                                       uint8_t midi_note,
                                                       commissioning_note_target_t *target);
const commissioning_entry_t *commissioning_state_entry_by_position(const commissioning_state_t *state,
                                                                   uint8_t position);
bool commissioning_flash_table_is_blank(void);

#endif
