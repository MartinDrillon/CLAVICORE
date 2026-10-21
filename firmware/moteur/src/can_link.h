#ifndef MOTEUR_CAN_LINK_H
#define MOTEUR_CAN_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "board_identity.h"
#include "can_protocol.h"
#include "solenoid_driver.h"

typedef struct {
    uint32_t rx_overrun;
    uint32_t rx_ext_or_rtr;
    uint32_t rx_bad_dlc;
    uint32_t rx_bad_value;
    uint32_t rx_unknown_id;
    uint32_t rx_ignored;
    uint32_t can_errors;
    uint32_t tx_fail;
    uint32_t announces_sent;
    uint32_t requests_served;
    uint32_t triggers_sent;
    uint32_t raw_reports_sent;
    uint32_t raw_reports_deferred;
    uint32_t ohm_reports_sent;
    uint32_t ohm_reports_deferred;
    uint32_t solenoid_commands_ok;
    uint32_t solenoid_ignored_tag;
    uint32_t solenoid_bad_channel;
    uint32_t solenoid_unoccupied;
    uint32_t solenoid_rejected;
    uint32_t solenoid_selftest_blocked;
} can_link_stats_t;

void can_link_init(const uint8_t uid[BOARD_IDENTITY_UID_SIZE], solenoid_driver_t *drivers,
                   uint8_t driver_count, uint32_t now_us);
void can_link_poll(uint32_t now_us);
bool can_link_send_selftest_trigger(void);
can_link_stats_t can_link_stats(void);

#endif
