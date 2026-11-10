#ifndef CAN_LINK_H
#define CAN_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "diag_log.h"
#include "can_protocol.h"
#include "listen_params.h"
#include "solenoid_profile.h"

typedef enum {
    CAN_LINK_REQUEST_SENT = 0,
    CAN_LINK_REQUEST_BUSY,
    CAN_LINK_REQUEST_TX_FAIL,
} can_link_request_result_t;

typedef struct {
    uint32_t rx_overrun;
    uint32_t rx_ext_or_rtr;
    uint32_t rx_bad_dlc;
    uint32_t rx_bad_value;
    uint32_t rx_unknown_id;
    uint32_t rx_ignored;
    uint32_t can_errors;
    uint32_t tx_fail;
    uint32_t tx_unacked_refused;
    uint32_t reasm_full;
    uint32_t events_dropped;
    uint32_t reports_emitted;
    uint32_t note_sent;
    uint32_t note_dropped;
    uint32_t note_tx_fail;
    uint32_t boot_missing;
    uint32_t boot_unknown;
    uint32_t profile_sync_failed;
    uint32_t profile_divergent;
    // ADR-0016 décision 4 : PROFILE_STATUS non attribuable à une carte attendue.
    uint32_t profile_ack_unattributed;
    // ADR-0017 décision 4 : annonces de boot spontanées (redémarrages de carte).
    uint32_t card_rebooted;
    // ADR-0018 : événements de carte reçus et cas signalés.
    uint32_t card_events;
    uint32_t trigger_blocked;
    uint32_t channel_fault_idle;
} can_link_stats_t;

void can_link_init(void);
void can_link_poll(void);
can_link_request_result_t can_link_request_uid(void);
// Demande explicite d'auto-test (console de bring-up, ADR-0009 §9 livraison B) :
// mêmes refus et même chemin que la trame TRIGGER d'un bouton Moteur.
can_link_request_result_t can_link_request_selftest(void);
// Réglage à chaud du profil d'activation (ADR-0015). `can_link_profile_active()`
// est le dernier profil acquitté par les cartes (au boot : le fichier compilé) ;
// `can_link_profile_compiled()` est `k_solenoid_profile`. `can_link_profile_send()`
// diffuse `profile` aux cartes ; il ne devient actif qu'une fois acquitté, le
// compte rendu étant journalisé par can_link_poll ([PROFILE] reglage: ...).
const solenoid_profile_t *can_link_profile_active(void);
const solenoid_profile_t *can_link_profile_compiled(void);
can_link_request_result_t can_link_profile_send(const solenoid_profile_t *profile);
bool can_link_calibration_enabled(void);
void can_link_set_calibration_enabled(bool enabled);
// Journalisation à la demande : table + correspondance canal/touche.
void can_link_dump_table(void);
void can_link_dump_status(void);
bool can_link_has_valid_table(void);
// Table de commissioning courante en LECTURE SEULE (NULL si invalide). Sert au
// mode REC pour classer une impulsion (UID, canal) sans dupliquer la table.
const commissioning_runtime_t *can_link_commissioning_runtime(void);
// Position d'une carte à partir de son uid_tag (false si absente de la table).
bool can_link_position_for_uid_tag(uint16_t uid_tag, uint8_t *position);
bool can_link_selftest_active(void);
bool can_link_note_on(uint8_t midi_note, uint8_t velocity);
bool can_link_note_off(uint8_t midi_note);
bool can_link_maintenance_push_start(uint8_t midi_note);
bool can_link_maintenance_push_stop(uint8_t midi_note, bool timed_out);
void can_link_set_note_activity(bool any_note_active, uint32_t last_note_event_ms);
can_link_stats_t can_link_stats(void);
// ADR-0020 §2 / ADR-0025 §4 : demande le balayage des compteurs des cartes
// présentes (`m stats`). Mêmes refus que la requête d'UID ; refuse aussi si
// aucune carte n'est attendue. Sans effet si un balayage est déjà en cours.
can_link_request_result_t can_link_stats_request_start(void);

// --- Mode REC : écoute IPROPI (ADR-0032) -----------------------------------
// Le Clavier est l'unique maître : il envoie le jeu complet de paramètres
// (18 x LISTEN_PARAM) puis LISTEN_CONTROL avec le CRC du jeu. Aucune valeur
// d'écoute n'est compilée côté Moteur.
bool can_link_profile_sync_active(void);
// Diffuse les 18 LISTEN_PARAM (1 ms d'intervalle). Ne démarre pas l'écoute.
bool can_link_listen_send_params(const listen_params_t *params);
// Envoie le jeu complet puis LISTEN_CONTROL `mode`. Pour TRACE/STREAM,
// `target_uid_tag` et `channel` désignent la carte et le canal ; en LISTEN,
// les deux valent 0.
bool can_link_listen_start(const listen_params_t *params, can_listen_mode_t mode,
                           uint16_t target_uid_tag, uint8_t channel);
// Envoie LISTEN_CONTROL OFF. Non bloquant : can_link_poll attend le STATUS IDLE
// (20 ms) et journalise `[LISTEN] arret sans reponse` à l'échéance.
bool can_link_listen_stop(void);
bool can_link_listen_trace(const listen_params_t *params, uint16_t uid_tag, uint8_t channel);
bool can_link_listen_stream(const listen_params_t *params, uint16_t uid_tag, uint8_t channel);
// STREAM_OFF : revient au balayage sans quitter l'écoute.
bool can_link_listen_stream_off(void);
bool can_link_listen_noise_query(uint16_t uid_tag, uint8_t channel, uint8_t request_id,
                                 uint16_t lookback_ms);
// Vrai entre un démarrage d'écoute réussi et son arrêt (ou son échec).
bool can_link_listen_is_active(void);

typedef enum {
    CAN_LINK_LISTEN_PULSE = 0,
    CAN_LINK_LISTEN_STATUS,
    CAN_LINK_LISTEN_NOISE,
    CAN_LINK_LISTEN_TRACE_FRAME,
    // Extension Clavier : nécessaire pour corréler durée et requête `e bruit`
    // (la fiche de lot omet ce membre, la trame 0x428 existe au §1.4).
    CAN_LINK_LISTEN_PULSE_TIMING,
} can_link_listen_event_kind_t;

typedef struct {
    can_link_listen_event_kind_t kind;
    union {
        can_listen_pulse_t pulse;
        can_listen_status_t status;
        can_listen_noise_report_t noise;
        can_listen_trace_t trace;
        can_listen_pulse_timing_t timing;
    } u;
    uint16_t source_uid_tag;  // attribué à la réception (0x429 : cible courante)
} can_link_listen_event_t;

// File bornée (64 événements) ; le débordement est compté dans
// stats.events_dropped, jamais silencieux.
bool can_link_listen_take_event(can_link_listen_event_t *out);

#endif
