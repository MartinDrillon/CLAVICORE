#ifndef MOTEUR_IPROPI_SELFTEST_H
#define MOTEUR_IPROPI_SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

#include "can_protocol.h"
#include "solenoid_driver.h"

#define IPROPI_SELFTEST_BUTTON_HOLD_US 3000000u
#define IPROPI_SELFTEST_MUX_SETTLE_US 150u
#define IPROPI_SELFTEST_ADC_SAMPLE_DELAY_US 1800u
#define IPROPI_SELFTEST_THRESHOLD_ADC_DEFAULT 32u
#define IPROPI_SELFTEST_THRESHOLD_ADC IPROPI_SELFTEST_THRESHOLD_ADC_DEFAULT

typedef struct {
    bool report_ready;
    can_announce_status_t report;
} ipropi_selftest_result_t;

void ipropi_selftest_init(solenoid_driver_t *drivers, uint8_t driver_count, uint16_t uid_tag);
void ipropi_selftest_tick(uint32_t now_us);
bool ipropi_selftest_start(uint8_t request_id);
bool ipropi_selftest_take_result(ipropi_selftest_result_t *out);
bool ipropi_selftest_is_running(void);
bool ipropi_selftest_button_update(bool pressed, uint32_t now_us);

#endif  // MOTEUR_IPROPI_SELFTEST_H
