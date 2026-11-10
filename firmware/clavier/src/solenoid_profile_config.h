// ============================================================================
//  SEUL FICHIER À MODIFIER POUR RÉGLER L'ACTIVATION DES SOLÉNOÏDES (ADR-0013, ADR-0019).
// ============================================================================
//
// Après modification :
//   1. flasher le Clavier (clavier.uf2) ;
//   2. lancer un auto-test IPROPI (bouton Moteur maintenu 3 s) : le profil est
//      transmis aux cartes Moteur puis persisté dans leur flash.
//
// Les valeurs ci-dessous sont transmises aux cartes Moteur par les trames
// PROFILE_* (0x420-0x423) et conservées d'une mise sous tension à l'autre.
// Elles ne sont PLUS bornées par le firmware Moteur (ADR-0014 : dérogation à
// l'invariant 4) : une valeur cohérente mais dangereuse sera obéie. Les valeurs
// initiales sont celles validées au banc, mais ce ne sont plus des garde-fous.
//
// Contrôles structurels appliqués (rejet en bloc, jamais d'écrêtage) :
//   - duties 0..100 % ;
//   - hold_duty_pct <= hold_duty_max_pct, probe_duty_pct <= probe_duty_max_pct,
//     maint_duty_pct <= maint_duty_max_pct, et duty_max_pct >= chaque duty de
//     phase (impact/pull/hold/release/probe/maint) ;
//   - la durée de chaque phase (impact/pull/probe) est seule autorité : aucune
//     borne supérieure n'est vérifiée (ADR-0014, amendement du 2026-10-25) ;
//   - dir_level_pull != dir_level_release, niveaux 0/1 ;
//   - pwm_freq_hz dans 1..100000 Hz ;
//   - durées en ms <= 4 000 000 (représentation µs sur 32 bits).
//
// Format complet et sémantique : docs/specs/profil-solenoides.md
// Procédure : docs/procedure-profil-solenoides.md
#ifndef CLAVIER_SOLENOID_PROFILE_CONFIG_H
#define CLAVIER_SOLENOID_PROFILE_CONFIG_H

#include "solenoid_profile.h"

// Paires consigne/plafond exprimées en constantes de compilation : le garde-fou
// _Static_assert ci-dessous vérifie ainsi exactement les valeurs du profil.
enum {
    K_PROFILE_HOLD_DUTY_PCT = 25,
    K_PROFILE_HOLD_DUTY_MAX_PCT = 25,
    K_PROFILE_PROBE_DUTY_PCT = 20,
    K_PROFILE_PROBE_DUTY_MAX_PCT = 20,
    K_PROFILE_MAINT_DUTY_PCT = 100,
    K_PROFILE_MAINT_DUTY_MAX_PCT = 100,
};

static const solenoid_profile_t k_solenoid_profile = {
    .pwm_freq_hz = 20000u,          // porteuse PWM, au-dessus du domaine audible
    .impact_duration_us = 2000u,     // IMPACT : impulsion 100 % très courte (ADR-0019)
    .impact_duty_pct = 100u,
    .pull_duration_us = 12000u,     // PULL : attraction, très court
    .pull_duty_pct = 100u,
    .hold_duty_pct = K_PROFILE_HOLD_DUTY_PCT,  // HOLD : maintien jusqu'au note off
    .hold_duty_max_pct = K_PROFILE_HOLD_DUTY_MAX_PCT,
    .hold_max_ms = 20000u,          // coupure de sécurité du HOLD
    .release_duration_us = 5000u,   // RELEASE : polarité inversée, court
    .release_duty_pct = 30u,
    .probe_duration_us = 5000u,     // PROBE : impulsion de l'auto-test IPROPI
    .probe_duty_pct = K_PROFILE_PROBE_DUTY_PCT,
    .probe_duty_max_pct = K_PROFILE_PROBE_DUTY_MAX_PCT,
    .maint_duty_pct = K_PROFILE_MAINT_DUTY_PCT,  // MAINTENANCE : poussée inverse tenue
    .maint_duty_max_pct = K_PROFILE_MAINT_DUTY_MAX_PCT,
    .maint_max_ms = 20000u,         // coupure de sécurité de la MAINTENANCE
    .maint_min_rest_ms = 200u,     // repos après une poussée maintenance
    .min_rest_ms = 10u,             // repos minimal entre deux cycles
    .dir_safety_delay_us = 200u,    // délai duty=0 -> changement de polarité
    .wake_delay_us = 2000u,         // tWAKE du DRV8874 après réveil
    .dir_level_pull = 0u,           // niveaux PH/IN2 (à confirmer au banc)
    .dir_level_release = 1u,
    .dir_level_maint = 1u,          // poussée inverse du jeu normal
    .duty_max_pct = 100u,           // plafond appliqué par le driver
};

// Garde-fou de cohérence (pas une limite électrique) : la consigne d'une phase
// ne peut pas dépasser son plafond, sinon solenoid_profile_validate() refuse le
// profil en bloc (SOLENOID_PROFILE_ERR_DUTY) et la synchronisation échoue.
_Static_assert(K_PROFILE_HOLD_DUTY_PCT <= K_PROFILE_HOLD_DUTY_MAX_PCT,
               "hold_duty_pct doit rester <= hold_duty_max_pct");
_Static_assert(K_PROFILE_PROBE_DUTY_PCT <= K_PROFILE_PROBE_DUTY_MAX_PCT,
               "probe_duty_pct doit rester <= probe_duty_max_pct");
_Static_assert(K_PROFILE_MAINT_DUTY_PCT <= K_PROFILE_MAINT_DUTY_MAX_PCT,
               "maint_duty_pct doit rester <= maint_duty_max_pct");

#endif  // CLAVIER_SOLENOID_PROFILE_CONFIG_H
