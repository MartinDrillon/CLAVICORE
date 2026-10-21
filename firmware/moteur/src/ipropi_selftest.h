#ifndef MOTEUR_IPROPI_SELFTEST_H
#define MOTEUR_IPROPI_SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

#include "can_protocol.h"
#include "solenoid_driver.h"

#define IPROPI_SELFTEST_BUTTON_HOLD_US 3000000u
#define IPROPI_SELFTEST_MUX_SETTLE_US 150u
// Fenêtre d'échantillonnage calée sur la FIN de l'impulsion PROBE (ADR-0009 §5b) :
//   - `..._TAIL_MARGIN_US` : garde entre la fin de la fenêtre et la coupure de
//     l'impulsion par le pilote (absorbe la latence de la boucle principale) ;
//   - `..._SAMPLE_SPACING_US` : espacement entre échantillons, pour couvrir au
//     moins une période PWM (20 kHz -> 50 µs) et moyenner la ondulation.
// La fenêtre et la durée PROBE appliquée viennent du pilote
// (solenoid_driver_probe_duration_us), jamais d'une recoloration du profil.
#define IPROPI_SELFTEST_ADC_TAIL_MARGIN_US 500u
#define IPROPI_SELFTEST_ADC_SAMPLE_SPACING_US 8u
#define IPROPI_SELFTEST_ADC_SAMPLE_TIME_US 2u
#define IPROPI_SELFTEST_ADC_SAMPLE_WINDOW_US                             \
    ((uint32_t)IPROPI_SELFTEST_ADC_SAMPLE_SPACING_US *                   \
         ((uint32_t)IPROPI_SELFTEST_RAW_SAMPLES - 1u) +                  \
     (uint32_t)IPROPI_SELFTEST_ADC_SAMPLE_TIME_US)
#define IPROPI_SELFTEST_RAW_SAMPLES 8u
#define IPROPI_SELFTEST_THRESHOLD_DELTA_ADC 400u

typedef struct {
    bool report_ready;
    can_announce_status_t report;
    uint8_t raw_count;
    can_ipropi_raw_t raw_reports[CAN_IPROPI_CHANNEL_COUNT];
    // Diagnostic ADR-0009 §5/§6 : résistance et courant estimés par canal.
    // Émis après les trames IPROPI_RAW, uniquement en mode diagnostic.
    uint8_t ohm_count;
    can_ipropi_ohm_t ohm_reports[CAN_IPROPI_CHANNEL_COUNT];
} ipropi_selftest_result_t;

void ipropi_selftest_init(solenoid_driver_t *drivers, uint8_t driver_count, uint16_t uid_tag);
void ipropi_selftest_tick(uint32_t now_us);
bool ipropi_selftest_start(uint8_t request_id);
bool ipropi_selftest_start_request(const can_ipropi_selftest_request_t *request);
bool ipropi_selftest_take_result(ipropi_selftest_result_t *out);
bool ipropi_selftest_is_running(void);
bool ipropi_selftest_button_update(bool pressed, uint32_t now_us);

#endif  // MOTEUR_IPROPI_SELFTEST_H
