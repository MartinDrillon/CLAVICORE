#ifndef MOTEUR_BOARD_IDENTITY_H
#define MOTEUR_BOARD_IDENTITY_H

// Identité de la carte : UID 64 bits fourni par le SDK (pico_get_unique_board_id).
// Les cartes Moteur sont anonymes (invariant 2) : l'UID est leur seule identité.
// Jamais de reconstruction maison de l'UID : on utilise la sortie du SDK telle quelle.

#include <stdbool.h>
#include <stdint.h>

#define BOARD_IDENTITY_UID_SIZE 8u

// Copie l'UID dans uid[8], dans l'ordre id[0]..id[7] du SDK (= ordre de
// pico_get_unique_board_id_string et ordre sur le fil CAN).
// Retourne false (uid mis à zéro) si l'UID est invalide : pointeur nul, tout à
// 0x00 ou tout à 0xFF.
bool board_identity_read(uint8_t uid[BOARD_IDENTITY_UID_SIZE]);

#endif
