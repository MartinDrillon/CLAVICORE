// Machine à états non bloquante d'un solénoïde (un canal = une instance).
// Contrat HAL : Docs/adr/adr-0001-hal-solenoide-drv8874-phen.md (rôles logiques
// pwm_channel / dir_channel / fault_channel / global_sleep).
//
//   IDLE -note_on-> PULL -> HOLD -note_off-> DIR_TO_RELEASE -> RELEASE
//        -> DIR_TO_PULL -> REST -> IDLE
//   (tout état actif) -NFAULT bas-> FAULT (verrouillé, reset explicite)
//
// DIR_TO_RELEASE / DIR_TO_PULL sont les états transitoires imposés par l'ADR :
// duty ramené à 0, attente du délai de sécurité, PUIS changement de dir_channel.
#ifndef MOTEUR_SOLENOID_DRIVER_H
#define MOTEUR_SOLENOID_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "pwm_hal.h"

typedef enum {
    SOLENOID_STATE_IDLE = 0,
    SOLENOID_STATE_PULL,
    SOLENOID_STATE_HOLD,
    SOLENOID_STATE_DIR_TO_RELEASE,
    SOLENOID_STATE_RELEASE,
    SOLENOID_STATE_DIR_TO_PULL,
    SOLENOID_STATE_REST,
    SOLENOID_STATE_PROBE,
    SOLENOID_STATE_MAINTENANCE,
    SOLENOID_STATE_FAULT,
} solenoid_state_t;

typedef enum {
    SOLENOID_OK = 0,
    SOLENOID_ERR_BUSY,            // canal non IDLE (déjà actif, retour, repos)
    SOLENOID_ERR_FAULT,           // état FAULT verrouillé
    SOLENOID_ERR_SLEEP,           // global_sleep non actif ou réveil pas terminé
    SOLENOID_ERR_REST,            // repos minimal non écoulé
    SOLENOID_ERR_PARAM,           // paramètre invalide
    SOLENOID_ERR_PWM,             // init PWM impossible
    SOLENOID_ERR_FAULT_PERSISTS,  // reset refusé : NFAULT toujours bas
} solenoid_result_t;

typedef enum {
    SOLENOID_FAULT_NONE = 0,
    SOLENOID_FAULT_NFAULT,  // NFAULT du DRV8874 actif (bas)
    SOLENOID_FAULT_SLEEP,   // global_sleep retombé pendant une activation
} solenoid_fault_t;

typedef struct {
    pwm_hal_t pwm_channel;
    unsigned int dir_gpio;    // dir_channel
    unsigned int fault_gpio;  // fault_channel
    solenoid_state_t state;
    solenoid_fault_t fault;
    uint32_t state_start_us;
    uint32_t rest_duration_us;
    bool note_off_pending;    // note_off reçu pendant PULL
    bool hold_timed_out;      // dernière phase HOLD coupée par la limite HOLD_MAX
} solenoid_driver_t;

// --- global_sleep (nSLEEP, partagé par les 12 drivers) ---
// Configure le GPIO en sortie, driver ENDORMI par défaut.
void solenoid_global_sleep_init(void);
// Réveille les drivers ; les canaux n'acceptent note_on qu'après SOLENOID_WAKE_DELAY_US.
void solenoid_global_wake(uint32_t now_us);
// Endort les drivers (coupure d'urgence commune).
void solenoid_global_sleep(void);

// --- Canal ---
// Configure pwm/dir/fault, sortie coupée, dir au niveau PULL.
solenoid_result_t solenoid_driver_init(solenoid_driver_t *drv, unsigned int pwm_gpio,
                                       unsigned int dir_gpio, unsigned int fault_gpio);

// velocity : réservé (ignoré en Phase 2a).
solenoid_result_t solenoid_driver_note_on(solenoid_driver_t *drv, uint8_t velocity, uint32_t now_us);

// Idempotent : sans effet hors PULL/HOLD. Pendant PULL, la fin de PULL est respectée.
solenoid_result_t solenoid_driver_note_off(solenoid_driver_t *drv, uint32_t now_us);

// Impulsion de test PULL seule, bornée par le driver. Refusée si le canal n'est
// pas IDLE, si global_sleep n'est pas prêt, ou si un défaut est déjà actif.
solenoid_result_t solenoid_driver_probe_start(solenoid_driver_t *drv, uint32_t now_us);

// Poussée maintenance unidirectionnelle, bornée par le driver (duty + durée).
// Refusée si le canal n'est pas IDLE, si le repos maintenance n'est pas écoulé,
// si global_sleep n'est pas prêt, ou si un défaut est déjà actif.
solenoid_result_t solenoid_driver_push_start(solenoid_driver_t *drv, uint32_t now_us);

// À appeler à chaque tour de boucle principale.
void solenoid_driver_tick(solenoid_driver_t *drv, uint32_t now_us);

// Sortie le canal de FAULT après action explicite de l'opérateur. Refusé tant que
// NFAULT est bas. Passe par DIR_TO_PULL -> REST -> IDLE.
solenoid_result_t solenoid_driver_reset_fault(solenoid_driver_t *drv, uint32_t now_us);

solenoid_state_t solenoid_driver_state(const solenoid_driver_t *drv);
solenoid_fault_t solenoid_driver_fault(const solenoid_driver_t *drv);
bool solenoid_driver_hold_timed_out(const solenoid_driver_t *drv);

const char *solenoid_state_name(solenoid_state_t state);
const char *solenoid_result_name(solenoid_result_t result);

#endif  // MOTEUR_SOLENOID_DRIVER_H
