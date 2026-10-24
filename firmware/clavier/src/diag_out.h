#ifndef DIAG_OUT_H
#define DIAG_OUT_H

#include <stdbool.h>
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

// Tampon central : 640 octets permettent une ligne `m stats` complète (ADR-0025),
// y compris avec les 18 compteurs et les valeurs u32 maximales.
#define DIAG_OUT_LINE_MAX 640u

#ifndef CLAVIER_DIAG_TRACE
#define CLAVIER_DIAG_TRACE 1
#endif
#ifndef CLAVIER_DIAG_DEFAULT_LEVEL
#define CLAVIER_DIAG_DEFAULT_LEVEL 1
#endif

// Catégories ADR-0025. Une catégorie ne change jamais une décision métier.
typedef enum {
    DIAG_CLASS_ALWAYS = 0,   // divergence, défaut, refus, action physique
    DIAG_CLASS_NORMAL = 1,   // verdict d'une opération explicitement demandée
    DIAG_CLASS_BRINGUP = 2,  // boot, identité, table, événements ordinaires
    DIAG_CLASS_TRACE = 3,    // notes, RAW, matrice, PUSH courants
    DIAG_CLASS_CONSOLE = 4,  // réponse à une commande explicite
} diag_class_t;

// Filtre pur, partagé entre le firmware et les tests hôte (ADR-0025).
static inline bool diag_class_enabled_for_level(diag_class_t category, uint8_t level,
                                                bool trace_compiled) {
    switch (category) {
    case DIAG_CLASS_ALWAYS:
    case DIAG_CLASS_CONSOLE:
    case DIAG_CLASS_NORMAL:
        return true;
    case DIAG_CLASS_BRINGUP:
        return level >= 1u;
    case DIAG_CLASS_TRACE:
        return trace_compiled && level >= 2u;
    default:
        return true;
    }
}

// Initialise le niveau à CLAVIER_DIAG_DEFAULT_LEVEL et l'horodatage à actif.
// À appeler avant toute émission de boot.
void diag_out_init(void);
bool diag_out_set_level(uint8_t level);
uint8_t diag_out_level(void);
bool diag_out_trace_available(void);
bool diag_out_is_enabled(diag_class_t category);
void diag_out_toggle_timestamp(void);
bool diag_out_timestamp_enabled(void);

// Horodatage courant en millisecondes depuis le démarrage. Source unique,
// partagée avec les échéances de `can_link.c` (`to_ms_since_boot`).
uint32_t diag_out_now_ms(void);

// Écrit `line` précédée du préfixe d'horodatage (si actif) et suivie d'un saut
// de ligne. Les lignes NULL ou vides sont ignorées.
void diag_out_line_class(diag_class_t category, const char *line);
void diag_out_printf_class(diag_class_t category, const char *format, ...);

// Compatibilité : sans catégorie explicite, une ligne est ALWAYS. Cela empêche
// qu'un nouveau call site oublie son niveau et disparaisse par accident.
void diag_out_line(const char *line);
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
