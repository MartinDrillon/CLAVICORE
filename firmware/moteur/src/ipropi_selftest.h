#ifndef MOTEUR_IPROPI_SELFTEST_H
#define MOTEUR_IPROPI_SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

#include "can_protocol.h"
#include "solenoid_driver.h"

#define IPROPI_SELFTEST_BUTTON_HOLD_US 3000000u
#define IPROPI_SELFTEST_MUX_SETTLE_US 150u
#define IPROPI_SELFTEST_ADC_SAMPLE_DELAY_US 1800u
#define IPROPI_SELFTEST_RAW_SAMPLES 8u
#define IPROPI_SELFTEST_THRESHOLD_DELTA_ADC 400u

typedef struct {
    bool report_ready;
    can_announce_status_t report;
    uint8_t raw_count;
    can_ipropi_raw_t raw_reports[CAN_IPROPI_CHANNEL_COUNT];
} ipropi_selftest_result_t;

void ipropi_selftest_init(solenoid_driver_t *drivers, uint8_t driver_count, uint16_t uid_tag);
void ipropi_selftest_tick(uint32_t now_us);
bool ipropi_selftest_start(uint8_t request_id);
bool ipropi_selftest_start_request(const can_ipropi_selftest_request_t *request);
bool ipropi_selftest_take_result(ipropi_selftest_result_t *out);
bool ipropi_selftest_is_running(void);
bool ipropi_selftest_button_update(bool pressed, uint32_t now_us);

#endif  // MOTEUR_IPROPI_SELFTEST_H
