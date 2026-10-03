// Bring-up isolé canal 0 (Phase 2a de Docs/specs/Roadmap.md) : validation
// électrique basique (sens du courant, PWM), avant toute machine à états
// pull/hold/release et avant tout multi-canal ou CAN — hors périmètre ici.
#include <stdbool.h>
#include <stdint.h>

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "config.h"

#define PWM_WRAP 9999u
#define PWM_DUTY_PERCENT 30u
#define TEST_BUTTON_DEBOUNCE_DELAY_MS 30u
#define MAIN_LOOP_POLL_DELAY_MS 5u

int main(void) {
    stdio_init_all();

    gpio_init(MOTEUR_GLOBAL_SLEEP_GPIO);
    gpio_set_dir(MOTEUR_GLOBAL_SLEEP_GPIO, GPIO_OUT);
    gpio_put(MOTEUR_GLOBAL_SLEEP_GPIO, true);  // nSLEEP actif haut : réveil du driver avant tout usage

    gpio_init(MOTEUR_DIR_CHANNEL0_GPIO);
    gpio_set_dir(MOTEUR_DIR_CHANNEL0_GPIO, GPIO_OUT);
    gpio_put(MOTEUR_DIR_CHANNEL0_GPIO, false);  // LOW = sens PULL, fixe pour ce test isolé

    gpio_init(MOTEUR_FAULT_CHANNEL0_GPIO);
    gpio_set_dir(MOTEUR_FAULT_CHANNEL0_GPIO, GPIO_IN);
    gpio_pull_up(MOTEUR_FAULT_CHANNEL0_GPIO);

    gpio_init(MOTEUR_TEST_BUTTON_GPIO);
    gpio_set_dir(MOTEUR_TEST_BUTTON_GPIO, GPIO_IN);
    gpio_pull_up(MOTEUR_TEST_BUTTON_GPIO);

    gpio_set_function(MOTEUR_PWM_CHANNEL0_GPIO, GPIO_FUNC_PWM);
    const uint pwm_slice = pwm_gpio_to_slice_num(MOTEUR_PWM_CHANNEL0_GPIO);
    const uint pwm_channel = pwm_gpio_to_channel(MOTEUR_PWM_CHANNEL0_GPIO);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, PWM_WRAP);
    pwm_init(pwm_slice, &config, false);
    pwm_set_chan_level(pwm_slice, pwm_channel, (PWM_WRAP * PWM_DUTY_PERCENT) / 100u);

    bool pwm_running = false;
    bool previous_button_level = true;  // pull-up : bouton relâché = niveau haut

    while (true) {
        // NFAULT actif bas : le driver signale un défaut, on coupe le PWM sans attendre
        // plutôt que de laisser le solénoïde activé hors de sa fenêtre de sécurité.
        if (!gpio_get(MOTEUR_FAULT_CHANNEL0_GPIO)) {
            pwm_set_enabled(pwm_slice, false);
            pwm_running = false;
        }

        const bool button_level = gpio_get(MOTEUR_TEST_BUTTON_GPIO);
        if (button_level != previous_button_level) {
            sleep_ms(TEST_BUTTON_DEBOUNCE_DELAY_MS);  // anti-rebond bloquant simple, suffisant pour ce test isolé
            const bool debounced_level = gpio_get(MOTEUR_TEST_BUTTON_GPIO);
            if (debounced_level != previous_button_level && !debounced_level) {
                // Front descendant confirmé (bouton actif bas) : bascule marche/arrêt du PWM.
                pwm_running = !pwm_running;
                pwm_set_enabled(pwm_slice, pwm_running);
            }
            previous_button_level = debounced_level;
        }

        sleep_ms(MAIN_LOOP_POLL_DELAY_MS);
    }
}
