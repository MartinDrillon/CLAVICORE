// Estimation de la résistance DC d'une bobine et du courant qu'elle tirerait à
// 100 % de duty (ADR-0009 §5c, §5d). Calcul PUR : aucune dépendance SDK, donc
// testable sur PC (tests/host/).
//
// Formules (ADR-0009 §5c/§5d, docs/specs/CAN.md §IPROPI_OHM) :
//   K (V/A)   = A_IPROPI (µA/A) x R_IPROPI (ohm) / 1e6
//   I (A)     = delta / 4095 x VREF_V / K
//   R (mOhm)  = duty x VM_V / I - Rds_mOhm
//             = duty_permil x VM_mV x 4095 x K / (delta x VREF_mV) - Rds_mOhm
//   I100 (mA) = VM_mV x 1000 / (R_mOhm + Rds_mOhm)
// avec `delta = max(probe_avg - idle_avg, 0)`.
//
// Portée (ADR-0009) : valeur **indicative**, valable si le courant est établi
// dans la fenêtre de mesure. Jamais un seuil, jamais une limite de courant.
#ifndef MOTEUR_IPROPI_OHM_H
#define MOTEUR_IPROPI_OHM_H

#include <stdbool.h>
#include <stdint.h>

// Constantes électriques du profil Moteur (solenoid_profile.h) et duty PROBE
// réellement appliqué par le pilote (après écrêtage).
typedef struct {
    uint32_t vm_millivolt;         // SOLENOID_VM_MILLIVOLT
    uint32_t vref_millivolt;       // SOLENOID_ADC_VREF_MILLIVOLT
    uint32_t ipropi_ua_per_a;      // SOLENOID_IPROPI_UA_PER_A (A_IPROPI)
    uint32_t ipropi_resistor_ohm;  // SOLENOID_IPROPI_RESISTOR_OHM (R_IPROPI)
    uint32_t rdson_milliohm;       // SOLENOID_RDSON_MILLIOHM
    uint32_t duty_permil;          // duty PROBE appliqué, en pour mille
} ipropi_ohm_config_t;

typedef struct {
    bool valid;
    uint16_t ohm_milliohm;  // CAN_IPROPI_VALUE_INVALID si !valid
    uint16_t i100_ma;       // CAN_IPROPI_VALUE_INVALID si !valid
} ipropi_ohm_result_t;

// `min_delta_adc` : seuil d'occupation (IPROPI_SELFTEST_THRESHOLD_DELTA_ADC).
// En dessous, aucun courant n'est détecté : résultat invalide, jamais 0 ohm.
ipropi_ohm_result_t ipropi_ohm_compute(const ipropi_ohm_config_t *config, uint16_t probe_avg,
                                       uint16_t idle_avg, uint16_t min_delta_adc);

#endif  // MOTEUR_IPROPI_OHM_H
