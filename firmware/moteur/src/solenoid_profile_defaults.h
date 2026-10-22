// Valeurs de REPLI du profil d'activation (ADR-0013 §2).
//
// Ce ne sont PAS des bornes : le profil transmis par le Clavier au lancement de
// l'auto-test IPROPI les remplace, et le résultat est persisté en flash
// (solenoid_profile_store.c). Ces valeurs ne servent qu'avant le premier
// auto-test, ou après un effacement complet de la flash Moteur.
//
// NE JAMAIS RÉGLER ICI : le point de réglage unique est
// `firmware/clavier/src/solenoid_profile_config.h`. Copie du fichier Clavier au
// moment de la migration (2026-10-21), conservée volontairement.
#ifndef MOTEUR_SOLENOID_PROFILE_DEFAULTS_H
#define MOTEUR_SOLENOID_PROFILE_DEFAULTS_H

#include "solenoid_profile.h"

static const solenoid_profile_t k_solenoid_profile_defaults = {
    .pwm_freq_hz = 20000u,
    // ADR-0019 : phase IMPACT désactivée par défaut côté repli ; elle ne s'active
    // qu'après le premier profil v2 reçu du Clavier. Une carte jamais synchronisée
    // joue donc le jeu normal d'avant l'ajout de la phase.
    .impact_duration_us = 0u,
    .impact_duty_pct = 100u,
    .impact_max_us = 2000u,
    .pull_duration_us = 15000u,
    .pull_duty_pct = 100u,
    .pull_max_us = 50000u,
    .hold_duty_pct = 25u,
    .hold_duty_max_pct = 25u,
    .hold_max_ms = 20000u,
    .release_duration_us = 5000u,
    .release_duty_pct = 50u,
    .probe_duration_us = 5000u,
    .probe_duty_pct = 20u,
    .probe_max_us = 10000u,
    .probe_duty_max_pct = 20u,
    .maint_duty_pct = 50u,
    .maint_duty_max_pct = 100u,
    .maint_max_ms = 20000u,
    .maint_min_rest_ms = 2000u,
    .min_rest_ms = 10u,
    .dir_safety_delay_us = 200u,
    .wake_delay_us = 2000u,
    .dir_level_pull = 0u,
    .dir_level_release = 1u,
    .dir_level_maint = 1u,
    .duty_max_pct = 100u,
};

#endif  // MOTEUR_SOLENOID_PROFILE_DEFAULTS_H
