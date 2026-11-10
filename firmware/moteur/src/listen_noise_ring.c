#include "listen_noise_ring.h"

#include <stddef.h>

#define LISTEN_NOISE_MASK (LISTEN_NOISE_SLICES - 1u)
#define LISTEN_NOISE_INIT ((0xFFFFu << 16) | 0x0000u)

_Static_assert((LISTEN_NOISE_SLICES & LISTEN_NOISE_MASK) == 0u,
               "LISTEN_NOISE_SLICES doit etre une puissance de 2");

// Accès atomique 32 bits au tableau. Le cast en volatile empêche le compilateur
// de mettre en cache une valeur entre deux lectures inter-cœurs.
static volatile uint32_t *slice_at(const listen_noise_ring_t *ring, uint32_t index) {
    return (volatile uint32_t *)&ring->slice[index & LISTEN_NOISE_MASK];
}

void listen_noise_ring_reset(listen_noise_ring_t *ring, uint32_t now_ms) {
    if (ring == NULL) {
        return;
    }
    for (uint32_t i = 0u; i < LISTEN_NOISE_SLICES; ++i) {
        ring->slice[i] = LISTEN_NOISE_EMPTY;
    }
    // Le premier record à `now_ms` doit ouvrir une nouvelle tranche.
    ring->last_ms = now_ms - 1u;
}

void listen_noise_ring_record(listen_noise_ring_t *ring, uint32_t now_ms, uint16_t sample) {
    if (ring == NULL) {
        return;
    }

    if (now_ms != ring->last_ms) {
        uint32_t steps = now_ms - ring->last_ms;
        const uint32_t from = ring->last_ms;

        if (steps > LISTEN_NOISE_SLICES) {
            steps = LISTEN_NOISE_SLICES;
        }
        // Tranches sautées : vides, jamais remplies avec de vieilles valeurs.
        for (uint32_t i = 1u; i < steps; ++i) {
            *slice_at(ring, from + i) = LISTEN_NOISE_EMPTY;
        }
        *slice_at(ring, now_ms) = LISTEN_NOISE_INIT;
        ring->last_ms = now_ms;
    }

    volatile uint32_t *slot = slice_at(ring, now_ms);
    const uint32_t packed = *slot;
    uint16_t min = (uint16_t)(packed >> 16);
    uint16_t max = (uint16_t)(packed & 0xFFFFu);

    if (packed == LISTEN_NOISE_EMPTY || (min == 0xFFFFu && max == 0u)) {
        min = sample;
        max = sample;
    } else {
        if (sample < min) {
            min = sample;
        }
        if (sample > max) {
            max = sample;
        }
    }
    *slot = ((uint32_t)min << 16) | (uint32_t)max;
}

bool listen_noise_ring_query(const listen_noise_ring_t *ring, uint32_t now_ms,
                             uint16_t lookback_ms, uint16_t *min, uint16_t *max,
                             bool *truncated, uint16_t *slices_valid) {
    if (ring == NULL || min == NULL || max == NULL || truncated == NULL ||
        slices_valid == NULL || lookback_ms == 0u) {
        return false;
    }

    *truncated = lookback_ms > LISTEN_NOISE_SLICES;
    const uint32_t count_requested =
        (lookback_ms > LISTEN_NOISE_SLICES) ? LISTEN_NOISE_SLICES : lookback_ms;

    // Producteur arrêté depuis plus d'un tour : aucune donnée fraîche.
    // La différence est signée : le producteur peut avoir avancé `last_ms`
    // d'une milliseconde entre la lecture de `now_ms` et cet appel (âge -1),
    // ce qui reste une donnée fraîche.
    if ((int32_t)(now_ms - ring->last_ms) > (int32_t)LISTEN_NOISE_SLICES) {
        *min = 0u;
        *max = 0u;
        *slices_valid = 0u;
        return false;
    }

    // Ancre figée : le producteur peut avancer `last_ms` pendant le balayage.
    const uint32_t anchor_ms = ring->last_ms;
    uint16_t value_min = 0u;
    uint16_t value_max = 0u;
    uint32_t valid = 0u;
    for (uint32_t i = 0u; i < count_requested; ++i) {
        const uint32_t packed = *slice_at(ring, anchor_ms - i);

        if (packed == LISTEN_NOISE_EMPTY) {
            continue;
        }
        const uint16_t slice_min = (uint16_t)(packed >> 16);
        const uint16_t slice_max = (uint16_t)(packed & 0xFFFFu);

        if (valid == 0u) {
            value_min = slice_min;
            value_max = slice_max;
        } else {
            if (slice_min < value_min) {
                value_min = slice_min;
            }
            if (slice_max > value_max) {
                value_max = slice_max;
            }
        }
        ++valid;
    }

    *min = value_min;
    *max = value_max;
    *slices_valid = (uint16_t)((valid > 0xFFFFu) ? 0xFFFFu : valid);
    return valid > 0u;
}
