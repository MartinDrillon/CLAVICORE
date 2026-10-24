#ifndef STATS_LINE_FORMAT_H
#define STATS_LINE_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "can_protocol.h"
#include "card_counter_names.h"

static inline bool stats_field_required(can_card_counter_t id) {
    return id == CAN_CARD_COUNTER_CAN_ERRORS || id == CAN_CARD_COUNTER_TX_FAIL ||
           id == CAN_CARD_COUNTER_RX_OVERRUN || id == CAN_CARD_COUNTER_REJECTED ||
           id == CAN_CARD_COUNTER_COMMANDS_OK;
}

static inline size_t stats_append_field(char *out, size_t capacity, size_t used,
                                        const char *name, uint32_t value) {
    const int written = snprintf(out + used, capacity - used, " %s=%lu", name,
                                 (unsigned long)value);

    if (written < 0 || (size_t)written >= capacity - used) {
        return 0u;
    }
    return used + (size_t)written;
}

// Rend une ligne `[CARD] pos=N stats …`. Retourne false si le tampon est
// insuffisant ; `*incomplete` signale un champ obligatoire sans réponse.
static inline bool stats_format_card_line(char *out, size_t capacity, uint8_t position,
                                          const bool *seen, const uint32_t *values,
                                          bool ext_unavailable, bool *incomplete) {
    int written;
    size_t used;

    if (out == NULL || seen == NULL || values == NULL || incomplete == NULL ||
        capacity == 0u) {
        return false;
    }
    written = snprintf(out, capacity, "[CARD] pos=%u stats", (unsigned)position);
    if (written < 0 || (size_t)written >= capacity) {
        return false;
    }
    used = (size_t)written;
    *incomplete = false;
    for (uint8_t id = 0u; id < (uint8_t)CAN_CARD_COUNTER_COUNT; ++id) {
        const can_card_counter_t counter = (can_card_counter_t)id;

        if (!seen[id]) {
            if (stats_field_required(counter)) {
                const int missing = snprintf(out + used, capacity - used, " %s=?",
                                             clavier_card_counter_name(counter));
                if (missing < 0 || (size_t)missing >= capacity - used) {
                    return false;
                }
                used += (size_t)missing;
                *incomplete = true;
            }
            continue;
        }
        if (!stats_field_required(counter) && values[id] == 0u) {
            continue;
        }
        used = stats_append_field(out, capacity, used, clavier_card_counter_name(counter),
                                  values[id]);
        if (used == 0u) {
            return false;
        }
    }
    if (ext_unavailable) {
        const int suffix = snprintf(out + used, capacity - used, " ext=n/a");
        if (suffix < 0 || (size_t)suffix >= capacity - used) {
            return false;
        }
    }
    return true;
}

static inline bool stats_format_summary_line(char *out, size_t capacity, uint32_t note_sent,
                                             uint32_t commands_ok, bool incomplete) {
    int written;

    if (incomplete) {
        written = snprintf(out, capacity, "[STATS] note_sent=%lu cartes_ok=n/a ecart=n/a",
                           (unsigned long)note_sent);
    } else {
        written = snprintf(out, capacity, "[STATS] note_sent=%lu cartes_ok=%lu ecart=%ld",
                           (unsigned long)note_sent, (unsigned long)commands_ok,
                           (long)((int32_t)note_sent - (int32_t)commands_ok));
    }
    return written >= 0 && (size_t)written < capacity;
}

#endif
