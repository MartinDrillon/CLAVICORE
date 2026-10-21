// Profil électrique d'un solénoïde : TOUTES les constantes réglables, isolées de
// la logique (solenoid_driver.c). Modifier ce fichier seul pour les essais.
//
// !!! Toutes les valeurs ci-dessous sont des valeurs initiales PRUDENTES,
// !!! À AJUSTER EMPIRIQUEMENT SUR PROTOTYPE (bobine sur mesure, fenêtre 2-3 A).
//
// Les limites SOLENOID_DUTY_MAX_PERCENT / SOLENOID_PULL_MAX_MS /
// SOLENOID_HOLD_MAX_MS sont de plus ré-appliquées (écrêtage) dans le driver
// (invariant 4 d'AGENTS.md) : un profil mal réglé ne peut pas les dépasser.
// Le profil MAINTENANCE est en outre borné par un budget thermique en
// (durée x duty) vérifié à la compilation (ADR-0012).
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
// 5 ms (ADR-0009) : le courant est quasi établi vers 3 ms, ce qui permet une
// mesure de résistance sur la FIN de l'impulsion (§5b). Plafond pilote
// SOLENOID_PROBE_MAX_MS inchangé : la borne reste appliquée par le driver.
#define SOLENOID_PROBE_DURATION_MS 5u     // ADR-0009 (relevé de 3 ms à 5 ms)
#define SOLENOID_PROBE_DUTY_PERCENT 20u   // validé empiriquement 2026-10-18

// --- Constantes électriques du calcul ohmique (ADR-0009 §5c, §5d) -----------
// Estimation de la résistance DC d'une bobine et du courant à 100 % de duty.
// `A_IPROPI` et `R_IPROPI` sont des constantes de CARTE (documentation du
// DRV8874 + schéma) : la sensibilité K = A_IPROPI x R_IPROPI est calculée à
// partir d'elles, jamais écrite en dur.
#define SOLENOID_IPROPI_UA_PER_A 450u     // A_IPROPI du DRV8874 (450 µA/A)
#define SOLENOID_IPROPI_RESISTOR_OHM 2400u // R_IPROPI vers GND (2,4 kΩ)
#define SOLENOID_VM_MILLIVOLT 24000u       // alimentation 24 V (à mesurer)
#define SOLENOID_ADC_VREF_MILLIVOLT 3300u  // référence ADC (à mesurer)
#define SOLENOID_RDSON_MILLIOHM 240u       // R_DS(on) du DRV8874 (datasheet)

// --- Phase MAINTENANCE (force INVERSE, « REPEL ») : ADR-0006 §4, ADR-0012 ----
// L'aimant est REPOUSSÉ (polarité inverse du jeu normal) tant que la touche est
// tenue : aucune phase HOLD, aucune phase RELEASE. Dérogation bornée à
// l'invariant 4 : le driver écrête le duty et coupe lui-même à SOLENOID_MAINT_MAX_MS.
// Le niveau de polarité correspondant est SOLENOID_MAINT_DIR_LEVEL, déclaré dans
// la section « Polarité de DIR_CHANNEL » ci-dessous.
#define SOLENOID_MAINT_DUTY_PERCENT 50u
#define SOLENOID_MAINT_MAX_MS 20000u            // ADR-0012 (relevé de 10 s à 20 s)
#define SOLENOID_MAINT_DUTY_MAX_PERCENT 100u
#define SOLENOID_MAINT_MIN_REST_MS 2000u
// Enveloppe thermique validée par l'utilisateur (ADR-0006 §4) : équivalent de
// 10 s à 100 % de duty, soit 1 000 000 en (ms x %). 20 s à 50 % dissipe la même
// énergie (1 000 000). Vérifiée à la compilation dans solenoid_driver.c : un
// réglage de banc ne peut pas la dépasser.
#define SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT (10000u * 100u)

// --- Polarité de DIR_CHANNEL (PH/IN2) ----------------------------------------
// Niveau logique 0/1 de dir_channel pour chaque phase (à confirmer au banc).
#define SOLENOID_DIR_LEVEL_PULL 0
#define SOLENOID_DIR_LEVEL_RELEASE 1
// Niveau de la poussée MAINTENANCE : INVERSE du jeu normal (ADR-0012).
#define SOLENOID_MAINT_DIR_LEVEL SOLENOID_DIR_LEVEL_RELEASE

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
