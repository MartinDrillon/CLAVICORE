// Pont entre la console `e` (bringup_console.c) et l'état Clavier (main.c).
//
// `clavier_state_t` reste privé à main.c : la console ne voit que ce contrat,
// qui ne contient aucune valeur d'écoute et n'ajoute aucune limite électrique
// (invariant 4). Les fonctions sont implémentées dans main.c ; les tests hôte
// de la console les remplacent par des doublures.
#ifndef CLAVIER_REC_CONSOLE_H
#define CLAVIER_REC_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#include "commissioning_table.h"
#include "listen_params.h"
#include "rec_mode.h"

// Accès en lecture/écriture au jeu de paramètres actif (RAM Clavier).
listen_params_t *rec_console_params(void);
// Jeu compilé (k_listen_config), référence de `e diff` et `e reset`.
const listen_params_t *rec_console_compiled_params(void);
// État REC (alternance, fusion, bruit, résumé de trace).
rec_state_t *rec_console_state(void);
// Table de commissioning et validité (résolution des touches).
const commissioning_runtime_t *rec_console_runtime(void);
bool rec_console_table_valid(void);
// Le jeu actif a passé listen_params_validate() au boot / après `e set`.
bool rec_console_params_valid(void);
rec_mode_t rec_console_mode(void);

// `e rec` : entrée en REC_SOLE. Refus journalisé par main.c ; retourne vrai
// seulement si le mode est effectivement actif après l'appel.
bool rec_console_enter_sole(void);
// `e raz` : remet l'alternance à zéro, sans émettre de Note Off.
bool rec_console_raz(void);
// Diffuse le jeu actif. En REC, le Moteur refuse LISTEN_PARAM hors OFF : on
// arrête puis on redémarre l'écoute, ce qui rediffuse jeu + CRC.
bool rec_console_send_params(void);

// `e trace` / `e stream` : `target` vient de la table de commissioning.
bool rec_console_trace(uint8_t midi_note, const commissioning_note_target_t *target);
bool rec_console_stream(const commissioning_note_target_t *target);
bool rec_console_stream_off(void);

#endif  // CLAVIER_REC_CONSOLE_H
