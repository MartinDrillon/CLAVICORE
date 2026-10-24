#ifndef SOLENOID_WAKE_H
#define SOLENOID_WAKE_H

#include <stdbool.h>
#include <stdint.h>

// Verrou one-shot du réveil global (ADR-0024). Isolé du pilote matériel pour
// être testable sur hôte avec une horloge 32 bits injectée.
typedef struct {
    bool latched;
    uint32_t deadline_us;
} solenoid_wake_state_t;

static inline bool solenoid_wake_ready(solenoid_wake_state_t *state, bool awake,
                                       uint32_t now_us) {
    if (!awake) {
        return false;
    }
    if (!state->latched && (int32_t)(now_us - state->deadline_us) >= 0) {
        state->latched = true;
    }
    return state->latched;
}

#endif
