#ifndef UID_POSITION_H
#define UID_POSITION_H

// Table d'amorçage UID -> position (ADR-0003 §7). Données propres au Clavier :
// le Moteur n'inclut jamais ce fichier (invariant 2).
// Ce n'est PAS la source de vérité (invariant 3) : la table de commissioning
// persistante (Phase 4) la remplacera.
//
// Chaîne CAN : nœud 1 = Clavier (absent de cette table), nœuds 2..6 = les 5 cartes
// Moteur. Convention : position = numéro de nœud - 2 (0..4) ; la position 0 porte
// les touches les plus GRAVES (c'est donc la carte qui joue DO2), la position la
// plus haute (4) les plus AIGUËS.
// Règle d'assignation ascendante (ADR-0010) : position 0, canal occupé le plus
// bas -> touche 1 = DO2 (MIDI 36) ; les blocs montent dans l'ordre des canaux
// puis des positions, les touches non attribuées restant du côté aigu. Une
// configuration dont les positions présentes ne forment pas 0..K-1 (position 0
// absente, ou trou de position) reste jouable mais est signalée PROVISIONAL avec
// le motif ANCHOR_SHIFT (0x40) : jamais de remappage silencieux (invariant 1).
// UID relevés avec tests/can_phase3_uid.md (ordre id[0]..id[7]). Entrée vide :
// {{{0}}, UID_POSITION_EMPTY}. Le rattachement UID -> position se vérifie avec le
// Ligne [UID] (champ pos) : 0x7F = UID absent de la table.

#include "uid_position_format.h"

static const uid_position_entry_t uid_position_table[UID_POSITION_COUNT] = {
    {{{0}}, UID_POSITION_EMPTY},  // position 0 : nœud 2 — carte GRAVE, à renseigner (c'est elle qui joue DO2)
    {{{0x8F, 0x09, 0x25, 0x95, 0x73, 0x32, 0xC7, 0xDA}}, 1},  // position 1 : nœud 3
    {{{0xD3, 0xFE, 0xC9, 0x4E, 0x4E, 0x95, 0xA1, 0xBB}}, 2},  // position 2 : nœud 4
    {{{0x05, 0xA5, 0x97, 0xF4, 0x7A, 0x4D, 0xA9, 0x2E}}, 3},  // position 3 : nœud 5
    {{{0xA3, 0xFA, 0x4C, 0x99, 0xCA, 0x52, 0x6F, 0x4B}}, 4},  // position 4 : nœud 6 (aigus)
};

#endif
