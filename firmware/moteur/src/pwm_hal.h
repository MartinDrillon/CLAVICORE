// Abstraction au-dessus de hardware_pwm : un « canal » = un GPIO sur une slice.
// Rôle logique ADR-0001 : pwm_channel (pin1 EN/IN1 du DRV8874).
#ifndef MOTEUR_PWM_HAL_H
#define MOTEUR_PWM_HAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned int gpio;
    unsigned int slice;
    unsigned int pwm_channel;  // voie A/B de la slice
    uint16_t wrap;
    uint8_t clkdiv_int;
    bool enabled;
} pwm_hal_t;

// Calcule diviseur/wrap depuis clock_get_hz(clk_sys) pour approcher freq_hz.
// Ne démarre PAS la sortie. Retourne false si fréquence irréalisable.
bool pwm_hal_init(pwm_hal_t *hal, unsigned int gpio, uint32_t freq_hz);

// Fréquence réellement obtenue (Hz) après arrondi du diviseur/wrap.
uint32_t pwm_hal_actual_freq_hz(const pwm_hal_t *hal);

// Duty en % (écrêté à 100). Valide que le canal soit actif ou non.
void pwm_hal_set_duty_percent(pwm_hal_t *hal, uint32_t percent);

// Connecte le GPIO au PWM et démarre la slice (duty courant conservé).
void pwm_hal_enable(pwm_hal_t *hal);

// Duty 0, slice arrêtée, GPIO forcé en sortie niveau bas (pas de niveau flottant).
void pwm_hal_disable(pwm_hal_t *hal);

// Duty 0, slice arrêtée, GPIO en entrée haute impédance sans pull.
void pwm_hal_release_hiz(pwm_hal_t *hal);

#endif  // MOTEUR_PWM_HAL_H
