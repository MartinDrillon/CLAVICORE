// Écoute IPROPI sur le second Cortex-M33 (ADR-0032 §1-§3) : acquisition Core 1,
// détecteurs, anneaux SPSC Core 1 -> Core 0, trace et stream.
//
// Frontière stricte : le Core 1 ne fait NI CAN, NI journal, NI solénoïde. Il
// publie des événements dans des anneaux à producteur unique / consommateur
// unique, vidés par la boucle principale du Core 0. Aucune valeur d'écoute
// n'est compilée ici : `listen_params_t` arrive du Clavier (CRC vérifié côté
// Core 0) et reste en RAM.
#ifndef MOTEUR_LISTEN_CORE1_H
#define MOTEUR_LISTEN_CORE1_H

#include <stdbool.h>
#include <stdint.h>

#include "can_protocol.h"
#include "listen_params.h"
#include "solenoid_driver.h"

#define LISTEN_PULSE_RING_SIZE 64u     // puissance de 2
#define LISTEN_STATUS_RING_SIZE 8u     // puissance de 2
#define LISTEN_STREAM_RING_SIZE 256u   // échantillons, puissance de 2
#define LISTEN_TRACE_PRE_TRIGGER 1024u // 25 % de 4096

typedef struct {  // publié par Core 1, consommé par Core 0
    uint8_t channel;
    uint8_t seq;
    uint16_t base;
    uint16_t peak;
    uint32_t t_peak_us;
    uint32_t t_end_us;
    bool split;
} listen_pulse_evt_t;

typedef struct {
    can_listen_state_t state;
    uint8_t refused_param_id;
    uint16_t sweep_period_us;
    uint8_t overflows;
} listen_status_evt_t;

// Prépare les structures partagées. Ne lance PAS le Core 1 (voir main.c).
void listen_core1_init(const uint8_t uid[CAN_UID_SIZE], uint16_t uid_tag,
                       solenoid_driver_t *drivers, uint8_t driver_count);

// Démarre la boucle Core 1 (multicore_launch_core1). Idempotent.
void listen_core1_launch(void);

// Vrai si un mode d'écoute est demandé (OFF sinon).
bool listen_core1_active(void);

// Applique le jeu d'écoute. N'a d'effet qu'en mode OFF (contrat ADR-0032 §2).
// Retourne false si le jeu n'a pas été appliqué (arguments invalides ou mode
// non OFF) : l'appelant doit alors refuser le démarrage, jamais l'ignorer.
bool listen_core1_apply_params(const listen_params_t *params, uint16_t crc);

// CRC du jeu actuellement appliqué au Core 1 (0 si aucun jeu valide).
uint16_t listen_core1_params_crc(void);

// Change le mode. STREAM_OFF revient au balayage sans quitter l'écoute.
// Refuse un mode inconnu, un canal cible invalide, ou un démarrage sans jeu
// de paramètres validé. Un seul écrivain : Core 0.
bool listen_core1_set_mode(can_listen_mode_t mode, uint8_t channel, uint16_t target_uid_tag);

// Core 0, chaque tour : canaux non IDLE (masque des solénoïdes pilotés).
void listen_core1_set_drive_mask(uint16_t non_idle_mask);

// Attend que le Core 1 soit parqué (mux rendu) après un passage OFF.
bool listen_core1_wait_parked(uint32_t timeout_us);

bool listen_core1_take_pulse(listen_pulse_evt_t *out);
bool listen_core1_take_status(listen_status_evt_t *out);
bool listen_core1_take_stream(uint16_t *out);  // échantillon décimé
bool listen_core1_take_noise(uint8_t channel, uint16_t lookback_ms, uint16_t *min,
                             uint16_t *max, bool *truncated);

uint32_t listen_core1_sweep_period_us(void);
uint8_t listen_core1_overflow_count(void);

// Trace : buffer prêt -> Core 0 lit 4096 échantillons puis libère.
bool listen_core1_trace_ready(void);
uint16_t listen_core1_trace_sample(uint32_t index);
void listen_core1_trace_release(void);

#endif  // MOTEUR_LISTEN_CORE1_H
