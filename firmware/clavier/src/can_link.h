#ifndef CAN_LINK_H
#define CAN_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "diag_log.h"

typedef enum {
    CAN_LINK_REQUEST_SENT = 0,
    CAN_LINK_REQUEST_BUSY,
    CAN_LINK_REQUEST_TX_FAIL,
} can_link_request_result_t;

typedef struct {
    uint32_t rx_overrun;
    uint32_t rx_ext_or_rtr;
    uint32_t rx_bad_dlc;
    uint32_t rx_bad_value;
    uint32_t rx_unknown_id;
    uint32_t rx_ignored;
    uint32_t can_errors;
    uint32_t tx_fail;
    uint32_t tx_unacked_refused;
    uint32_t reasm_full;
    uint32_t events_dropped;
    uint32_t reports_emitted;
} can_link_stats_t;

void can_link_init(void);
void can_link_poll(void);
can_link_request_result_t can_link_request_uid(void);
void can_link_set_note_activity(bool any_note_active, uint32_t last_note_event_ms);
can_link_stats_t can_link_stats(void);

#endif
