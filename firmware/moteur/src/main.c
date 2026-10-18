// Bring-up Moteur Phase 2a (Docs/specs/Roadmap.md) : canal 0 uniquement, sans CAN.
//
// Comportement :
//  - Appui court sur le bouton GPIO34 : note_on au front d'appui, note_off au
//    relâchement. La machine à états (solenoid_driver.c) applique PULL -> HOLD ->
//    RELEASE avec ses propres limites ; le profil est dans solenoid_profile.h.
//  - Appui maintenu 5,0 s : clock-test actif (si compilé). Tout appui
//    suivant quitte le clock-test ; cet appui est ignoré par le solénoïde.
//  - NFAULT : coupure PWM immédiate, état FAULT verrouillé. Un appui bouton tente
//    alors le reset explicite (refusé tant que NFAULT reste bas).
//
// Procédure de mesure du « test fréquence » (oscilloscope / analyseur logique) :
//  (a) Horloge : maintenir le bouton 5 s ; mesurer GPIO41 (/FREQ OSC).
//      Attendu : carré 50 % à 1 MHz (période 1 µs) pour clk_sys = 150 MHz ; un
//      écart indique une clk_sys réelle différente (la trace série donne
//      clk_sys, wrap, fréquence théorique). Appui suivant : GPIO41 en Hi-Z.
//  (b) Porteuse PWM canal 0 : mesurer GPIO35 (pwm_channel 0) pendant une note
//      tenue (HOLD). Attendu : 20 kHz (période 50 µs), duty ~25 % en HOLD
//      (SOLENOID_PWM_FREQ_HZ / SOLENOID_HOLD_DUTY_PERCENT dans solenoid_profile.h).
//      Solénoïde éventuellement débranché pour cette mesure.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "board_identity.h"
#include "can_link.h"
#include "clock_test.h"
#include "config.h"
#include "solenoid_driver.h"

#define BUTTON_DEBOUNCE_US 20000u  // état stable 20 ms avant prise en compte

static void report_result(const char *action, solenoid_result_t result) {
    if (result != SOLENOID_OK) {
        printf("[solenoide0] %s refuse: %s\n", action, solenoid_result_name(result));
    }
}

int main(void) {
    stdio_init_all();

    gpio_init(MOTEUR_TEST_BUTTON_GPIO);
    gpio_set_dir(MOTEUR_TEST_BUTTON_GPIO, GPIO_IN);
    gpio_pull_up(MOTEUR_TEST_BUTTON_GPIO);  // actif bas

    // global_sleep : drivers endormis à l'init, réveillés avant toute activation de canal.
    solenoid_global_sleep_init();

    solenoid_driver_t channel0;
    const solenoid_result_t init_result =
        solenoid_driver_init(&channel0, MOTEUR_PWM_CHANNEL0_GPIO, MOTEUR_DIR_CHANNEL0_GPIO,
                             MOTEUR_FAULT_CHANNEL0_GPIO);
    if (init_result != SOLENOID_OK) {
        printf("[solenoide0] init en echec: %s (canal verrouille)\n", solenoid_result_name(init_result));
    }
    solenoid_global_wake(time_us_32());
    clock_test_init();

    // CAN Phase 3 (ADR-0003) : annonce d'UID uniquement. Aucune commande de solénoïde
    // ne passe par le CAN. Sans UID valide, le CAN reste inactif (signalé, jamais masqué).
    uint8_t board_uid[BOARD_IDENTITY_UID_SIZE];
    if (board_identity_read(board_uid)) {
        can_link_init(board_uid, time_us_32());
    } else {
        printf("[moteur] UID invalide: CAN desactive\n");
    }
    bool stable_pressed = false;
    bool raw_pressed = false;
    uint32_t raw_change_us = time_us_32();
    bool ignore_until_release = false;
    solenoid_state_t last_state = solenoid_driver_state(&channel0);
    solenoid_fault_t last_fault = SOLENOID_FAULT_NONE;
    bool last_timeout = false;

    printf("[moteur] Phase 2a prete. Etat canal0: %s\n", solenoid_state_name(last_state));

    while (true) {
        const uint32_t now_us = time_us_32();

        // Anti-rebond non bloquant.
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

        // Clock-test : ENTERED/EXITED consomment l'appui en cours.
        const clock_test_event_t clk_event = clock_test_update(stable_pressed, now_us);
        if (clk_event == CLK_TEST_EVENT_ENTERED) {
            report_result("note_off (entree clock-test)", solenoid_driver_note_off(&channel0, now_us));
            ignore_until_release = true;
        } else if (clk_event == CLK_TEST_EVENT_EXITED) {
            ignore_until_release = true;
        }

        if (rising && !ignore_until_release) {
            if (solenoid_driver_state(&channel0) == SOLENOID_STATE_FAULT) {
                report_result("reset defaut", solenoid_driver_reset_fault(&channel0, now_us));
            } else {
                report_result("note_on", solenoid_driver_note_on(&channel0, 0u, now_us));
            }
        }
        if (falling) {
            if (!ignore_until_release) {
                report_result("note_off", solenoid_driver_note_off(&channel0, now_us));
            }
            ignore_until_release = false;
        }

        solenoid_driver_tick(&channel0, now_us);

        // CAN : non bloquant, travail borné, appelé APRÈS le pilote pour ne jamais le retarder.
        can_link_poll(now_us);

        // Affichage minimal : changements d'état, défauts, coupure HOLD max.
        const solenoid_state_t state = solenoid_driver_state(&channel0);
        if (state != last_state) {
            printf("[solenoide0] %s -> %s\n", solenoid_state_name(last_state), solenoid_state_name(state));
            last_state = state;
        }
        const solenoid_fault_t fault = solenoid_driver_fault(&channel0);
        if (fault != last_fault) {
            if (fault != SOLENOID_FAULT_NONE) {
                printf("[solenoide0] DEFAUT %s: PWM coupe, reset explicite requis\n",
                       fault == SOLENOID_FAULT_NFAULT ? "NFAULT" : "SLEEP");
            }
            last_fault = fault;
        }
        const bool timeout = solenoid_driver_hold_timed_out(&channel0);
        if (timeout && !last_timeout) {
            printf("[solenoide0] HOLD max atteint: relachement force\n");
        }
        last_timeout = timeout;
    }
}
