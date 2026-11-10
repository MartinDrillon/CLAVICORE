// Implémentation du jeu canonique des paramètres d'écoute (ADR-0032 §5).
//
// Aucune valeur de réglage n'est compilée ici : seules les bornes
// structurelles de validation sont connues, et elles ne sont pas des limites
// électriques. Le format sérialisé est le contrat diffusé par LISTEN_PARAM :
// 18 u32 little-endian, ids 1..18, sans en-tête.
#include "listen_params.h"

#include <stddef.h>
#include <string.h>

// Table unique id -> nom -> offset : garantit que noms, get/set et
// sérialisation restent alignés sur l'ordre canonique des ids.
typedef struct {
    const char *name;
    size_t offset;
} listen_field_desc_t;

#define LISTEN_FIELD(field) {#field, offsetof(listen_params_t, field)}

static const listen_field_desc_t k_listen_fields[LISTEN_PARAMS_COUNT] = {
    LISTEN_FIELD(settle_us),
    LISTEN_FIELD(samples_per_dwell),
    LISTEN_FIELD(baseline_init_ms),
    LISTEN_FIELD(baseline_tau_ms),
    LISTEN_FIELD(rise_thr),
    LISTEN_FIELD(return_thr),
    LISTEN_FIELD(return_hold_us),
    LISTEN_FIELD(split_pct),
    LISTEN_FIELD(min_pulse_us),
    LISTEN_FIELD(drive_blank_ms),
    LISTEN_FIELD(trace_period_us),
    LISTEN_FIELD(trace_trig),
    LISTEN_FIELD(trace_timeout_ms),
    LISTEN_FIELD(stream_decim),
    LISTEN_FIELD(vel_fixed),
    LISTEN_FIELD(vel_min_delta),
    LISTEN_FIELD(vel_max_delta),
    LISTEN_FIELD(detail),
};

static bool listen_id_is_valid(uint8_t id) {
    return id >= 1u && id <= LISTEN_PARAMS_COUNT;
}

static const listen_field_desc_t *listen_field_at(uint8_t id) {
    if (!listen_id_is_valid(id)) {
        return NULL;
    }
    return &k_listen_fields[id - 1u];
}

size_t listen_params_field_count(void) {
    return LISTEN_PARAMS_COUNT;
}

const char *listen_params_field_name(uint8_t id) {
    const listen_field_desc_t *field = listen_field_at(id);

    return field == NULL ? NULL : field->name;
}

bool listen_params_field_id(const char *name, uint8_t *id) {
    if (name == NULL || id == NULL) {
        return false;
    }
    for (uint8_t i = 0u; i < LISTEN_PARAMS_COUNT; ++i) {
        if (strcmp(k_listen_fields[i].name, name) == 0) {
            *id = (uint8_t)(i + 1u);
            return true;
        }
    }
    return false;
}

bool listen_params_get(const listen_params_t *p, uint8_t id, uint32_t *out) {
    const listen_field_desc_t *field = listen_field_at(id);

    if (p == NULL || out == NULL || field == NULL) {
        return false;
    }
    memcpy(out, (const uint8_t *)p + field->offset, sizeof(*out));
    return true;
}

bool listen_params_set(listen_params_t *p, uint8_t id, uint32_t value) {
    const listen_field_desc_t *field = listen_field_at(id);

    if (p == NULL || field == NULL) {
        return false;
    }
    memcpy((uint8_t *)p + field->offset, &value, sizeof(value));
    return true;
}

listen_params_result_t listen_params_validate(const listen_params_t *p, uint8_t *bad_id) {
    uint8_t faulty = 0u;

    if (bad_id != NULL) {
        *bad_id = 0u;
    }
    if (p == NULL) {
        return LISTEN_PARAMS_ERR_ARG;
    }

    // Contrôles dans l'ordre des ids : `bad_id` désigne le premier fautif.
    if (p->samples_per_dwell < 1u || p->samples_per_dwell > 64u) {
        faulty = LISTEN_PARAM_SAMPLES_PER_DWELL;
    } else if (p->return_thr >= p->rise_thr) {
        faulty = LISTEN_PARAM_RETURN_THR;
    } else if (p->split_pct < 1u || p->split_pct > 99u) {
        faulty = LISTEN_PARAM_SPLIT_PCT;
    } else if (p->trace_period_us < 2u) {
        faulty = LISTEN_PARAM_TRACE_PERIOD_US;
    } else if (p->trace_timeout_ms < 1u) {
        faulty = LISTEN_PARAM_TRACE_TIMEOUT_MS;
    } else if (p->stream_decim < 1u) {
        faulty = LISTEN_PARAM_STREAM_DECIM;
    } else if (p->vel_fixed < 1u || p->vel_fixed > 127u) {
        faulty = LISTEN_PARAM_VEL_FIXED;
    } else if (!((p->vel_min_delta == 0u && p->vel_max_delta == 0u) ||
                 (p->vel_min_delta > 0u && p->vel_max_delta > 0u &&
                  p->vel_min_delta < p->vel_max_delta))) {
        faulty = LISTEN_PARAM_VEL_MIN_DELTA;
    } else if (p->detail > 1u) {
        faulty = LISTEN_PARAM_DETAIL;
    }

    if (faulty != 0u) {
        if (bad_id != NULL) {
            *bad_id = faulty;
        }
        return LISTEN_PARAMS_ERR_FIELD;
    }
    return LISTEN_PARAMS_OK;
}

size_t listen_params_serialize(const listen_params_t *p,
                               uint8_t out[LISTEN_PARAMS_WIRE_SIZE]) {
    if (p == NULL || out == NULL) {
        return 0u;
    }
    for (uint8_t i = 0u; i < LISTEN_PARAMS_COUNT; ++i) {
        uint32_t value;

        // Les champs sont lus par la même table que get/set : l'ordre sérialisé
        // est exactement l'ordre des ids 1..18.
        memcpy(&value, (const uint8_t *)p + k_listen_fields[i].offset, sizeof(value));
        can_put_u16_le(&out[4u * i], (uint16_t)(value & 0xFFFFu));
        can_put_u16_le(&out[4u * i + 2u], (uint16_t)(value >> 16u));
    }
    return LISTEN_PARAMS_WIRE_SIZE;
}

uint16_t listen_params_crc16(const listen_params_t *p) {
    uint8_t wire[LISTEN_PARAMS_WIRE_SIZE];

    if (p == NULL) {
        return 0xFFFFu;
    }
    if (listen_params_serialize(p, wire) != LISTEN_PARAMS_WIRE_SIZE) {
        return 0xFFFFu;
    }
    return can_crc16_ccitt(wire, LISTEN_PARAMS_WIRE_SIZE);
}
