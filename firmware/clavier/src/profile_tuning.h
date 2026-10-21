// Réglage à chaud du profil d'activation par la console CDC (ADR-0015).
//
// Logique PURE (aucune dépendance au SDK, testable sur PC dans `tests/host/`) :
// table des champs du `solenoid_profile_t`, analyse d'affectations façon
// initialiseur C (`.pull_duration_us = 12000u,`) ou `nom valeur`, application
// atomique sur une copie, mise en forme pour recopie dans
// `solenoid_profile_config.h`.
//
// Règles :
//  - une ligne est appliquée EN BLOC : si une seule affectation est refusée, le
//    profil fourni n'est pas modifié (jamais d'application partielle) ;
//  - seuls les contrôles STRUCTURELS de `solenoid_profile_validate()` sont
//    appliqués (ADR-0014 : aucune borne de sécurité côté firmware) ;
//  - les champs dont la modification à chaud n'a pas d'effet fiable côté Moteur
//    sont verrouillés avec une raison explicite (voir la table dans le .c).
#ifndef CLAVIER_PROFILE_TUNING_H
#define CLAVIER_PROFILE_TUNING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "solenoid_profile.h"

#define PROFILE_TUNING_NAME_MAX 32u

typedef enum {
    PROFILE_TUNING_OK = 0,
    PROFILE_TUNING_ERR_EMPTY,          // aucune affectation dans la ligne
    PROFILE_TUNING_ERR_SYNTAX,         // nom de champ absent ou ligne mal formée
    PROFILE_TUNING_ERR_UNKNOWN_FIELD,  // champ inexistant
    PROFILE_TUNING_ERR_LOCKED,         // champ non réglable à chaud
    PROFILE_TUNING_ERR_NUMBER,         // valeur absente, non numérique ou > 32 bits
    PROFILE_TUNING_ERR_RANGE,          // valeur trop grande pour le type du champ
    PROFILE_TUNING_ERR_STRUCTURE,      // profil résultant incohérent (voir `structure`)
} profile_tuning_result_t;

typedef struct {
    size_t assigned;                       // affectations analysées (si OK : appliquées)
    char field[PROFILE_TUNING_NAME_MAX];   // champ concerné par l'erreur, sinon vide
    uint32_t range_max;                    // pour ERR_RANGE : valeur maximale du type
    solenoid_profile_result_t structure;   // pour ERR_STRUCTURE
} profile_tuning_report_t;

// Analyse `text` et applique les affectations à `profile`. En cas d'erreur,
// `profile` est laissé INCHANGÉ. `report` peut être NULL.
profile_tuning_result_t profile_tuning_apply(const char *text, solenoid_profile_t *profile,
                                             profile_tuning_report_t *report);

// Texte court (français) d'un résultat, avec le détail éventuel de `report`.
// Écrit dans `out` (tronqué à `capacity`), retourne `out`.
const char *profile_tuning_describe(profile_tuning_result_t result,
                                    const profile_tuning_report_t *report, char *out,
                                    size_t capacity);

// --- Table des champs --------------------------------------------------------

size_t profile_tuning_field_count(void);
const char *profile_tuning_field_name(size_t index);
bool profile_tuning_field_is_tunable(size_t index);
// Raison du verrouillage ; NULL si le champ est réglable à chaud.
const char *profile_tuning_field_lock_reason(size_t index);
uint32_t profile_tuning_field_value(const solenoid_profile_t *profile, size_t index);
bool profile_tuning_find(const char *name, size_t *index_out);

// Écrit `.nom = valeurs,` (suffixe `u`, comme dans solenoid_profile_config.h).
// Retourne le nombre de caractères écrits (hors NUL), 0 si `capacity` est trop petit.
size_t profile_tuning_format_field(const solenoid_profile_t *profile, size_t index, char *out,
                                   size_t capacity);

// Nombre de champs dont la valeur diffère entre `a` et `b`.
size_t profile_tuning_diff_count(const solenoid_profile_t *a, const solenoid_profile_t *b);

#endif  // CLAVIER_PROFILE_TUNING_H
