#ifndef CLAVICORE_COMMISSIONING_TABLE_H
#define CLAVICORE_COMMISSIONING_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_protocol.h"
#include "uid_position_format.h"

#define COMMISSIONING_FLASH_MAGIC \
    ((uint32_t)'C' | ((uint32_t)'L' << 8u) | ((uint32_t)'V' << 16u) | ((uint32_t)'C' << 24u))
#define COMMISSIONING_FLASH_FORMAT_VERSION 1u
#define COMMISSIONING_FLASH_SECTOR_SIZE 4096u
#define COMMISSIONING_FLASH_HEADER_SIZE 12u
#define COMMISSIONING_FLASH_ENTRY_SIZE 13u

#define COMMISSIONING_KEY_MIN 1u
#define COMMISSIONING_KEY_MAX 49u
#define COMMISSIONING_KEY_COUNT 49u
#define COMMISSIONING_MIDI_NOTE_MIN 36u
#define COMMISSIONING_MIDI_NOTE_MAX 84u
#define COMMISSIONING_MIDI_NOTE_OFFSET 35u

typedef enum {
    COMMISSIONING_OK = 0,
    COMMISSIONING_BAD_ARG,
    COMMISSIONING_BAD_MAGIC,
    COMMISSIONING_BAD_VERSION,
    COMMISSIONING_BAD_COUNT,
    COMMISSIONING_BAD_CRC,
    COMMISSIONING_INVALID_ENTRY,
    COMMISSIONING_INVALID_BITMAP,
    COMMISSIONING_DUPLICATE_POSITION,
    COMMISSIONING_DUPLICATE_UID,
    COMMISSIONING_DUPLICATE_TAG,
    COMMISSIONING_TOO_MANY_KEYS,
    COMMISSIONING_NOTE_NOT_FOUND,
} commissioning_result_t;

typedef struct {
    uid_position_entry_t identity;
    uint16_t occupation_bitmap;
    uint16_t reserved;
} commissioning_entry_t;

typedef struct {
    bool assigned;
    uint8_t key;
    uint8_t midi_note;
    uint8_t position;
    uint8_t channel;
    uint16_t uid_tag;
    clavicore_uid64_t uid;
} commissioning_note_target_t;

typedef struct {
    commissioning_entry_t entries[UID_POSITION_COUNT];
    uint16_t entry_count;
    commissioning_note_target_t notes[COMMISSIONING_KEY_COUNT];
    uint16_t total_occupied;
    bool partial;
    bool position_present[UID_POSITION_COUNT];
    uint8_t position_first_key[UID_POSITION_COUNT];
    uint8_t position_last_key[UID_POSITION_COUNT];
} commissioning_runtime_t;

bool commissioning_bitmap_is_valid(uint16_t bitmap);
uint8_t commissioning_bitmap_popcount(uint16_t bitmap);
bool commissioning_midi_note_to_key(uint8_t midi_note, uint8_t *key_out);
uint8_t commissioning_key_to_midi_note(uint8_t key);
uint32_t commissioning_crc32(const uint8_t *data, size_t length);
bool commissioning_entry_equal(const commissioning_entry_t *a, const commissioning_entry_t *b);
const commissioning_entry_t *commissioning_find_entry_by_position(const commissioning_entry_t *entries,
                                                                  size_t count, uint8_t position);
commissioning_result_t commissioning_validate_entries(const commissioning_entry_t *entries,
                                                      size_t count);
commissioning_result_t commissioning_build_runtime(const commissioning_entry_t *entries, size_t count,
                                                   commissioning_runtime_t *runtime);
commissioning_result_t commissioning_lookup_midi_note(const commissioning_runtime_t *runtime,
                                                      uint8_t midi_note,
                                                      commissioning_note_target_t *target);
commissioning_result_t commissioning_flash_encode(uint8_t *sector, size_t sector_size,
                                                  const commissioning_entry_t *entries,
                                                  uint16_t count);
commissioning_result_t commissioning_flash_decode(const uint8_t *sector, size_t sector_size,
                                                  commissioning_entry_t *entries,
                                                  uint16_t max_count, uint16_t *count_out);
const char *commissioning_result_name(commissioning_result_t result);

#endif
