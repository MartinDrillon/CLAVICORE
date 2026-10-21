// Implémentation du format de profil d'activation des solénoïdes (ADR-0013/0014).
// Voir solenoid_profile.h : aucune dépendance SDK, testable sur hôte.
#include "solenoid_profile.h"

static void put_u16_le(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8u);
}

static uint16_t get_u16_le(const uint8_t *in) {
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8u));
}

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

uint32_t solenoid_profile_crc32(const uint8_t *data, size_t length) {
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

size_t solenoid_profile_encode(const solenoid_profile_t *profile, uint8_t *out, size_t capacity) {
    size_t offset = 0u;

    if (profile == NULL || out == NULL || capacity < SOLENOID_PROFILE_WIRE_SIZE) {
        return 0u;
    }

    out[offset++] = (uint8_t)SOLENOID_PROFILE_FORMAT_VERSION;
    put_u32_le(&out[offset], profile->pwm_freq_hz);
    offset += 4u;
    put_u32_le(&out[offset], profile->pull_duration_us);
    offset += 4u;
    out[offset++] = profile->pull_duty_pct;
    put_u32_le(&out[offset], profile->pull_max_us);
    offset += 4u;
    out[offset++] = profile->hold_duty_pct;
    out[offset++] = profile->hold_duty_max_pct;
    put_u32_le(&out[offset], profile->hold_max_ms);
    offset += 4u;
    put_u32_le(&out[offset], profile->release_duration_us);
    offset += 4u;
    out[offset++] = profile->release_duty_pct;
    put_u32_le(&out[offset], profile->probe_duration_us);
    offset += 4u;
    out[offset++] = profile->probe_duty_pct;
    put_u32_le(&out[offset], profile->probe_max_us);
    offset += 4u;
    out[offset++] = profile->probe_duty_max_pct;
    out[offset++] = profile->maint_duty_pct;
    out[offset++] = profile->maint_duty_max_pct;
    put_u32_le(&out[offset], profile->maint_max_ms);
    offset += 4u;
    put_u32_le(&out[offset], profile->maint_min_rest_ms);
    offset += 4u;
    put_u32_le(&out[offset], profile->min_rest_ms);
    offset += 4u;
    put_u16_le(&out[offset], profile->dir_safety_delay_us);
    offset += 2u;
    put_u16_le(&out[offset], profile->wake_delay_us);
    offset += 2u;
    out[offset++] = profile->dir_level_pull;
    out[offset++] = profile->dir_level_release;
    out[offset++] = profile->dir_level_maint;
    out[offset++] = profile->duty_max_pct;
    return offset;
}

bool solenoid_profile_decode(const uint8_t *data, size_t length, solenoid_profile_t *out) {
    size_t offset = 0u;

    if (data == NULL || out == NULL) {
        return false;
    }
    if (length != SOLENOID_PROFILE_WIRE_SIZE || data[0] != (uint8_t)SOLENOID_PROFILE_FORMAT_VERSION) {
        return false;
    }

    ++offset;  // format_version déjà vérifiée
    out->pwm_freq_hz = get_u32_le(&data[offset]);
    offset += 4u;
    out->pull_duration_us = get_u32_le(&data[offset]);
    offset += 4u;
    out->pull_duty_pct = data[offset++];
    out->pull_max_us = get_u32_le(&data[offset]);
    offset += 4u;
    out->hold_duty_pct = data[offset++];
    out->hold_duty_max_pct = data[offset++];
    out->hold_max_ms = get_u32_le(&data[offset]);
    offset += 4u;
    out->release_duration_us = get_u32_le(&data[offset]);
    offset += 4u;
    out->release_duty_pct = data[offset++];
    out->probe_duration_us = get_u32_le(&data[offset]);
    offset += 4u;
    out->probe_duty_pct = data[offset++];
    out->probe_max_us = get_u32_le(&data[offset]);
    offset += 4u;
    out->probe_duty_max_pct = data[offset++];
    out->maint_duty_pct = data[offset++];
    out->maint_duty_max_pct = data[offset++];
    out->maint_max_ms = get_u32_le(&data[offset]);
    offset += 4u;
    out->maint_min_rest_ms = get_u32_le(&data[offset]);
    offset += 4u;
    out->min_rest_ms = get_u32_le(&data[offset]);
    offset += 4u;
    out->dir_safety_delay_us = get_u16_le(&data[offset]);
    offset += 2u;
    out->wake_delay_us = get_u16_le(&data[offset]);
    offset += 2u;
    out->dir_level_pull = data[offset++];
    out->dir_level_release = data[offset++];
    out->dir_level_maint = data[offset++];
    out->duty_max_pct = data[offset++];
    return true;
}

solenoid_profile_result_t solenoid_profile_validate(const solenoid_profile_t *profile) {
    if (profile == NULL) {
        return SOLENOID_PROFILE_ERR_NULL;
    }
    // Duties : représentables et cohérents entre eux (aucune limite thermique).
    if (profile->pull_duty_pct > 100u || profile->hold_duty_pct > 100u ||
        profile->hold_duty_max_pct > 100u || profile->release_duty_pct > 100u ||
        profile->probe_duty_pct > 100u || profile->probe_duty_max_pct > 100u ||
        profile->maint_duty_pct > 100u || profile->maint_duty_max_pct > 100u ||
        profile->duty_max_pct > 100u) {
        return SOLENOID_PROFILE_ERR_DUTY;
    }
    if (profile->hold_duty_pct > profile->hold_duty_max_pct ||
        profile->probe_duty_pct > profile->probe_duty_max_pct ||
        profile->maint_duty_pct > profile->maint_duty_max_pct ||
        profile->pull_duty_pct > profile->duty_max_pct ||
        profile->hold_duty_pct > profile->duty_max_pct ||
        profile->release_duty_pct > profile->duty_max_pct ||
        profile->probe_duty_pct > profile->duty_max_pct ||
        profile->maint_duty_pct > profile->duty_max_pct) {
        return SOLENOID_PROFILE_ERR_DUTY;
    }
    if (profile->pull_duration_us > profile->pull_max_us ||
        profile->probe_duration_us > profile->probe_max_us) {
        return SOLENOID_PROFILE_ERR_CONSISTENCY;
    }
    if (profile->dir_level_pull > 1u || profile->dir_level_release > 1u ||
        profile->dir_level_maint > 1u || profile->dir_level_pull == profile->dir_level_release) {
        return SOLENOID_PROFILE_ERR_DIR;
    }
    if (profile->pwm_freq_hz < SOLENOID_PROFILE_PWM_FREQ_MIN_HZ ||
        profile->pwm_freq_hz > SOLENOID_PROFILE_PWM_FREQ_MAX_HZ) {
        return SOLENOID_PROFILE_ERR_PWM;
    }
    // Représentabilité : une valeur en ms doit tenir en µs sur 32 bits.
    if (profile->hold_max_ms > SOLENOID_PROFILE_DURATION_MS_MAX ||
        profile->maint_max_ms > SOLENOID_PROFILE_DURATION_MS_MAX ||
        profile->maint_min_rest_ms > SOLENOID_PROFILE_DURATION_MS_MAX ||
        profile->min_rest_ms > SOLENOID_PROFILE_DURATION_MS_MAX) {
        return SOLENOID_PROFILE_ERR_DURATION;
    }
    return SOLENOID_PROFILE_OK;
}

uint32_t solenoid_profile_tag(const solenoid_profile_t *profile) {
    uint8_t wire[SOLENOID_PROFILE_WIRE_SIZE];
    const size_t length = solenoid_profile_encode(profile, wire, sizeof(wire));

    if (length == 0u) {
        return 0u;
    }
    return solenoid_profile_crc32(wire, length);
}
