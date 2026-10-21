#include "ipropi_ohm.h"

#include "can_protocol.h"

#define ADC_FULL_SCALE 4095u
#define MICRO_PER_UNIT 1000000u

static uint16_t clamp_u16(uint64_t value) {
    return value > (uint64_t)CAN_IPROPI_VALUE_MAX ? (uint16_t)CAN_IPROPI_VALUE_MAX
                                                  : (uint16_t)value;
}

ipropi_ohm_result_t ipropi_ohm_compute(const ipropi_ohm_config_t *config, uint16_t probe_avg,
                                       uint16_t idle_avg, uint16_t min_delta_adc) {
    ipropi_ohm_result_t result = {
        .valid = false,
        .ohm_milliohm = CAN_IPROPI_VALUE_INVALID,
        .i100_ma = CAN_IPROPI_VALUE_INVALID,
    };
    uint32_t delta;
    uint64_t numerator;
    uint64_t denominator;
    uint64_t quotient;
    uint64_t ohm_milliohm;
    uint64_t ohm_with_rdson_milliohm;
    uint64_t i100_ma;

    if (config == NULL || config->vm_millivolt == 0u || config->vref_millivolt == 0u ||
        config->ipropi_ua_per_a == 0u || config->ipropi_resistor_ohm == 0u ||
        config->duty_permil == 0u) {
        return result;
    }

    // Décalage à courant nul retiré : même grandeur que le critère d'occupation.
    delta = (probe_avg > idle_avg) ? (uint32_t)(probe_avg - idle_avg) : 0u;
    if (delta < (uint32_t)min_delta_adc) {
        return result;  // aucun courant détecté : canal vide, pas d'estimation
    }

    // R_mOhm = duty_permil x VM_mV x 4095 x K / (delta x VREF_mV) - Rds_mOhm,
    // avec K = A_IPROPI x R_IPROPI / 1e6 (la division par 1e6 est portée par le
    // dénominateur, ce qui garde tout le calcul entier en 64 bits).
    numerator = (uint64_t)config->duty_permil * (uint64_t)config->vm_millivolt *
                (uint64_t)ADC_FULL_SCALE * (uint64_t)config->ipropi_ua_per_a *
                (uint64_t)config->ipropi_resistor_ohm;
    denominator = (uint64_t)delta * (uint64_t)config->vref_millivolt *
                  (uint64_t)MICRO_PER_UNIT;
    quotient = numerator / denominator;

    // ADC saturé ou bobine quasi court-circuit : borne basse à 0, jamais de
    // valeur négative transmise.
    ohm_milliohm = (quotient > (uint64_t)config->rdson_milliohm)
                       ? (quotient - (uint64_t)config->rdson_milliohm)
                       : 0u;
    ohm_with_rdson_milliohm = ohm_milliohm + (uint64_t)config->rdson_milliohm;
    if (ohm_with_rdson_milliohm == 0u) {
        return result;
    }

    // Courant à 100 % de duty, régime établi : VM / (R + Rds).
    i100_ma = ((uint64_t)config->vm_millivolt * 1000u) / ohm_with_rdson_milliohm;

    result.valid = true;
    result.ohm_milliohm = clamp_u16(ohm_milliohm);
    result.i100_ma = clamp_u16(i100_ma);
    return result;
}
