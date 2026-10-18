#include "clock_test.h"

#if MOTEUR_ENABLE_CLOCK_TEST

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "config.h"
#include "pwm_hal.h"

#define CLOCK_TEST_LONG_PRESS_US 5000000u  // 5,0 s

static clock_test_state_t g_state = CLK_TEST_IDLE;
static pwm_hal_t g_pwm;
static bool g_prev_pressed;
static uint32_t g_press_start_us;
static bool g_press_consumed;  // appui déjà utilisé (entrée) : ne compte plus jusqu'au relâchement

void clock_test_init(void) {
    g_state = CLK_TEST_IDLE;
    g_prev_pressed = false;
    g_press_consumed = false;
    // GPIO41 : haute impédance tant que le test n'est pas actif.
    gpio_init(MOTEUR_CLOCK_TEST_GPIO);
    gpio_set_dir(MOTEUR_CLOCK_TEST_GPIO, GPIO_IN);
    gpio_disable_pulls(MOTEUR_CLOCK_TEST_GPIO);
}

static void activate(void) {
    // Hypothèse : clk_sys = 150 MHz => wrap = 149 (150 pas) => 1 MHz. On ne la suppose pas :
    // wrap/diviseur sont calculés depuis clock_get_hz(clk_sys) et la fréquence obtenue est loguée.
    if (!pwm_hal_init(&g_pwm, MOTEUR_CLOCK_TEST_GPIO, MOTEUR_CLOCK_TEST_FREQ_HZ)) {
        printf("[clock-test] ERREUR: init PWM impossible (clk_sys=%lu Hz)\n",
               (unsigned long)clock_get_hz(clk_sys));
        return;
    }
    pwm_hal_set_duty_percent(&g_pwm, 50u);
    pwm_hal_enable(&g_pwm);
    // Limiter l'EMI : slew lent (SLEWFAST = 0) et 2 mA.
    gpio_set_slew_rate(MOTEUR_CLOCK_TEST_GPIO, GPIO_SLEW_RATE_SLOW);
    gpio_set_drive_strength(MOTEUR_CLOCK_TEST_GPIO, GPIO_DRIVE_STRENGTH_2MA);
    g_state = CLK_TEST_ACTIVE;
    printf("[clock-test] ACTIF: clk_sys=%lu Hz, wrap=%u, div=%u, sortie=%lu Hz sur GPIO%u\n",
           (unsigned long)clock_get_hz(clk_sys), (unsigned)g_pwm.wrap, (unsigned)g_pwm.clkdiv_int,
           (unsigned long)pwm_hal_actual_freq_hz(&g_pwm), (unsigned)MOTEUR_CLOCK_TEST_GPIO);
}

clock_test_event_t clock_test_update(bool pressed, uint32_t now_us) {
    clock_test_event_t event = CLK_TEST_EVENT_NONE;
    const bool rising = pressed && !g_prev_pressed;
    g_prev_pressed = pressed;

    if (!pressed) {
        g_press_consumed = false;
        return event;
    }
    if (rising) {
        g_press_start_us = now_us;
        if (g_state == CLK_TEST_ACTIVE) {  // tout appui ensuite : retour IDLE, GPIO41 en Hi-Z
            pwm_hal_release_hiz(&g_pwm);
            g_state = CLK_TEST_IDLE;
            g_press_consumed = true;
            printf("[clock-test] IDLE: GPIO%u en haute impédance\n", (unsigned)MOTEUR_CLOCK_TEST_GPIO);
            return CLK_TEST_EVENT_EXITED;
        }
    }
    if (g_state == CLK_TEST_IDLE && !g_press_consumed &&
        (uint32_t)(now_us - g_press_start_us) >= CLOCK_TEST_LONG_PRESS_US) {
        g_press_consumed = true;
        activate();
        if (g_state == CLK_TEST_ACTIVE) {
            event = CLK_TEST_EVENT_ENTERED;
        }
    }
    return event;
}

clock_test_state_t clock_test_state(void) {
    return g_state;
}

#endif  // MOTEUR_ENABLE_CLOCK_TEST
