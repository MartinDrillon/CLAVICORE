#ifndef PROFILE_SYNC_LOGIC_H
#define PROFILE_SYNC_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#define PROFILE_SYNC_MAX_ATTEMPTS 3u

typedef struct {
    uint8_t ids[PROFILE_SYNC_MAX_ATTEMPTS];
    uint8_t count;
} profile_sync_history_t;

typedef enum {
    PROFILE_ACK_IGNORE = 0,
    PROFILE_ACK_CONFIRM,
    PROFILE_ACK_RETRYABLE,
    PROFILE_ACK_TERMINAL,
    PROFILE_ACK_DIVERGENT,
} profile_ack_action_t;

typedef enum {
    PROFILE_PROBE_RECONCILE = 0,
    PROFILE_PROBE_FALLBACK,
    PROFILE_PROBE_DIVERGENT,
    PROFILE_PROBE_FAIL,
} profile_probe_action_t;

typedef enum {
    PROFILE_ACTION_WAIT = 0,
    PROFILE_ACTION_SUCCESS,
    PROFILE_ACTION_FAILURE,
    PROFILE_ACTION_RETRY,
    PROFILE_ACTION_START_PROBE,
} profile_next_action_t;

static inline bool profile_sync_result_retryable(uint8_t result) {
    // Les valeurs sont celles de can_profile_result_t (CAN.md 0x423).
    return result == 1u || result == 2u || result == 3u;
}

static inline bool profile_sync_history_contains(const profile_sync_history_t *history,
                                                 uint8_t transfer_id) {
    if (history == NULL) {
        return false;
    }
    for (uint8_t i = 0u; i < history->count; ++i) {
        if (history->ids[i] == transfer_id) {
            return true;
        }
    }
    return false;
}

static inline bool profile_sync_history_append(profile_sync_history_t *history,
                                               uint8_t transfer_id) {
    if (history == NULL || transfer_id == 0u ||
        history->count >= PROFILE_SYNC_MAX_ATTEMPTS) {
        return false;
    }
    history->ids[history->count++] = transfer_id;
    return true;
}

static inline profile_ack_action_t profile_sync_classify_ack(bool already_confirmed,
                                                             bool transfer_known,
                                                             uint8_t result,
                                                             uint32_t tag,
                                                             uint32_t expected_tag) {
    if (already_confirmed) {
        return (result == 0u && tag != expected_tag) ? PROFILE_ACK_DIVERGENT
                                                      : PROFILE_ACK_IGNORE;
    }
    if (!transfer_known) {
        return PROFILE_ACK_IGNORE;
    }
    if (result == 0u) {
        return tag == expected_tag ? PROFILE_ACK_CONFIRM : PROFILE_ACK_DIVERGENT;
    }
    return profile_sync_result_retryable(result) ? PROFILE_ACK_RETRYABLE
                                                  : PROFILE_ACK_TERMINAL;
}

static inline profile_probe_action_t profile_sync_classify_probe(bool result_ok, bool source_nvm,
                                                                 bool source_defaults,
                                                                 uint32_t tag,
                                                                 uint32_t expected_tag) {
    if (result_ok && source_nvm && tag == expected_tag) {
        return PROFILE_PROBE_RECONCILE;
    }
    if (result_ok && tag != expected_tag) {
        return PROFILE_PROBE_DIVERGENT;
    }
    if (source_defaults) {
        return PROFILE_PROBE_FALLBACK;
    }
    return PROFILE_PROBE_FAIL;
}

static inline bool profile_sync_all_confirmed(uint8_t expected, uint8_t direct,
                                              uint8_t reconciled) {
    return (uint8_t)(direct + reconciled) >= expected;
}

static inline profile_next_action_t profile_sync_next_action(bool active, bool failed,
                                                             bool probe_pending,
                                                             bool probe_finished,
                                                             bool all_confirmed,
                                                             uint8_t attempt,
                                                             bool deadline_reached) {
    if (!active) {
        return PROFILE_ACTION_WAIT;
    }
    if (probe_pending) {
        if (!failed && all_confirmed) {
            return PROFILE_ACTION_SUCCESS;
        }
        return probe_finished ? PROFILE_ACTION_FAILURE : PROFILE_ACTION_WAIT;
    }
    if (failed) {
        return PROFILE_ACTION_FAILURE;
    }
    if (all_confirmed) {
        return PROFILE_ACTION_SUCCESS;
    }
    if (!deadline_reached) {
        return PROFILE_ACTION_WAIT;
    }
    return attempt < PROFILE_SYNC_MAX_ATTEMPTS ? PROFILE_ACTION_RETRY
                                               : PROFILE_ACTION_START_PROBE;
}

#endif
