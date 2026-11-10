// Jeu canonique des paramètres d'écoute IPROPI (ADR-0032 §5).
//
// C pur, sans SDK. Ce module ne compile AUCUNE valeur d'écoute : le Clavier
// détient `k_listen_config` et envoie le jeu complet avant chaque démarrage ;
// le Moteur ne garde que la RAM. Les défauts de référence vivent donc côté
// Clavier (firmware/clavier/src/listen_config.h), jamais ici.
//
// `listen_params_validate()` applique des contrôles STRUCTURELS (cohérence du
// jeu), rejetés en bloc et jamais écrêtés. Aucun champ n'est une limite
// électrique : `settle_us`, `drive_blank_ms` et les seuils règlent l'écoute.
#ifndef CLAVICORE_LISTEN_PARAMS_H
#define CLAVICORE_LISTEN_PARAMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_protocol.h"

#define LISTEN_PARAMS_COUNT 18u
#define LISTEN_PARAMS_WIRE_SIZE (LISTEN_PARAMS_COUNT * 4u)  // 72 octets

typedef enum {
    LISTEN_PARAM_INVALID = 0,
    LISTEN_PARAM_SETTLE_US = 1,
    LISTEN_PARAM_SAMPLES_PER_DWELL = 2,
    LISTEN_PARAM_BASELINE_INIT_MS = 3,
    LISTEN_PARAM_BASELINE_TAU_MS = 4,
    LISTEN_PARAM_RISE_THR = 5,
    LISTEN_PARAM_RETURN_THR = 6,
    LISTEN_PARAM_RETURN_HOLD_US = 7,
    LISTEN_PARAM_SPLIT_PCT = 8,
    LISTEN_PARAM_MIN_PULSE_US = 9,
    LISTEN_PARAM_DRIVE_BLANK_MS = 10,
    LISTEN_PARAM_TRACE_PERIOD_US = 11,
    LISTEN_PARAM_TRACE_TRIG = 12,
    LISTEN_PARAM_TRACE_TIMEOUT_MS = 13,
    LISTEN_PARAM_STREAM_DECIM = 14,
    LISTEN_PARAM_VEL_FIXED = 15,
    LISTEN_PARAM_VEL_MIN_DELTA = 16,
    LISTEN_PARAM_VEL_MAX_DELTA = 17,
    LISTEN_PARAM_DETAIL = 18,
} listen_param_id_t;

typedef struct {
    uint32_t settle_us;          // attente mux après commutation (écoute)
    uint32_t samples_per_dwell;  // conversions moyennées par canal (1..64)
    uint32_t baseline_init_ms;   // fenêtre de base initiale
    uint32_t baseline_tau_ms;    // constante de temps du suivi lent
    uint32_t rise_thr;           // seuil de début (points au-dessus de la base)
    uint32_t return_thr;         // seuil de retour (< rise_thr)
    uint32_t return_hold_us;     // maintien du retour (0 = instantané)
    uint32_t split_pct;          // creux de trille (1..99 %)
    uint32_t min_pulse_us;       // impulsion minimale rapportée
    uint32_t drive_blank_ms;     // masquage après pilotage
    uint32_t trace_period_us;    // période de capture (>= 2)
    uint32_t trace_trig;         // déclenchement de trace
    uint32_t trace_timeout_ms;   // sans déclenchement (>= 1, défaut 10000)
    uint32_t stream_decim;       // un échantillon sur N (>= 1)
    uint32_t vel_fixed;          // vélocité fixe (1..127)
    uint32_t vel_min_delta;      // 0/0 = vélocité fixe
    uint32_t vel_max_delta;
    uint32_t detail;             // 1 = émet LISTEN_PULSE_TIMING
} listen_params_t;

typedef enum {
    LISTEN_PARAMS_OK = 0,
    LISTEN_PARAMS_ERR_ARG,
    LISTEN_PARAMS_ERR_FIELD,  // *bad_id désigne le champ fautif
} listen_params_result_t;

// Nombre de champs du jeu canonique (18).
size_t listen_params_field_count(void);
// Nom du champ `id` ("settle_us"...), NULL si id hors 1..18.
const char *listen_params_field_name(uint8_t id);
// Recherche inverse ; false si le nom est inconnu ou l'argument NULL.
bool listen_params_field_id(const char *name, uint8_t *id);
// Accès par id ; seuls les ids 1..18 sont acceptés, la valeur n'est pas jugée.
bool listen_params_get(const listen_params_t *p, uint8_t id, uint32_t *out);
bool listen_params_set(listen_params_t *p, uint8_t id, uint32_t value);
// Valide le jeu complet ; *bad_id reçoit le premier champ fautif (0 si OK).
listen_params_result_t listen_params_validate(const listen_params_t *p, uint8_t *bad_id);
// 18 u32 little-endian dans l'ordre des ids 1..18 ; retourne 72, ou 0 si NULL.
size_t listen_params_serialize(const listen_params_t *p,
                               uint8_t out[LISTEN_PARAMS_WIRE_SIZE]);
// can_crc16_ccitt() de la forme sérialisée du jeu (0xFFFF si p NULL).
uint16_t listen_params_crc16(const listen_params_t *p);

#endif  // CLAVICORE_LISTEN_PARAMS_H
