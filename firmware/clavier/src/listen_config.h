// ============================================================================
//  SEUL FICHIER À MODIFIER POUR RÉGLER L'ÉCOUTE IPROPI (ADR-0032 §5).
// ============================================================================
//
// Ces valeurs sont diffusées à toutes les cartes Moteur au démarrage du mode
// REC (18 trames LISTEN_PARAM, puis LISTEN_CONTROL avec leur CRC16). Aucune
// n'est compilée dans le firmware Moteur : celui-ci ne garde que la RAM.
//
// Procédure de réglage :
//   1. à chaud, sur la console du port 1 (PuTTY) : `e show`, `e set <champ>
//      <valeur>`, vérifier avec `e get <champ>` ;
//   2. recopier les lignes affichées par `e diff` (les seuls champs différents
//      de ce fichier) ;
//   3. recompiler et flasher le Clavier.
//
// Aucune valeur ci-dessous n'est une limite électrique ou un garde-fou : ce
// sont des paramètres d'écoute (fenêtres, seuils ADC, cadences, vélocité MIDI).
// Le profil d'activation des solénoïdes reste `solenoid_profile_config.h`.
//
// Contrôles structurels appliqués par listen_params_validate() (rejet en bloc,
// jamais d'écrêtage) : samples_per_dwell 1..64, split_pct 1..99,
// trace_period_us >= 2, trace_timeout_ms >= 1, stream_decim >= 1,
// return_thr < rise_thr, vel_fixed 1..127, couple (vel_min_delta,
// vel_max_delta) soit (0,0), soit min > 0, max > 0 et min < max, detail 0|1.
//
// Format complet et sémantique : shared/protocol/listen_params.h et
// docs/specs/CAN.md (doc-aval).
#ifndef CLAVIER_LISTEN_CONFIG_H
#define CLAVIER_LISTEN_CONFIG_H

#include "listen_params.h"

static const listen_params_t k_listen_config = {
    .settle_us = 15u,              // attente mux après commutation (écoute)
    .samples_per_dwell = 2u,       // conversions moyennées par canal (1..64)
    .baseline_init_ms = 200u,      // fenêtre de ligne de base initiale
    .baseline_tau_ms = 500u,       // constante de temps du suivi lent
    .rise_thr = 5u,                // seuil de début (points au-dessus de la base)
    .return_thr = 3u,              // seuil de retour (bruit max 3 points)
    .return_hold_us = 1000u,       // maintien du retour avant fin d'impulsion
    .split_pct = 30u,              // creux de trille (1..99 %)
    .min_pulse_us = 500u,          // impulsion minimale rapportée
    .drive_blank_ms = 50u,         // masquage après pilotage d'un canal
    .trace_period_us = 20u,        // période de capture `trace` (50 kHz)
    .trace_trig = 5u,              // déclenchement de trace au-dessus de la base
    .trace_timeout_ms = 10000u,    // ADR-0032 §11 : sans déclenchement
    .stream_decim = 1u,            // un échantillon sur N (>= 1)
    .vel_fixed = 100u,             // vélocité MIDI fixe (1..127)
    .vel_min_delta = 0u,           // (0,0) = vélocité fixe
    .vel_max_delta = 0u,
    .detail = 1u,                  // 1 = émet LISTEN_PULSE_TIMING
};

#endif  // CLAVIER_LISTEN_CONFIG_H
