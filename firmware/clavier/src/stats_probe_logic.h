#ifndef STATS_PROBE_LOGIC_H
#define STATS_PROBE_LOGIC_H

#include <stdbool.h>

// ADR-0024/0025 : une carte qui a répondu à au moins un compteur ancien mais
// jamais à un ID 13–17 est une carte ancienne, pas une carte morte.
static inline bool stats_card_ext_unavailable(bool any_response, bool ext_seen) {
    return any_response && !ext_seen;
}

// Une carte est muette si elle n'a répondu à aucun compteur du balayage.
static inline bool stats_card_silent(bool any_response) {
    return !any_response;
}

#endif
