// Tests du pilote de réveil global (ADR-0024), avec GPIO/PWM simulés.
// `solenoid_driver.c` est inclus pour accéder aux échéances et au tick réels.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../firmware/moteur/src/solenoid_driver.c"

static int g_checks;
static int g_failures;
static bool g_nsleep_high;
static bool g_nfault_high = true;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("ECHEC %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                    \
    } while (0)

static const solenoid_profile_t g_profile = {
    .pwm_freq_hz = 20000u,
    .impact_duration_us = 800u,
    .impact_duty_pct = 100u,
    .pull_duration_us = 15000u,
    .pull_duty_pct = 50u,
    .hold_duty_pct = 25u,
    .hold_duty_max_pct = 25u,
    .hold_max_ms = 20000u,
    .release_duration_us = 5000u,
    .release_duty_pct = 50u,
    .probe_duration_us = 5000u,
    .probe_duty_pct = 20u,
    .probe_duty_max_pct = 20u,
    .maint_duty_pct = 50u,
    .maint_duty_max_pct = 100u,
    .maint_max_ms = 20000u,
    .maint_min_rest_ms = 2000u,
    .min_rest_ms = 1u,
    .dir_safety_delay_us = 200u,
    .wake_delay_us = 2000u,
    .dir_level_pull = 0u,
    .dir_level_release = 1u,
    .dir_level_maint = 1u,
    .duty_max_pct = 100u,
};

const solenoid_profile_t *solenoid_profile_active(void) {
    return &g_profile;
}

void gpio_init(unsigned int gpio) { (void)gpio; }
void gpio_put(unsigned int gpio, bool value) {
    if (gpio == MOTEUR_GLOBAL_SLEEP_GPIO) {
        g_nsleep_high = value;
    }
}
bool gpio_get(unsigned int gpio) {
    (void)gpio;
    return g_nfault_high;
}
bool gpio_get_out_level(unsigned int gpio) {
    (void)gpio;
    return g_nsleep_high;
}
void gpio_set_dir(unsigned int gpio, bool out) { (void)gpio; (void)out; }
void gpio_pull_up(unsigned int gpio) { (void)gpio; }

bool pwm_hal_init(pwm_hal_t *hal, unsigned int gpio, uint32_t freq_hz) {
    (void)gpio;
    hal->wrap = 1000u;
    hal->enabled = false;
    (void)freq_hz;
    return true;
}
uint32_t pwm_hal_actual_freq_hz(const pwm_hal_t *hal) { (void)hal; return 20000u; }
void pwm_hal_set_duty_percent(pwm_hal_t *hal, uint32_t percent) { (void)hal; (void)percent; }
void pwm_hal_enable(pwm_hal_t *hal) { hal->enabled = true; }
void pwm_hal_disable(pwm_hal_t *hal) { hal->enabled = false; }
void pwm_hal_release_hiz(pwm_hal_t *hal) { hal->enabled = false; }

static void init_driver(solenoid_driver_t *drv, solenoid_state_t state) {
    memset(drv, 0, sizeof(*drv));
    drv->state = state;
    drv->fault = SOLENOID_FAULT_NONE;
    drv->fault_gpio = 39u;
    drv->pwm_channel.wrap = 1000u;
}

int main(void) {
    solenoid_driver_t drv;

    // W-01/W-02/W-07 : la porte temporelle refuse avant l'échéance, accepte après.
    solenoid_global_sleep_init();
    solenoid_global_wake(0u);
    init_driver(&drv, SOLENOID_STATE_IDLE);
    CHECK(solenoid_driver_note_on(&drv, 100u, 1999u) == SOLENOID_ERR_SLEEP);
    CHECK(solenoid_driver_probe_start(&drv, 1999u) == SOLENOID_ERR_SLEEP);
    CHECK(solenoid_driver_push_start(&drv, 1999u) == SOLENOID_ERR_SLEEP);
    CHECK(solenoid_driver_note_on(&drv, 100u, 2000u) == SOLENOID_OK);

    // W-06 : le tick pose le verrou même quand tous les canaux sont en FAULT.
    solenoid_global_sleep_init();
    solenoid_global_wake(0u);
    solenoid_driver_t all_fault[12];
    for (uint8_t i = 0u; i < 12u; ++i) {
        init_driver(&all_fault[i], SOLENOID_STATE_FAULT);
        solenoid_driver_tick(&all_fault[i], 2000u);
    }
    init_driver(&drv, SOLENOID_STATE_IDLE);
    CHECK(solenoid_driver_note_on(&drv, 100u, 2001u) == SOLENOID_OK);

    // W-03/W-09 : le verrou reste actif après 2^31 µs et le rebouclage.
    init_driver(&drv, SOLENOID_STATE_IDLE);
    CHECK(solenoid_driver_note_on(&drv, 100u, 0x80000000u + 2001u) == SOLENOID_OK);

    // W-08 : NFAULT n'est pas lu avant le réveil, puis verrouille FAULT après.
    solenoid_global_sleep_init();
    solenoid_global_wake(0u);
    g_nfault_high = false;
    init_driver(&drv, SOLENOID_STATE_IDLE);
    solenoid_driver_tick(&drv, 1999u);
    CHECK(drv.state == SOLENOID_STATE_IDLE);
    solenoid_driver_tick(&drv, 2000u);
    CHECK(drv.state == SOLENOID_STATE_FAULT);
    g_nfault_high = true;

    // W-05 : un sommeil explicite efface le verrou et coupe nSLEEP.
    solenoid_global_sleep();
    CHECK(!g_nsleep_high);
    init_driver(&drv, SOLENOID_STATE_IDLE);
    CHECK(solenoid_driver_note_on(&drv, 100u, 10000u) == SOLENOID_ERR_SLEEP);

    printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
