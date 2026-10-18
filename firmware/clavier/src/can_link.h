#ifndef CAN_LINK_H
#define CAN_LINK_H

// Liaison CAN côté Clavier (ADR-0002, ADR-0003) : wrapper can2040.
//
// Contraintes :
//  - le callback can2040 (contexte IRQ) ne fait QUE copier la trame dans un
//    anneau SPSC ; aucun parsing, aucune émission depuis le callback ;
//  - tout le traitement (parsing shared/protocol, réassemblage A+B, timeouts)
//    se fait en boucle principale via can_link_poll() ;
//  - une seule UID_REQUEST en vol à la fois.
//
// Les résultats sont fournis sous forme de messages SysEx déjà encodés, dans
// une petite file ; main.c les transmet à midi_send_sysex().

#include <stdbool.h>
#include <stdint.h>

#include "sysex_uid.h"

typedef struct {
    uint8_t len;
    uint8_t data[SYSEX_MAX_LEN];
} can_link_sysex_t;

typedef enum {
    CAN_LINK_REQUEST_SENT = 0,
    CAN_LINK_REQUEST_BUSY,     // une requête est déjà en vol (STATUS BUSY mis en file)
    CAN_LINK_REQUEST_TX_FAIL,  // file can2040 pleine ou trame précédente non acquittée
} can_link_request_result_t;

typedef struct {
    uint32_t rx_overrun;         // anneau RX plein (trames perdues)
    uint32_t rx_ext_or_rtr;      // trames étendues / RTR ignorées
    uint32_t rx_bad_dlc;         // DLC inattendu pour un ID connu
    uint32_t rx_bad_value;       // champ hors plage pour un ID connu
    uint32_t rx_unknown_id;      // ID réservé, inconnu ou interdit
    uint32_t rx_ignored;         // ID défini mais sans objet pour le Clavier
    uint32_t can_errors;         // notifications d'erreur can2040
    uint32_t tx_fail;            // can2040_transmit en échec
    uint32_t tx_unacked_refused; // requêtes refusées : trame précédente non acquittée
    uint32_t reasm_full;         // table de réassemblage pleine (trame perdue)
    uint32_t events_dropped;     // file de SysEx pleine
    uint32_t reports_emitted;
} can_link_stats_t;

// Initialise can2040 : PIO0, PIO0_IRQ_0 (priorité la plus haute), GPIO RX=0 TX=1,
// CAN_BITRATE_HZ.
void can_link_init(void);

// À appeler à chaque tour de boucle principale, AVANT l'émission SysEx :
// vide l'anneau RX, réassemble, gère les timeouts.
void can_link_poll(void);

// Émet une UID_REQUEST (appui sur la touche GET_UID). Ne bloque jamais.
can_link_request_result_t can_link_request_uid(void);

// File de SysEx à émettre : peek ne retire pas, pop retire après acceptation par midi.
bool can_link_peek_sysex(can_link_sysex_t *out);
void can_link_pop_sysex(void);

// Instantané des compteurs de diagnostic.
can_link_stats_t can_link_stats(void);

#endif