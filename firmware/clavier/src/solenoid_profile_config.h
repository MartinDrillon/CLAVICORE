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
//   - duties 0..100 %, plafonds >= valeurs opératoires ;
//   - pull_duration_us <= pull_max_us, probe_duration_us <= probe_max_us,
//     impact_duration_us <= impact_max_us ;
//   - dir_level_pull != dir_level_release, niveaux 0/1 ;
//   - pwm_freq_hz dans 1..100000 Hz ;
//   - durées en ms <= 4 000 000 (représentation µs sur 32 bits) ;
//   - la fenêtre ADC doit couvrir une période PWM et finir avant la coupure
//     PROBE (contrôle de mesure côté Moteur).
//
// Format complet et sémantique : docs/specs/profil-solenoides.md
// Procédure : docs/procedure-profil-solenoides.md
#ifndef CLAVIER_SOLENOID_PROFILE_CONFIG_H
#define CLAVIER_SOLENOID_PROFILE_CONFIG_H

#include "solenoid_profile.h"

static const solenoid_profile_t k_solenoid_profile = {
    .pwm_freq_hz = 20000u,          // porteuse PWM, au-dessus du domaine audible
    .impact_duration_us = 800u,     // IMPACT : impulsion 100 % très courte (ADR-0019)
    .impact_duty_pct = 100u,
    .impact_max_us = 2000u,         // plafond de cohérence de l'impulsion
    .pull_duration_us = 10000u,     // PULL : attraction, très court
    .pull_duty_pct = 50u,
    .pull_max_us = 50000u,          // plafond opératoire du PULL
    .hold_duty_pct = 25u,           // HOLD : maintien jusqu'au note off
    .hold_duty_max_pct = 25u,
    .hold_max_ms = 20000u,          // coupure de sécurité du HOLD
    .release_duration_us = 1000u,   // RELEASE : polarité inversée, court
    .release_duty_pct = 50u,
    .probe_duration_us = 5000u,     // PROBE : impulsion de l'auto-test IPROPI
    .probe_duty_pct = 20u,
    .probe_max_us = 10000u,
    .probe_duty_max_pct = 20u,
    .maint_duty_pct = 50u,          // MAINTENANCE : poussée inverse tenue
    .maint_duty_max_pct = 100u,
    .maint_max_ms = 20000u,         // coupure de sécurité de la MAINTENANCE
    .maint_min_rest_ms = 2000u,     // repos après une poussée maintenance
    .min_rest_ms = 10u,             // repos minimal entre deux cycles
    .dir_safety_delay_us = 200u,    // délai duty=0 -> changement de polarité
    .wake_delay_us = 2000u,         // tWAKE du DRV8874 après réveil
    .dir_level_pull = 0u,           // niveaux PH/IN2 (à confirmer au banc)
    .dir_level_release = 1u,
    .dir_level_maint = 1u,          // poussée inverse du jeu normal
    .duty_max_pct = 100u,           // plafond appliqué par le driver
};

#endif  // CLAVIER_SOLENOID_PROFILE_CONFIG_H
