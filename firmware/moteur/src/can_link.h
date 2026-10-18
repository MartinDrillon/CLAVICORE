#ifndef MOTEUR_CAN_LINK_H
#define MOTEUR_CAN_LINK_H

// Liaison CAN côté Moteur (ADR-0002, ADR-0003) : wrapper can2040.
//
// Phase 3 : réponse à UID_REQUEST + annonce spontanée au boot (BOARD_ANNOUNCE
// trames A+B), avec délai pseudo-aléatoire dérivé de l'UID. AUCUNE commande de
// solénoïde ne passe par le CAN en Phase 3 (invariant 4).
//
// Contraintes :
//  - le callback can2040 (IRQ) ne fait que copier dans un anneau SPSC ;
//  - can_link_poll() est NON BLOQUANT et à travail borné : il ne doit jamais
//    retarder solenoid_driver_tick() ;
//  - aucune émission depuis le callback.

#include <stdbool.h>
#include <stdint.h>

#include "board_identity.h"

typedef struct {
    uint32_t rx_overrun;      // anneau RX plein (trames perdues)
    uint32_t rx_ext_or_rtr;   // trames étendues / RTR ignorées
    uint32_t rx_bad_dlc;      // DLC inattendu pour un ID connu
    uint32_t rx_bad_value;    // champ hors plage pour un ID connu
    uint32_t rx_unknown_id;   // ID réservé, inconnu ou interdit (jamais interprété)
    uint32_t rx_ignored;      // ID défini mais sans objet pour le Moteur
    uint32_t can_errors;      // notifications d'erreur can2040
    uint32_t tx_fail;         // can2040_transmit en échec (trame abandonnée)
    uint32_t announces_sent;  // annonces dont A et B ont été mises en file
    uint32_t requests_served; // UID_REQUEST valides reçues
} can_link_stats_t;

// Initialise can2040 (PIO0, PIO0_IRQ_0, GPIO config.h, CAN_BITRATE_HZ) et arme
// l'annonce de boot (reply_to = 0) après le délai dérivé de l'UID.
// `uid` doit être un UID valide (voir board_identity_read).
void can_link_init(const uint8_t uid[BOARD_IDENTITY_UID_SIZE], uint32_t now_us);

// À appeler à chaque tour de boucle principale. Travail borné, non bloquant.
void can_link_poll(uint32_t now_us);

can_link_stats_t can_link_stats(void);

#endif
