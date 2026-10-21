// Persistance flash du profil d'activation (ADR-0013 §5).
//
// Deux secteurs de 4 Kio en fin de flash, écrits en alternance (A/B) : chaque
// enregistrement porte un numéro de séquence et un CRC32. Au boot, le slot
// valide de `seq` le plus élevé gagne ; le slot au `seq` le plus faible est
// effacé et reprogrammé, ce qui laisse toujours un slot intact en cas de
// coupure d'alimentation pendant l'écriture.
#include "solenoid_profile_store.h"

#include <string.h>

#include "hardware/address_mapped.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/platform.h"

#include "solenoid_profile_defaults.h"

#define PROFILE_RECORD_HEADER_SIZE SOLENOID_PROFILE_FLASH_HEADER_SIZE
#define PROFILE_SLOT_A_OFFSET (PICO_FLASH_SIZE_BYTES - 2u * FLASH_SECTOR_SIZE)
#define PROFILE_SLOT_B_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define PROFILE_FLASH_TIMEOUT_MS 1000u
#define PROFILE_NO_SLOT 0xFFu

typedef struct {
    uint32_t offset;
    const uint8_t *data;
} profile_flash_op_t;

static solenoid_profile_t g_profile;
static uint32_t g_tag;
static can_profile_source_t g_source;
static uint32_t g_seq;
static uint8_t g_slot;  // 0 = A, 1 = B, PROFILE_NO_SLOT = aucun

static uint8_t g_sector[FLASH_SECTOR_SIZE];

static void put_u16_le(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8u);
}

static void put_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8u) & 0xFFu);
    out[2] = (uint8_t)((value >> 16u) & 0xFFu);
    out[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t get_u16_le(const uint8_t *in) {
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8u));
}

static uint32_t get_u32_le(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8u) | ((uint32_t)in[2] << 16u) |
           ((uint32_t)in[3] << 24u);
}

static uint32_t slot_offset(uint8_t slot) {
    return slot == 0u ? PROFILE_SLOT_A_OFFSET : PROFILE_SLOT_B_OFFSET;
}

static const uint8_t *slot_base(uint32_t offset) {
    return (const uint8_t *)(XIP_BASE + offset);
}

// Décode un enregistrement. Retourne false si le slot est vide ou invalide.
static bool slot_parse(const uint8_t *base, solenoid_profile_t *profile_out, uint32_t *seq_out,
                       uint32_t *tag_out) {
    const uint8_t *payload;
    uint32_t stored_crc;

    if (get_u32_le(&base[0]) != SOLENOID_PROFILE_FLASH_MAGIC ||
        get_u16_le(&base[4]) != SOLENOID_PROFILE_FLASH_VERSION ||
        get_u16_le(&base[6]) != PROFILE_RECORD_HEADER_SIZE ||
        get_u16_le(&base[8]) != SOLENOID_PROFILE_WIRE_SIZE) {
        return false;
    }
    stored_crc = get_u32_le(&base[16]);
    payload = &base[PROFILE_RECORD_HEADER_SIZE];
    if (solenoid_profile_crc32(payload, SOLENOID_PROFILE_WIRE_SIZE) != stored_crc) {
        return false;
    }
    if (!solenoid_profile_decode(payload, SOLENOID_PROFILE_WIRE_SIZE, profile_out)) {
        return false;
    }
    if (solenoid_profile_validate(profile_out) != SOLENOID_PROFILE_OK) {
        return false;
    }
    *seq_out = get_u32_le(&base[12]);
    *tag_out = stored_crc;
    return true;
}

void solenoid_profile_store_load(void) {
    solenoid_profile_t profile_a;
    solenoid_profile_t profile_b;
    uint32_t seq_a = 0u;
    uint32_t seq_b = 0u;
    uint32_t tag_a = 0u;
    uint32_t tag_b = 0u;
    const bool valid_a = slot_parse(slot_base(PROFILE_SLOT_A_OFFSET), &profile_a, &seq_a, &tag_a);
    const bool valid_b = slot_parse(slot_base(PROFILE_SLOT_B_OFFSET), &profile_b, &seq_b, &tag_b);

    if (valid_a && (!valid_b || seq_a >= seq_b)) {
        g_profile = profile_a;
        g_tag = tag_a;
        g_seq = seq_a;
        g_slot = 0u;
        g_source = CAN_PROFILE_SOURCE_NVM;
        return;
    }
    if (valid_b) {
        g_profile = profile_b;
        g_tag = tag_b;
        g_seq = seq_b;
        g_slot = 1u;
        g_source = CAN_PROFILE_SOURCE_NVM;
        return;
    }

    g_profile = k_solenoid_profile_defaults;
    g_tag = solenoid_profile_tag(&g_profile);
    g_seq = 0u;
    g_slot = PROFILE_NO_SLOT;
    g_source = CAN_PROFILE_SOURCE_DEFAULTS;
}

const solenoid_profile_t *solenoid_profile_active(void) {
    return &g_profile;
}

uint32_t solenoid_profile_active_tag(void) {
    return g_tag;
}

can_profile_source_t solenoid_profile_active_source(void) {
    return g_source;
}

static void __not_in_flash_func(profile_flash_write_cb)(void *param) {
    const profile_flash_op_t *op = (const profile_flash_op_t *)param;

    flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    flash_range_program(op->offset, op->data, FLASH_SECTOR_SIZE);
}

static void build_sector(const solenoid_profile_t *profile, uint32_t seq, uint32_t tag) {
    memset(g_sector, 0xFF, sizeof(g_sector));
    put_u32_le(&g_sector[0], SOLENOID_PROFILE_FLASH_MAGIC);
    put_u16_le(&g_sector[4], SOLENOID_PROFILE_FLASH_VERSION);
    put_u16_le(&g_sector[6], PROFILE_RECORD_HEADER_SIZE);
    put_u16_le(&g_sector[8], SOLENOID_PROFILE_WIRE_SIZE);
    put_u16_le(&g_sector[10], 0u);
    put_u32_le(&g_sector[12], seq);
    put_u32_le(&g_sector[16], tag);
    // [20..31] reste à 0xFF (réservé).
    (void)solenoid_profile_encode(profile, &g_sector[PROFILE_RECORD_HEADER_SIZE],
                                  SOLENOID_PROFILE_WIRE_SIZE);
}

bool solenoid_profile_store_write(const solenoid_profile_t *profile, uint32_t tag) {
    profile_flash_op_t op;
    const uint8_t target_slot = (g_slot == 0u) ? 1u : 0u;
    const uint32_t target_offset = slot_offset(target_slot);
    const uint32_t new_seq = g_seq + 1u;
    solenoid_profile_t verify_profile;
    uint32_t verify_seq = 0u;
    uint32_t verify_tag = 0u;

    if (profile == NULL) {
        return false;
    }

    build_sector(profile, new_seq, tag);
    op.offset = target_offset;
    op.data = g_sector;
    if (flash_safe_execute(profile_flash_write_cb, &op, PROFILE_FLASH_TIMEOUT_MS) != PICO_OK) {
        return false;
    }
    if (!slot_parse(slot_base(target_offset), &verify_profile, &verify_seq, &verify_tag) ||
        verify_seq != new_seq || verify_tag != tag) {
        return false;
    }

    g_profile = verify_profile;
    g_tag = verify_tag;
    g_seq = verify_seq;
    g_slot = target_slot;
    g_source = CAN_PROFILE_SOURCE_NVM;
    return true;
}
