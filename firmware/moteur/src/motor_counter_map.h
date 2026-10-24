#ifndef MOTOR_COUNTER_MAP_H
#define MOTOR_COUNTER_MAP_H

#include <stdint.h>

#include "can_protocol.h"
#include "solenoid_driver.h"

// ADR-0024 : un refus pilote non OK correspond à exactement un compteur de
// cause. SOLENOID_OK ne produit aucun compteur.
static inline can_card_counter_t solenoid_reject_counter_id(solenoid_result_t result) {
    switch (result) {
    case SOLENOID_OK: return CAN_CARD_COUNTER_COUNT;
    case SOLENOID_ERR_SLEEP: return CAN_CARD_COUNTER_REJECT_SLEEP;
    case SOLENOID_ERR_REST: return CAN_CARD_COUNTER_REJECT_REST;
    case SOLENOID_ERR_BUSY: return CAN_CARD_COUNTER_REJECT_BUSY;
    case SOLENOID_ERR_FAULT: return CAN_CARD_COUNTER_REJECT_FAULT;
    case SOLENOID_ERR_PARAM:
    case SOLENOID_ERR_PWM:
    case SOLENOID_ERR_FAULT_PERSISTS:
    default: return CAN_CARD_COUNTER_REJECT_OTHER;
    }
}

// ADR-0024 : la trame 0x425 doit refléter le compteur ISR, jamais la copie
// potentiellement périmée de `stats`.
static inline uint32_t motor_counter_rx_overrun(uint32_t isr_value, uint32_t stale_value) {
    (void)stale_value;
    return isr_value;
}

#endif
