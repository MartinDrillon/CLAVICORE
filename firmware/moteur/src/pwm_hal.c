#include "pwm_hal.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#define PWM_COUNTER_MAX 65536u  // wrap max 65535 -> 65536 pas par période
#define PWM_CLKDIV_INT_MAX 255u

bool pwm_hal_init(pwm_hal_t *hal, unsigned int gpio, uint32_t freq_hz) {
    if (hal == NULL || freq_hz == 0u) {
        return false;
    }
    // Hypothèse : le calcul suit la fréquence réelle de clk_sys (150 MHz attendu),
    // jamais une valeur supposée.
    const uint64_t clk = clock_get_hz(clk_sys);
    uint64_t div = (clk / freq_hz + (PWM_COUNTER_MAX - 1u)) / PWM_COUNTER_MAX;
    if (div < 1u) {
        div = 1u;
    }
    if (div > PWM_CLKDIV_INT_MAX) {
        return false;
    }
    const uint64_t denom = (uint64_t)freq_hz * div;
    const uint64_t steps = (clk + denom / 2u) / denom;  // pas par période, arrondi
    if (steps < 2u || steps > PWM_COUNTER_MAX) {
        return false;
    }

    hal->gpio = gpio;
    hal->slice = pwm_gpio_to_slice_num(gpio);
    hal->pwm_channel = pwm_gpio_to_channel(gpio);
    hal->wrap = (uint16_t)(steps - 1u);
    hal->clkdiv_int = (uint8_t)div;
    hal->enabled = false;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, hal->clkdiv_int);
    pwm_config_set_wrap(&cfg, hal->wrap);
    pwm_init(hal->slice, &cfg, false);
    pwm_set_chan_level(hal->slice, hal->pwm_channel, 0u);
    return true;
}

uint32_t pwm_hal_actual_freq_hz(const pwm_hal_t *hal) {
    return (uint32_t)(clock_get_hz(clk_sys) / ((uint32_t)hal->clkdiv_int * ((uint32_t)hal->wrap + 1u)));
}

void pwm_hal_set_duty_percent(pwm_hal_t *hal, uint32_t percent) {
    if (percent > 100u) {
        percent = 100u;
    }
    const uint32_t level = (((uint32_t)hal->wrap + 1u) * percent) / 100u;  // 100 % -> > wrap : toujours haut
    pwm_set_chan_level(hal->slice, hal->pwm_channel, (uint16_t)(level > 0xFFFFu ? 0xFFFFu : level));
}

void pwm_hal_enable(pwm_hal_t *hal) {
    gpio_set_function(hal->gpio, GPIO_FUNC_PWM);
    pwm_set_enabled(hal->slice, true);
    hal->enabled = true;
}

void pwm_hal_disable(pwm_hal_t *hal) {
    pwm_set_chan_level(hal->slice, hal->pwm_channel, 0u);
    // Une slice arrêtée garde le dernier niveau de sortie : on reprend la main en SIO bas.
    gpio_set_function(hal->gpio, GPIO_FUNC_SIO);
    gpio_put(hal->gpio, false);
    gpio_set_dir(hal->gpio, GPIO_OUT);
    pwm_set_enabled(hal->slice, false);
    hal->enabled = false;
}

void pwm_hal_release_hiz(pwm_hal_t *hal) {
    pwm_set_chan_level(hal->slice, hal->pwm_channel, 0u);
    pwm_set_enabled(hal->slice, false);
    gpio_set_function(hal->gpio, GPIO_FUNC_SIO);
    gpio_set_dir(hal->gpio, GPIO_IN);
    gpio_disable_pulls(hal->gpio);
    hal->enabled = false;
}
