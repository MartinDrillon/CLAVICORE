#ifndef DIAG_OUT_H
#define DIAG_OUT_H

#include <stddef.h>
#include <stdint.h>

// Journal de diagnostic du Clavier (ADR-0017).
//
// Toute ligne du journal CDC passe par ce module : il ajoute le préfixe
// d'horodatage monotone `[T+123456 ms] ` puis écrit la ligne telle quelle. Le
// contenu APRÈS le préfixe reste le format `[TAG] clé=valeur` d'ADR-0005 :
// rien d'autre n'est transformé ici.
//
// C'est le SEUL point d'injection de l'horodatage : les formateurs de
// `diag_log.c` restent purs (aucune horloge, aucun accès matériel) pour rester
// compilables et testables sur hôte (ADR-0017 IMP-002).

// Marge suffisante pour DIAG_LOG_LINE_MAX (256) et les lignes construites à la
// volée par la console.
#define DIAG_OUT_LINE_MAX 288u

// Horodatage courant en millisecondes depuis le démarrage. Source unique,
// partagée avec les échéances de `can_link.c` (`to_ms_since_boot`).
uint32_t diag_out_now_ms(void);

// Écrit `line` précédée du préfixe d'horodatage et suivie d'un saut de ligne.
// Les lignes NULL ou vides sont ignorées (même contrat que `emit_line`).
void diag_out_line(const char *line);

// Même chose pour une ligne construite à la volée (interface de `printf`).
// Une ligne trop longue est tronquée, jamais supprimée : une ligne manquante
// serait plus grave qu'une ligne courte dans un journal de diagnostic.
void diag_out_printf(const char *format, ...);

// Identité du binaire, pour la ligne de boot (ADR-0017 décision 2). Le build
// peut définir `CLAVIER_BUILD_ID` ; à défaut, date et heure de compilation.
// Sert uniquement à vérifier ce qui est flashé : jamais interprété.
const char *diag_out_build_id(void);

// Version du firmware Clavier, annoncée au boot (ADR-0017 décision 2).
#define CLAVIER_FW_MAJOR 0u
#define CLAVIER_FW_MINOR 1u
#define CLAVIER_FW_PATCH 0u

#endif
