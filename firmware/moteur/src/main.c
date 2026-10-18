#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "board_identity.h"
#include "can_link.h"
#include "clock_test.h"
#include "config.h"
#include "ipropi_selftest.h"
#include "solenoid_driver.h"

#define BUTTON_DEBOUNCE_US 20000u
#define MANUAL_TEST_CHANNEL_COUNT 2u

static const uint8_t MANUAL_TEST_CHANNELS[MANUAL_TEST_CHANNEL_COUNT] = {0u, 11u};
static uint8_t manual_test_next_index = 0u;
static uint8_t manual_test_active_channel = 0u;

static const unsigned int PWM_GPIOS[MOTEUR_CHANNEL_COUNT] = {
    MOTEUR_PWM_CHANNEL0_GPIO,  MOTEUR_PWM_CHANNEL1_GPIO,  MOTEUR_PWM_CHANNEL2_GPIO,
    MOTEUR_PWM_CHANNEL3_GPIO,  MOTEUR_PWM_CHANNEL4_GPIO,  MOTEUR_PWM_CHANNEL5_GPIO,
    MOTEUR_PWM_CHANNEL6_GPIO,  MOTEUR_PWM_CHANNEL7_GPIO,  MOTEUR_PWM_CHANNEL8_GPIO,
    MOTEUR_PWM_CHANNEL9_GPIO,  MOTEUR_PWM_CHANNEL10_GPIO, MOTEUR_PWM_CHANNEL11_GPIO,
};

static const unsigned int DIR_GPIOS[MOTEUR_CHANNEL_COUNT] = {
    MOTEUR_DIR_CHANNEL0_GPIO,  MOTEUR_DIR_CHANNEL1_GPIO,  MOTEUR_DIR_CHANNEL2_GPIO,
    MOTEUR_DIR_CHANNEL3_GPIO,  MOTEUR_DIR_CHANNEL4_GPIO,  MOTEUR_DIR_CHANNEL5_GPIO,
    MOTEUR_DIR_CHANNEL6_GPIO,  MOTEUR_DIR_CHANNEL7_GPIO,  MOTEUR_DIR_CHANNEL8_GPIO,
    MOTEUR_DIR_CHANNEL9_GPIO,  MOTEUR_DIR_CHANNEL10_GPIO, MOTEUR_DIR_CHANNEL11_GPIO,
};

static const unsigned int FAULT_GPIOS[MOTEUR_CHANNEL_COUNT] = {
    MOTEUR_FAULT_CHANNEL0_GPIO,  MOTEUR_FAULT_CHANNEL1_GPIO,  MOTEUR_FAULT_CHANNEL2_GPIO,
    MOTEUR_FAULT_CHANNEL3_GPIO,  MOTEUR_FAULT_CHANNEL4_GPIO,  MOTEUR_FAULT_CHANNEL5_GPIO,
    MOTEUR_FAULT_CHANNEL6_GPIO,  MOTEUR_FAULT_CHANNEL7_GPIO,  MOTEUR_FAULT_CHANNEL8_GPIO,
    MOTEUR_FAULT_CHANNEL9_GPIO,  MOTEUR_FAULT_CHANNEL10_GPIO, MOTEUR_FAULT_CHANNEL11_GPIO,
};

static void report_result(uint8_t channel, const char *action, solenoid_result_t result) {
    if (result != SOLENOID_OK) {
        printf("[solenoide%u] %s refuse: %s\n", (unsigned)channel, action, solenoid_result_name(result));
    }
}

static bool all_channels_idle(const solenoid_driver_t *drivers) {
    for (uint8_t i = 0u; i < MOTEUR_CHANNEL_COUNT; ++i) {
        if (solenoid_driver_state(&drivers[i]) != SOLENOID_STATE_IDLE) {
            return false;
        }
    }
    return true;
}

int main(void) {
    stdio_init_all();

    gpio_init(MOTEUR_TEST_BUTTON_GPIO);
    gpio_set_dir(MOTEUR_TEST_BUTTON_GPIO, GPIO_IN);
    gpio_pull_up(MOTEUR_TEST_BUTTON_GPIO);

    solenoid_global_sleep_init();

    solenoid_driver_t drivers[MOTEUR_CHANNEL_COUNT];
    for (uint8_t i = 0u; i < MOTEUR_CHANNEL_COUNT; ++i) {
        const solenoid_result_t init_result =
            solenoid_driver_init(&drivers[i], PWM_GPIOS[i], DIR_GPIOS[i], FAULT_GPIOS[i]);
        if (init_result != SOLENOID_OK) {
            printf("[solenoide%u] init en echec: %s\n", (unsigned)i, solenoid_result_name(init_result));
        }
    }

    solenoid_global_wake(time_us_32());
    clock_test_init();

    uint8_t board_uid[BOARD_IDENTITY_UID_SIZE];
    if (board_identity_read(board_uid)) {
        can_link_init(board_uid, drivers, MOTEUR_CHANNEL_COUNT, time_us_32());
    } else {
        printf("[moteur] UID invalide: CAN desactive\n");
    }

    bool stable_pressed = false;
    bool raw_pressed = false;
    uint32_t raw_change_us = time_us_32();
    bool ignore_until_release = false;
    bool selftest_trigger_pending = false;
    bool selftest_logged = false;
    solenoid_state_t last_state = solenoid_driver_state(&drivers[0]);
    solenoid_fault_t last_fault = SOLENOID_FAULT_NONE;
    bool last_timeout = false;

    // Choix documenté : l'appui court alterne le test manuel sur les canaux 0 et 11,
    // l'appui long 3 s coupe ce test puis émet 0x402 dès que tous les canaux sont
    // revenus IDLE. Le clock-test 5 s reste accessible sur le même bouton.
    printf("[moteur] Livraison 1 prete. Etat canal0: %s\n", solenoid_state_name(last_state));

    while (true) {
        const uint32_t now_us = time_us_32();
        const bool raw = !gpio_get(MOTEUR_TEST_BUTTON_GPIO);

        if (raw != raw_pressed) {
            raw_pressed = raw;
            raw_change_us = now_us;
        }

        bool rising = false;
        bool falling = false;
        if (raw_pressed != stable_pressed && (uint32_t)(now_us - raw_change_us) >= BUTTON_DEBOUNCE_US) {
            stable_pressed = raw_pressed;
            rising = stable_pressed;
            falling = !stable_pressed;
        }

        const clock_test_event_t clk_event = clock_test_update(stable_pressed, now_us);
        if (clk_event == CLK_TEST_EVENT_ENTERED) {
            report_result(manual_test_active_channel, "note_off (entree clock-test)",
                          solenoid_driver_note_off(&drivers[manual_test_active_channel], now_us));
            ignore_until_release = true;
        } else if (clk_event == CLK_TEST_EVENT_EXITED) {
            ignore_until_release = true;
        }

        if (rising && !ignore_until_release) {
            manual_test_active_channel = MANUAL_TEST_CHANNELS[manual_test_next_index];
            if (solenoid_driver_state(&drivers[manual_test_active_channel]) == SOLENOID_STATE_FAULT) {
                report_result(manual_test_active_channel, "reset defaut",
                              solenoid_driver_reset_fault(&drivers[manual_test_active_channel], now_us));
            } else {
                report_result(manual_test_active_channel, "note_on",
                              solenoid_driver_note_on(&drivers[manual_test_active_channel], 0u, now_us));
            }
        }

        if (ipropi_selftest_button_update(stable_pressed, now_us)) {
            report_result(manual_test_active_channel, "note_off (declenchement selftest)",
                          solenoid_driver_note_off(&drivers[manual_test_active_channel], now_us));
            selftest_trigger_pending = true;
            ignore_until_release = true;
            selftest_logged = false;
        }

        if (falling) {
            if (!ignore_until_release) {
                report_result(manual_test_active_channel, "note_off",
                              solenoid_driver_note_off(&drivers[manual_test_active_channel], now_us));
                manual_test_next_index = (uint8_t)((manual_test_next_index + 1u) % MANUAL_TEST_CHANNEL_COUNT);
            }
            ignore_until_release = false;
            selftest_logged = false;
        }

        for (uint8_t i = 0u; i < MOTEUR_CHANNEL_COUNT; ++i) {
            solenoid_driver_tick(&drivers[i], now_us);
        }

        if (selftest_trigger_pending && all_channels_idle(drivers)) {
            if (can_link_send_selftest_trigger()) {
                printf("[moteur] selftest trigger 0x402 emis apres appui long\n");
            } else {
                printf("[moteur] echec emission selftest trigger 0x402\n");
            }
            selftest_trigger_pending = false;
            selftest_logged = true;
        } else if (selftest_trigger_pending && !selftest_logged) {
            printf("[moteur] attente retour IDLE avant emission 0x402\n");
            selftest_logged = true;
        }

        can_link_poll(now_us);

        const solenoid_state_t state = solenoid_driver_state(&drivers[0]);
        if (state != last_state) {
            printf("[solenoide0] %s -> %s\n", solenoid_state_name(last_state), solenoid_state_name(state));
            last_state = state;
        }
        const solenoid_fault_t fault = solenoid_driver_fault(&drivers[0]);
        if (fault != last_fault) {
            if (fault != SOLENOID_FAULT_NONE) {
                printf("[solenoide0] DEFAUT %s: PWM coupe, reset explicite requis\n",
                       fault == SOLENOID_FAULT_NFAULT ? "NFAULT" : "SLEEP");
            }
            last_fault = fault;
        }
        const bool timeout = solenoid_driver_hold_timed_out(&drivers[0]);
        if (timeout && !last_timeout) {
            printf("[solenoide0] HOLD max atteint: relachement force\n");
        }
        last_timeout = timeout;
    }
}
