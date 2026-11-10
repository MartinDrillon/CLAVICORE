// Carte du multiplexeur IPROPI (74HC4067, ADR-0001) : SOURCE UNIQUE partagée
// par l'auto-test IPROPI et l'écoute (ADR-0032 §1). Aucune valeur d'écoute ici :
// seuls les GPIO de sélection et d'activation sont manipulés.
//
// Convention matérielle identique à ipropi_selftest.c d'origine :
//   - MOTEUR_MUX_EN_GPIO  : bas = mux actif, haut = mux déconnecté ;
//   - MOTEUR_MUX_S0..S3   : entrées de sélection, poids faible -> fort.
#ifndef MOTEUR_IPROPI_MUX_H
#define MOTEUR_IPROPI_MUX_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define IPROPI_MUX_CHANNEL_COUNT MOTEUR_CHANNEL_COUNT

// Configure EN et S0..S3 en sortie, mux désactivé, canal 0 sélectionné.
void ipropi_mux_init(void);

// Sélectionne le canal logique 0..11. Retourne false sans rien changer si le
// canal est hors bornes (gestion d'erreur explicite, jamais de sélection
// silencieusement fausse).
bool ipropi_mux_select(uint8_t channel);

// EN bas : la sortie analogique est connectée au bus ADC.
void ipropi_mux_enable(void);

// EN haut : mux déconnecté (état de repos, bus ADC rendu à l'auto-test).
void ipropi_mux_disable(void);

#endif  // MOTEUR_IPROPI_MUX_H
