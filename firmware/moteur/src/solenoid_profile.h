// Profil électrique d'un solénoïde : TOUTES les constantes réglables, isolées de
// la logique (solenoid_driver.c). Modifier ce fichier seul pour les essais.
//
// !!! Toutes les valeurs ci-dessous sont des valeurs initiales PRUDENTES,
// !!! À AJUSTER EMPIRIQUEMENT SUR PROTOTYPE (bobine sur mesure, fenêtre 2-3 A).
//
// Les limites SOLENOID_DUTY_MAX_PERCENT / SOLENOID_PULL_MAX_MS /
// SOLENOID_HOLD_MAX_MS sont de plus ré-appliquées (écrêtage) dans le driver
// (invariant 4 d'AGENTS.md) : un profil mal réglé ne peut pas les dépasser.
#ifndef MOTEUR_SOLENOID_PROFILE_H
#define MOTEUR_SOLENOID_PROFILE_H

// --- Porteuse PWM -----------------------------------------------------------
// 20 kHz : au-dessus du domaine audible. À mesurer sur PWM_CHANNEL[0] (GPIO35).
#define SOLENOID_PWM_FREQ_HZ 20000u

// --- Phase PULL (attraction) : duty haut, très court ------------------------
#define SOLENOID_PULL_DURATION_MS 15u   // à ajuster empiriquement sur prototype
#define SOLENOID_PULL_DUTY_PERCENT 100u // à ajuster empiriquement sur prototype

// --- Phase HOLD (maintien) : duty bas, jusqu'à note_off ---------------------
#define SOLENOID_HOLD_DUTY_PERCENT 25u  // à ajuster empiriquement sur prototype
// Coupure de sécurité forcée : au-delà, le driver relâche seul (invariant 4, ADR-0007).
#define SOLENOID_HOLD_MAX_MS 20000u           // ADR-0007 (relevé de 2 s à 20 s)
// Plafond absolu du duty HOLD (enveloppe thermique validée, ADR-0007).
#define SOLENOID_HOLD_DUTY_MAX_PERCENT 25u

// --- Phase RELEASE (polarité inversée), duty moyen, court -------------------
#define SOLENOID_RELEASE_DURATION_MS 5u    // à ajuster empiriquement sur prototype
#define SOLENOID_RELEASE_DUTY_PERCENT 50u  // à ajuster empiriquement sur prototype

// --- Phase PROBE (auto-test IPROPI) -----------------------------------------
#define SOLENOID_PROBE_DURATION_MS 3u     // validé empiriquement 2026-10-18
#define SOLENOID_PROBE_DUTY_PERCENT 20u   // validé empiriquement 2026-10-18

// --- Phase MAINTENANCE (push only) ------------------------------------------
#define SOLENOID_MAINT_DUTY_PERCENT 50u
#define SOLENOID_MAINT_MAX_MS 10000u
#define SOLENOID_MAINT_DUTY_MAX_PERCENT 100u
#define SOLENOID_MAINT_MIN_REST_MS 2000u

// --- Polarité de DIR_CHANNEL (PH/IN2) ----------------------------------------
// Niveau logique 0/1 de dir_channel pour chaque phase (à confirmer au banc).
#define SOLENOID_DIR_LEVEL_PULL 0
#define SOLENOID_DIR_LEVEL_RELEASE 1

// --- Sécurités ---------------------------------------------------------------
// Délai entre duty = 0 et changement de dir_channel (ADR-0001), en µs.
#define SOLENOID_DIR_SAFETY_DELAY_US 200u  // à ajuster empiriquement sur prototype
// Repos minimal entre la fin d'un cycle et le suivant, en ms.
#define SOLENOID_MIN_REST_MS 10u           // à ajuster empiriquement sur prototype
// Plafonds absolus appliqués par le driver quel que soit le profil ci-dessus.
#define SOLENOID_DUTY_MAX_PERCENT 100u
#define SOLENOID_PULL_MAX_MS 50u
#define SOLENOID_PROBE_MAX_MS 10u
#define SOLENOID_PROBE_DUTY_MAX_PERCENT 20u

// Délai après réveil de global_sleep (nSLEEP) avant d'accepter une activation
// (tWAKE du DRV8874, ~1 ms typique ; marge prise).
#define SOLENOID_WAKE_DELAY_US 2000u

#endif  // MOTEUR_SOLENOID_PROFILE_H
