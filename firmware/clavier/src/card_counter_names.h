#ifndef CARD_COUNTER_NAMES_H
#define CARD_COUNTER_NAMES_H

#include "can_protocol.h"

static inline const char *clavier_card_counter_name(can_card_counter_t counter) {
    switch (counter) {
    case CAN_CARD_COUNTER_CAN_ERRORS: return "can_errors";
    case CAN_CARD_COUNTER_TX_FAIL: return "tx_fail";
    case CAN_CARD_COUNTER_RX_OVERRUN: return "rx_overrun";
    case CAN_CARD_COUNTER_SELFTEST_BLOCKED: return "selftest_blocked";
    case CAN_CARD_COUNTER_IGNORED_TAG: return "ignored_tag";
    case CAN_CARD_COUNTER_BAD_CHANNEL: return "bad_channel";
    case CAN_CARD_COUNTER_PROFILE_STATUS_LOST: return "profile_status_lost";
    case CAN_CARD_COUNTER_PROFILE_COMMITS_REJECTED: return "profile_commits_rejected";
    case CAN_CARD_COUNTER_PROFILE_COMMITS_OK: return "profile_commits_ok";
    case CAN_CARD_COUNTER_RX_IGNORED: return "rx_ignored";
    case CAN_CARD_COUNTER_UNOCCUPIED: return "unoccupied";
    case CAN_CARD_COUNTER_REJECTED: return "rejected";
    case CAN_CARD_COUNTER_COMMANDS_OK: return "commands_ok";
    case CAN_CARD_COUNTER_REJECT_SLEEP: return "solenoid_reject_sleep";
    case CAN_CARD_COUNTER_REJECT_REST: return "solenoid_reject_rest";
    case CAN_CARD_COUNTER_REJECT_BUSY: return "solenoid_reject_busy";
    case CAN_CARD_COUNTER_REJECT_FAULT: return "solenoid_reject_fault";
    case CAN_CARD_COUNTER_REJECT_OTHER: return "solenoid_reject_other";
    default: return NULL;
    }
}

#endif
