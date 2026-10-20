#include "commissioning_table.h"

#include <string.h>

static void put_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8u) & 0xFFu);
    out[2] = (uint8_t)((value >> 16u) & 0xFFu);
    out[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint32_t get_u32_le(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8u) | ((uint32_t)in[2] << 16u) |
           ((uint32_t)in[3] << 24u);
}

static void encode_entry(uint8_t out[COMMISSIONING_FLASH_ENTRY_SIZE],
                         const commissioning_entry_t *entry) {
    memcpy(out, entry->identity.uid.id, CAN_UID_SIZE);
    out[8] = entry->identity.position;
    can_put_u16_le(&out[9], entry->occupation_bitmap);
    can_put_u16_le(&out[11], entry->reserved);
}

static void decode_entry(const uint8_t in[COMMISSIONING_FLASH_ENTRY_SIZE], commissioning_entry_t *entry) {
    memcpy(entry->identity.uid.id, in, CAN_UID_SIZE);
    entry->identity.position = in[8];
    entry->occupation_bitmap = can_get_u16_le(&in[9]);
    entry->reserved = can_get_u16_le(&in[11]);
}

bool commissioning_bitmap_is_valid(uint16_t bitmap) {
    return (bitmap & ~CAN_OCCUPATION_BITS_MASK) == 0u;
}

uint8_t commissioning_bitmap_popcount(uint16_t bitmap) {
    uint8_t count = 0u;

    bitmap &= CAN_OCCUPATION_BITS_MASK;
    while (bitmap != 0u) {
        count = (uint8_t)(count + (bitmap & 1u));
        bitmap >>= 1u;
    }
    return count;
}

bool commissioning_midi_note_to_key(uint8_t midi_note, uint8_t *key_out) {
    if (midi_note < COMMISSIONING_MIDI_NOTE_MIN || midi_note > COMMISSIONING_MIDI_NOTE_MAX ||
        key_out == NULL) {
        return false;
    }
    *key_out = (uint8_t)(midi_note - COMMISSIONING_MIDI_NOTE_OFFSET);
    return true;
}

uint8_t commissioning_key_to_midi_note(uint8_t key) {
    return (uint8_t)(key + COMMISSIONING_MIDI_NOTE_OFFSET);
}

uint32_t commissioning_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;

    if (data == NULL) {
        return crc;
    }

    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint32_t)data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }
    return ~crc;
}

bool commissioning_entry_equal(const commissioning_entry_t *a, const commissioning_entry_t *b) {
    return a != NULL && b != NULL && a->identity.position == b->identity.position &&
           a->occupation_bitmap == b->occupation_bitmap && a->reserved == b->reserved &&
           uid_position_uid_equal(&a->identity.uid, &b->identity.uid);
}

const commissioning_entry_t *commissioning_find_entry_by_position(const commissioning_entry_t *entries,
                                                                  size_t count, uint8_t position) {
    if (entries == NULL) {
        return NULL;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (entries[i].identity.position == position) {
            return &entries[i];
        }
    }
    return NULL;
}

commissioning_result_t commissioning_validate_entries(const commissioning_entry_t *entries,
                                                      size_t count) {
    if (entries == NULL) {
        return COMMISSIONING_BAD_ARG;
    }
    if (count == 0u || count > UID_POSITION_COUNT) {
        return COMMISSIONING_BAD_COUNT;
    }
    for (size_t i = 0u; i < count; ++i) {
        const commissioning_entry_t *entry = &entries[i];

        if (!uid_position_uid_is_valid(&entry->identity.uid) ||
            entry->identity.position >= UID_POSITION_COUNT) {
            return COMMISSIONING_INVALID_ENTRY;
        }
        if (!commissioning_bitmap_is_valid(entry->occupation_bitmap)) {
            return COMMISSIONING_INVALID_BITMAP;
        }
        for (size_t j = i + 1u; j < count; ++j) {
            const commissioning_entry_t *other = &entries[j];

            if (entry->identity.position == other->identity.position) {
                return COMMISSIONING_DUPLICATE_POSITION;
            }
            if (uid_position_uid_equal(&entry->identity.uid, &other->identity.uid)) {
                return COMMISSIONING_DUPLICATE_UID;
            }
            if (can_uid_tag(entry->identity.uid.id) == can_uid_tag(other->identity.uid.id)) {
                return COMMISSIONING_DUPLICATE_TAG;
            }
        }
    }
    return COMMISSIONING_OK;
}

commissioning_result_t commissioning_build_runtime(const commissioning_entry_t *entries, size_t count,
                                                   commissioning_runtime_t *runtime) {
    uint16_t current_high = COMMISSIONING_KEY_MAX;
    uint16_t total_occupied = 0u;
    commissioning_result_t valid;

    if (entries == NULL || runtime == NULL) {
        return COMMISSIONING_BAD_ARG;
    }
    valid = commissioning_validate_entries(entries, count);
    if (valid != COMMISSIONING_OK) {
        return valid;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->entry_count = (uint16_t)count;
    for (size_t i = 0u; i < count; ++i) {
        runtime->entries[i] = entries[i];
    }

    for (int position = (int)UID_POSITION_COUNT - 1; position >= 0; --position) {
        const commissioning_entry_t *entry =
            commissioning_find_entry_by_position(entries, count, (uint8_t)position);
        uint8_t channel_index = 0u;
        uint8_t block_size;
        uint16_t block_start;

        if (entry == NULL) {
            continue;
        }

        block_size = commissioning_bitmap_popcount(entry->occupation_bitmap);
        runtime->position_present[position] = true;
        if (block_size == 0u) {
            continue;
        }
        total_occupied = (uint16_t)(total_occupied + block_size);
        if (total_occupied > COMMISSIONING_KEY_COUNT || current_high < block_size) {
            return COMMISSIONING_TOO_MANY_KEYS;
        }
        block_start = (uint16_t)(current_high - block_size + 1u);
        runtime->position_first_key[position] = (uint8_t)block_start;
        runtime->position_last_key[position] = (uint8_t)current_high;
        for (uint8_t channel = 0u; channel < CAN_IPROPI_CHANNEL_COUNT; ++channel) {
            commissioning_note_target_t *target;
            uint8_t key;

            if ((entry->occupation_bitmap & ((uint16_t)1u << channel)) == 0u) {
                continue;
            }
            key = (uint8_t)(block_start + channel_index);
            if (key < COMMISSIONING_KEY_MIN || key > COMMISSIONING_KEY_MAX) {
                return COMMISSIONING_TOO_MANY_KEYS;
            }
            target = &runtime->notes[key - COMMISSIONING_KEY_MIN];
            target->assigned = true;
            target->key = key;
            target->midi_note = commissioning_key_to_midi_note(key);
            target->position = entry->identity.position;
            target->channel = channel;
            target->uid_tag = can_uid_tag(entry->identity.uid.id);
            target->uid = entry->identity.uid;
            ++channel_index;
        }
        current_high = (uint16_t)(block_start - 1u);
    }

    runtime->total_occupied = total_occupied;
    runtime->partial = total_occupied != COMMISSIONING_KEY_COUNT;
    return COMMISSIONING_OK;
}

commissioning_result_t commissioning_lookup_midi_note(const commissioning_runtime_t *runtime,
                                                      uint8_t midi_note,
                                                      commissioning_note_target_t *target) {
    uint8_t key;

    if (runtime == NULL || target == NULL) {
        return COMMISSIONING_BAD_ARG;
    }
    if (!commissioning_midi_note_to_key(midi_note, &key)) {
        return COMMISSIONING_NOTE_NOT_FOUND;
    }
    if (!runtime->notes[key - COMMISSIONING_KEY_MIN].assigned) {
        return COMMISSIONING_NOTE_NOT_FOUND;
    }
    *target = runtime->notes[key - COMMISSIONING_KEY_MIN];
    return COMMISSIONING_OK;
}

commissioning_result_t commissioning_flash_encode(uint8_t *sector, size_t sector_size,
                                                  const commissioning_entry_t *entries,
                                                  uint16_t count) {
    commissioning_result_t valid;
    size_t entries_bytes;

    if (sector == NULL || entries == NULL) {
        return COMMISSIONING_BAD_ARG;
    }
    valid = commissioning_validate_entries(entries, count);
    if (valid != COMMISSIONING_OK) {
        return valid;
    }
    entries_bytes = (size_t)count * COMMISSIONING_FLASH_ENTRY_SIZE;
    if (sector_size < COMMISSIONING_FLASH_HEADER_SIZE + entries_bytes) {
        return COMMISSIONING_BAD_COUNT;
    }

    memset(sector, 0xFF, sector_size);
    put_u32_le(&sector[0], COMMISSIONING_FLASH_MAGIC);
    can_put_u16_le(&sector[4], COMMISSIONING_FLASH_FORMAT_VERSION);
    can_put_u16_le(&sector[6], count);
    for (uint16_t i = 0u; i < count; ++i) {
        encode_entry(&sector[COMMISSIONING_FLASH_HEADER_SIZE + (size_t)i * COMMISSIONING_FLASH_ENTRY_SIZE],
                     &entries[i]);
    }
    put_u32_le(&sector[8],
               commissioning_crc32(&sector[COMMISSIONING_FLASH_HEADER_SIZE], entries_bytes));
    return COMMISSIONING_OK;
}

commissioning_result_t commissioning_flash_decode(const uint8_t *sector, size_t sector_size,
                                                  commissioning_entry_t *entries,
                                                  uint16_t max_count, uint16_t *count_out) {
    uint16_t count;
    size_t entries_bytes;
    uint32_t expected_crc;
    uint32_t actual_crc;
    commissioning_result_t valid;

    if (sector == NULL || entries == NULL || count_out == NULL) {
        return COMMISSIONING_BAD_ARG;
    }
    if (sector_size < COMMISSIONING_FLASH_HEADER_SIZE) {
        return COMMISSIONING_BAD_COUNT;
    }
    if (get_u32_le(&sector[0]) != COMMISSIONING_FLASH_MAGIC) {
        return COMMISSIONING_BAD_MAGIC;
    }
    if (can_get_u16_le(&sector[4]) != COMMISSIONING_FLASH_FORMAT_VERSION) {
        return COMMISSIONING_BAD_VERSION;
    }
    count = can_get_u16_le(&sector[6]);
    if (count == 0u || count > max_count) {
        return COMMISSIONING_BAD_COUNT;
    }

    entries_bytes = (size_t)count * COMMISSIONING_FLASH_ENTRY_SIZE;
    if (sector_size < COMMISSIONING_FLASH_HEADER_SIZE + entries_bytes) {
        return COMMISSIONING_BAD_COUNT;
    }
    expected_crc = get_u32_le(&sector[8]);
    actual_crc = commissioning_crc32(&sector[COMMISSIONING_FLASH_HEADER_SIZE], entries_bytes);
    if (expected_crc != actual_crc) {
        return COMMISSIONING_BAD_CRC;
    }

    for (uint16_t i = 0u; i < count; ++i) {
        decode_entry(&sector[COMMISSIONING_FLASH_HEADER_SIZE + (size_t)i * COMMISSIONING_FLASH_ENTRY_SIZE],
                     &entries[i]);
    }
    valid = commissioning_validate_entries(entries, count);
    if (valid != COMMISSIONING_OK) {
        return valid;
    }
    *count_out = count;
    return COMMISSIONING_OK;
}

const char *commissioning_result_name(commissioning_result_t result) {
    switch (result) {
    case COMMISSIONING_OK: return "OK";
    case COMMISSIONING_BAD_ARG: return "BAD_ARG";
    case COMMISSIONING_BAD_MAGIC: return "BAD_MAGIC";
    case COMMISSIONING_BAD_VERSION: return "BAD_VERSION";
    case COMMISSIONING_BAD_COUNT: return "BAD_COUNT";
    case COMMISSIONING_BAD_CRC: return "BAD_CRC";
    case COMMISSIONING_INVALID_ENTRY: return "INVALID_ENTRY";
    case COMMISSIONING_INVALID_BITMAP: return "INVALID_BITMAP";
    case COMMISSIONING_DUPLICATE_POSITION: return "DUPLICATE_POSITION";
    case COMMISSIONING_DUPLICATE_UID: return "DUPLICATE_UID";
    case COMMISSIONING_DUPLICATE_TAG: return "DUPLICATE_TAG";
    case COMMISSIONING_TOO_MANY_KEYS: return "TOO_MANY_KEYS";
    case COMMISSIONING_NOTE_NOT_FOUND: return "NOTE_NOT_FOUND";
    default: return "?";
    }
}
