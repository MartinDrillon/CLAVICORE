// Format partagé de la table d'amorçage UID -> position (ADR-0003 §7).
//
// Ce fichier ne contient QUE le format et des fonctions pures. Les données
// (valeurs d'UID relevées) vivent uniquement côté Clavier
// (firmware/clavier/src/uid_position.h) : le Moteur n'inclut jamais ces données
// (invariant 2 : cartes anonymes). La table persistante en flash (Phase 4) fait
// foi une fois initialisée (invariant 3) ; cette table n'est qu'une amorce et
// ne corrige jamais rien silencieusement (invariant 1).
//
// Phase 4 : la table flash reprendra uid_position_entry_t comme PRÉFIXE de ses
// enregistrements : ne pas modifier l'ordre ni la taille de cette structure
// sans nouvelle ADR (et incrément de UID_POSITION_FORMAT_VERSION).
#ifndef CLAVICORE_UID_POSITION_FORMAT_H
#define CLAVICORE_UID_POSITION_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UID_POSITION_FORMAT_VERSION 1u
#define UID_POSITION_COUNT 5u

// position == 0xFF : entrée vide.
#define UID_POSITION_EMPTY 0xFFu
// Valeur numérique rapportée (`pos=127`) pour un UID absent de la table.
#define UID_POSITION_UNKNOWN_REPORTED 0x7Fu

// Même disposition que pico_unique_board_id_t (uint8_t id[8]).
typedef struct {
    uint8_t id[8];
} clavicore_uid64_t;

typedef struct {
    clavicore_uid64_t uid;
    uint8_t position;  // 0..UID_POSITION_COUNT-1, ou UID_POSITION_EMPTY
} uid_position_entry_t;

typedef enum {
    UID_POSITION_FOUND = 0,
    UID_POSITION_NOT_FOUND,  // UID absent (table valide)
    UID_POSITION_TABLE_INVALID,  // table invalide : aucune recherche fiable
} uid_position_lookup_t;

// UID tout à 0x00 ou tout à 0xFF : invalide.
static inline bool uid_position_uid_is_valid(const clavicore_uid64_t *uid) {
    bool all_zero = true;
    bool all_ff = true;

    if (uid == NULL) {
        return false;
    }
    for (uint8_t i = 0u; i < 8u; ++i) {
        if (uid->id[i] != 0x00u) {
            all_zero = false;
        }
        if (uid->id[i] != 0xFFu) {
            all_ff = false;
        }
    }
    return !all_zero && !all_ff;
}

static inline bool uid_position_uid_equal(const clavicore_uid64_t *a, const clavicore_uid64_t *b) {
    for (uint8_t i = 0u; i < 8u; ++i) {
        if (a->id[i] != b->id[i]) {
            return false;
        }
    }
    return true;
}

// Nombre d'entrées non vides (position != UID_POSITION_EMPTY).
static inline size_t uid_position_used_count(const uid_position_entry_t *table, size_t count) {
    size_t used = 0u;

    if (table == NULL) {
        return 0u;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (table[i].position != UID_POSITION_EMPTY) {
            ++used;
        }
    }
    return used;
}

// Contrôle de table. Valide si, pour chaque entrée non vide : UID valide,
// position < UID_POSITION_COUNT, et aucun UID en doublon. Une table entièrement
// vide est valide (état normal en Phase 3). Les UID des entrées vides sont ignorés.
// Doublon de position : non contrôlé (la convention de numérotation n'est pas
// figée, ADR-0003 §7).
static inline bool uid_position_table_is_valid(const uid_position_entry_t *table, size_t count) {
    if (table == NULL) {
        return false;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (table[i].position == UID_POSITION_EMPTY) {
            continue;
        }
        if (table[i].position >= UID_POSITION_COUNT || !uid_position_uid_is_valid(&table[i].uid)) {
            return false;
        }
        for (size_t j = i + 1u; j < count; ++j) {
            if (table[j].position != UID_POSITION_EMPTY &&
                uid_position_uid_equal(&table[i].uid, &table[j].uid)) {
                return false;
            }
        }
    }
    return true;
}

// Recherche d'un UID. N'écrit *position qu'en cas de UID_POSITION_FOUND.
static inline uid_position_lookup_t uid_position_lookup(const uid_position_entry_t *table,
                                                        size_t count,
                                                        const clavicore_uid64_t *uid,
                                                        uint8_t *position) {
    if (table == NULL || uid == NULL || position == NULL ||
        !uid_position_table_is_valid(table, count)) {
        return UID_POSITION_TABLE_INVALID;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (table[i].position != UID_POSITION_EMPTY &&
            uid_position_uid_equal(&table[i].uid, uid)) {
            *position = table[i].position;
            return UID_POSITION_FOUND;
        }
    }
    return UID_POSITION_NOT_FOUND;
}

#endif  // CLAVICORE_UID_POSITION_FORMAT_H
