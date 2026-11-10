// Détecteur d'impulsion IPROPI (ADR-0032 §2) : module PUR, aucune dépendance
// SDK, aucune allocation, une instance par canal. `now_us` est fourni par
// l'appelant (Core 1), ce qui rend l'algorithme testable sur PC.
//
// Une impulsion = appui OU relâchement : IPROPI mesure |i|, le sens n'est pas
// lisible (ADR-0032 contexte). Le détecteur ne décide jamais d'une note, il
// rapporte base, crête, delta, instants et drapeau de coupure de trille.
#ifndef MOTEUR_PULSE_DETECTOR_H
#define MOTEUR_PULSE_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "listen_params.h"

typedef enum {
    PULSE_STATE_INIT = 0,        // fenêtre baseline_init_ms en cours
    PULSE_STATE_IDLE = 1,        // calme, base suivie
    PULSE_STATE_RISE = 2,        // au-dessus de base + rise_thr
    PULSE_STATE_RETURN_HOLD = 3, // retour sous base + return_thr, maintien en cours
} pulse_state_t;

typedef struct {
    uint16_t base;       // ligne de base au début de l'impulsion
    uint16_t peak;       // maximum
    uint16_t delta;      // peak - base, saturant à 0xFFFF
    uint32_t t_peak_us;  // t_peak - t0
    uint32_t t_end_us;   // t_end - t0
    bool split;          // fin par creux de trille
} pulse_event_t;

typedef struct {
    listen_params_t cfg;  // copie à l'entrée en écoute, jamais relue depuis l'extérieur
    pulse_state_t state;
    uint32_t baseline_q16;  // base en Q16.16
    uint32_t init_sum;
    uint32_t init_count;
    uint32_t init_end_us;
    uint16_t last_sample;
    uint16_t base_at_start;
    uint16_t peak;
    uint16_t trough;
    bool trough_armed;       // creux de trille armé après la crête
    uint32_t last_calm_us;   // dernier échantillon <= base + return_thr
    uint32_t t0_us;
    uint32_t t_peak_us;
    uint32_t t_trough_us;
    uint32_t return_candidate_us;
    // Horodatage du dernier échantillon : indispensable au dt de l'EMA de base
    // (le contrat ADR-0032 §2 ne le nommait pas, mais la formule l'exige).
    uint32_t last_sample_us;
    uint32_t rejected;  // impulsions < min_pulse_us
} pulse_detector_t;

// Initialise le détecteur avec le jeu d'écoute fourni (copie) et démarre la
// fenêtre de base initiale.
void pulse_detector_init(pulse_detector_t *pd, const listen_params_t *params, uint32_t now_us);

// Réinitialise l'état pour une nouvelle écoute, en conservant `cfg`.
void pulse_detector_reset(pulse_detector_t *pd, uint32_t now_us);

// Retourne true si `out` reçoit une impulsion complète et non rejetée.
// `out` peut être NULL : l'impulsion est alors seulement comptée.
bool pulse_detector_feed(pulse_detector_t *pd, uint16_t sample, uint32_t now_us, pulse_event_t *out);

// Ligne de base courante arrondie. Pendant l'init, renvoie la moyenne des
// échantillons déjà accumulés (utile au déclenchement de trace).
uint16_t pulse_detector_baseline(const pulse_detector_t *pd);

#endif  // MOTEUR_PULSE_DETECTOR_H
