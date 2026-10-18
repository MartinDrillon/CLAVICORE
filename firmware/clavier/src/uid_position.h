#ifndef UID_POSITION_H
#define UID_POSITION_H

// Table d'amorçage UID -> position (ADR-0003 §7). Données propres au Clavier :
// le Moteur n'inclut jamais ce fichier (invariant 2).
// Ce n'est PAS la source de vérité (invariant 3) : la table de commissioning
// persistante (Phase 4) la remplacera.
//
// Chaîne CAN : nœud 1 = Clavier (absent de cette table), nœuds 2..6 = les 5 cartes
// Moteur. Convention provisoire : position = numéro de nœud - 2 (0..4), de l'aigu
// vers le grave ; la position 4 (nœud 6) porte les notes les plus graves.
// UID relevés avec tests/can_phase3_uid.md (ordre id[0]..id[7]). Entrée vide :
// {{{0}}, UID_POSITION_EMPTY}. Le rattachement UID -> position se vérifie avec le
// SysEx UID_REPORT (champ position) : 0x7F = UID absent de la table.

#include "uid_position_format.h"

static const uid_position_entry_t uid_position_table[UID_POSITION_COUNT] = {
    {{{0}}, UID_POSITION_EMPTY},  // position 0 : nœud 2
    {{{0}}, UID_POSITION_EMPTY},  // position 1 : nœud 3
    {{{0}}, UID_POSITION_EMPTY},  // position 2 : nœud 4
    {{{0}}, UID_POSITION_EMPTY},  // position 3 : nœud 5
    {{{0xA3, 0xFA, 0x4C, 0x99, 0xCA, 0x52, 0x6F, 0x4B}}, 4},  // position 4 : nœud 6 (graves)
};

#endif
