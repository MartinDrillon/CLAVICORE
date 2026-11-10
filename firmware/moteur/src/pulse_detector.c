#include "pulse_detector.h"

#include <string.h>

// Borne l'accumulation de la base initiale : 65 535 échantillons x 4095 tient
// dans un u32 (268 M). Au-delà, l'init se conclut sur la moyenne courante pour
// ne jamais déborder ; `baseline_init_ms` reste structurellement libre.
#define PULSE_INIT_MAX_SAMPLES 65535u
#define PULSE_Q16_ONE 65536u

static uint16_t baseline_u16(const pulse_detector_t *pd) {
    uint32_t q = pd->baseline_q16;
    const uint32_t q_max = ((uint32_t)CAN_ADC_12BIT_MAX) << 16;

    if (q > q_max) {
        q = q_max;
    }
    return (uint16_t)((q + (PULSE_Q16_ONE / 2u)) >> 16);
}

static uint32_t ms_to_us_u32(uint32_t ms) {
    const uint64_t us = (uint64_t)ms * 1000u;

    // Plafond structurel : la fin de fenêtre est comparée par différence signée
    // 32 bits, valable pour une durée < ~35 min (baseline_init_ms libre).
    return us > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)us;
}

// Suivi lent de la base, uniquement en IDLE (ADR-0032 §2). Le produit
// delta x dt est fait en 64 bits : dt peut atteindre plusieurs secondes.
static void track_baseline(pulse_detector_t *pd, uint16_t sample, uint32_t now_us) {
    if (pd->cfg.baseline_tau_ms == 0u) {
        return;  // suivi désactivé explicitement (tau nul), jamais une division par zéro
    }

    const uint32_t dt_us = now_us - pd->last_sample_us;
    if (dt_us == 0u) {
        return;
    }

    const int64_t target = ((int64_t)sample) << 16;
    const int64_t current = (int64_t)pd->baseline_q16;
    const int64_t denominator = ((int64_t)pd->cfg.baseline_tau_ms) * 1000;
    const int64_t step = (target - current) * (int64_t)dt_us / denominator;
    int64_t next = current + step;
    const int64_t q_max = ((int64_t)CAN_ADC_12BIT_MAX) << 16;

    if (next < 0) {
        next = 0;
    } else if (next > q_max) {
        next = q_max;
    }
    pd->baseline_q16 = (uint32_t)next;
}

// Clôt l'impulsion courante. Retourne true si elle est rapportée dans `out`.
static bool finish_pulse(pulse_detector_t *pd, uint32_t t_end_us, bool split, pulse_event_t *out) {
    const uint32_t duration_us = t_end_us - pd->t0_us;

    pd->state = PULSE_STATE_IDLE;
    pd->trough_armed = false;
    if (!split) {
        // Le retour a été observé calme : l'EMA peut reprendre de ce point.
        pd->last_calm_us = t_end_us;
    }
    if (duration_us < pd->cfg.min_pulse_us) {
        ++pd->rejected;
        return false;
    }
    if (out != NULL) {
        const uint32_t delta = (pd->peak > pd->base_at_start)
                                   ? (uint32_t)pd->peak - (uint32_t)pd->base_at_start
                                   : 0u;
        out->base = pd->base_at_start;
        out->peak = pd->peak;
        out->delta = (delta > 0xFFFFu) ? 0xFFFFu : (uint16_t)delta;
        out->t_peak_us = pd->t_peak_us - pd->t0_us;
        out->t_end_us = t_end_us - pd->t0_us;
        out->split = split;
    }
    return true;
}

static bool feed_init(pulse_detector_t *pd, uint16_t sample, uint32_t now_us) {
    if (pd->cfg.baseline_init_ms == 0u) {
        // Pas de fenêtre : la base est le premier échantillon (ADR-0032 §2).
        pd->baseline_q16 = ((uint32_t)sample) << 16;
        pd->state = PULSE_STATE_IDLE;
        pd->last_calm_us = now_us;
        return false;
    }

    pd->init_sum += sample;
    ++pd->init_count;
    if ((int32_t)(now_us - pd->init_end_us) < 0 && pd->init_count < PULSE_INIT_MAX_SAMPLES) {
        return false;
    }

    const uint32_t mean = (pd->init_sum + (pd->init_count / 2u)) / pd->init_count;
    pd->baseline_q16 = mean << 16;
    pd->state = PULSE_STATE_IDLE;
    pd->last_calm_us = now_us;
    return false;
}

static bool feed_idle(pulse_detector_t *pd, uint16_t sample, uint32_t now_us, pulse_event_t *out) {
    (void)out;
    const uint16_t base = baseline_u16(pd);
    const uint32_t return_level = (uint32_t)base + pd->cfg.return_thr;
    const uint32_t rise_level = (uint32_t)base + pd->cfg.rise_thr;

    if ((uint32_t)sample <= return_level) {
        pd->last_calm_us = now_us;
    }
    track_baseline(pd, sample, now_us);

    if ((uint32_t)sample > rise_level) {
        pd->base_at_start = base;
        pd->t0_us = pd->last_calm_us;
        pd->peak = sample;
        pd->trough = sample;
        pd->trough_armed = false;
        pd->t_peak_us = now_us;
        pd->t_trough_us = now_us;
        pd->state = PULSE_STATE_RISE;
    }
    return false;
}

static bool feed_rise(pulse_detector_t *pd, uint16_t sample, uint32_t now_us, pulse_event_t *out) {
    const uint16_t base = baseline_u16(pd);
    const uint32_t return_level = (uint32_t)base + pd->cfg.return_thr;
    bool reported = false;

    if ((uint32_t)sample > (uint32_t)pd->peak) {
        pd->peak = sample;
        pd->t_peak_us = now_us;
    }

    // Coupure de trille : après la crête, creux sous split_pct % de la crête,
    // puis remontée de plus de rise_thr au-dessus du creux (ADR-0032 §2).
    const uint32_t split_level =
        (uint32_t)base +
        (uint32_t)(((uint32_t)pd->cfg.split_pct * (uint32_t)(pd->peak - base)) / 100u);
    if (pd->trough_armed) {
        if ((uint32_t)sample < (uint32_t)pd->trough) {
            pd->trough = sample;
            pd->t_trough_us = now_us;
        }
        if ((uint32_t)sample > ((uint32_t)pd->trough + pd->cfg.rise_thr)) {
            const uint32_t t_split_end = pd->t_trough_us;
            reported = finish_pulse(pd, t_split_end, true, out);
            // Nouvelle impulsion ouverte au creux, à l'échantillon courant.
            pd->t0_us = t_split_end;
            pd->base_at_start = base;
            pd->peak = sample;
            pd->trough = sample;
            pd->trough_armed = false;
            pd->t_peak_us = now_us;
            pd->t_trough_us = now_us;
            pd->state = PULSE_STATE_RISE;
            return reported;
        }
    } else if ((uint32_t)sample < split_level) {
        pd->trough = sample;
        pd->t_trough_us = now_us;
        pd->trough_armed = true;
    }

    if ((uint32_t)sample <= return_level) {
        pd->return_candidate_us = now_us;
        if (pd->cfg.return_hold_us == 0u) {
            reported = finish_pulse(pd, now_us, false, out);
        } else {
            pd->state = PULSE_STATE_RETURN_HOLD;
        }
    }
    return reported;
}

static bool feed_return_hold(pulse_detector_t *pd, uint16_t sample, uint32_t now_us,
                             pulse_event_t *out) {
    const uint16_t base = baseline_u16(pd);
    const uint32_t return_level = (uint32_t)base + pd->cfg.return_thr;

    if ((uint32_t)sample > return_level) {
        // Faux retour : même impulsion, la crête continue d'être suivie.
        pd->state = PULSE_STATE_RISE;
        if ((uint32_t)sample > (uint32_t)pd->peak) {
            pd->peak = sample;
            pd->t_peak_us = now_us;
        }
        return false;
    }
    if ((uint32_t)(now_us - pd->return_candidate_us) >= pd->cfg.return_hold_us) {
        // Fin au premier échantillon du maintien, pas à l'échéance (ADR-0032 §2).
        return finish_pulse(pd, pd->return_candidate_us, false, out);
    }
    return false;
}

void pulse_detector_init(pulse_detector_t *pd, const listen_params_t *params, uint32_t now_us) {
    if (pd == NULL) {
        return;
    }
    if (params != NULL) {
        pd->cfg = *params;
    } else {
        memset(&pd->cfg, 0, sizeof(pd->cfg));
    }
    pulse_detector_reset(pd, now_us);
}

void pulse_detector_reset(pulse_detector_t *pd, uint32_t now_us) {
    if (pd == NULL) {
        return;
    }
    pd->state = PULSE_STATE_INIT;
    pd->baseline_q16 = 0u;
    pd->init_sum = 0u;
    pd->init_count = 0u;
    pd->init_end_us = now_us + ms_to_us_u32(pd->cfg.baseline_init_ms);
    pd->last_sample = 0u;
    pd->base_at_start = 0u;
    pd->peak = 0u;
    pd->trough = 0u;
    pd->trough_armed = false;
    pd->last_calm_us = now_us;
    pd->t0_us = now_us;
    pd->t_peak_us = now_us;
    pd->t_trough_us = now_us;
    pd->return_candidate_us = now_us;
    pd->last_sample_us = now_us;
    pd->rejected = 0u;
}

bool pulse_detector_feed(pulse_detector_t *pd, uint16_t sample, uint32_t now_us,
                         pulse_event_t *out) {
    bool reported = false;

    if (pd == NULL) {
        return false;
    }

    switch (pd->state) {
    case PULSE_STATE_INIT:
        reported = feed_init(pd, sample, now_us);
        break;
    case PULSE_STATE_IDLE:
        reported = feed_idle(pd, sample, now_us, out);
        break;
    case PULSE_STATE_RISE:
        reported = feed_rise(pd, sample, now_us, out);
        break;
    case PULSE_STATE_RETURN_HOLD:
        reported = feed_return_hold(pd, sample, now_us, out);
        break;
    default:
        // État impossible : on repart d'un état sûr et nommé.
        pd->state = PULSE_STATE_IDLE;
        break;
    }

    pd->last_sample = sample;
    pd->last_sample_us = now_us;
    return reported;
}

uint16_t pulse_detector_baseline(const pulse_detector_t *pd) {
    if (pd == NULL) {
        return 0u;
    }
    if (pd->state == PULSE_STATE_INIT) {
        if (pd->init_count > 0u) {
            return (uint16_t)((pd->init_sum + (pd->init_count / 2u)) / pd->init_count);
        }
        return 0u;
    }
    return baseline_u16(pd);
}
