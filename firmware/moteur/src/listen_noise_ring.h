// Anneau de bruit IPROPI (ADR-0032 §2, §4) : module PUR, testable sur PC.
// Une tranche de 1 ms par canal, min/max empaquetés dans UN u32
// (`min << 16 | max`) : l'accès 32 bits aligné est atomique entre les cœurs,
// donc aucune lecture déchirée entre Core 1 (producteur) et Core 0 (lecteur).
//
// 0xFFFFFFFF marque une tranche vide (aucun échantillon) : elle ne compte pas
// dans `slices_valid` et ne participe pas au min/max. `0xFFFF/0x0000` est la
// valeur d'initialisation d'une tranche qui va recevoir un échantillon.
#ifndef MOTEUR_LISTEN_NOISE_RING_H
#define MOTEUR_LISTEN_NOISE_RING_H

#include <stdbool.h>
#include <stdint.h>

#define LISTEN_NOISE_SLICES 256u
#define LISTEN_NOISE_EMPTY 0xFFFFFFFFu

typedef struct {
    uint32_t slice[LISTEN_NOISE_SLICES];  // 0xFFFFFFFF = tranche vide
    // Écrit par le producteur (Core 1), lu par le consommateur (Core 0) :
    // volatile pour forcer une relecture à chaque requête.
    volatile uint32_t last_ms;
} listen_noise_ring_t;  // 1024 o par canal -> 12 288 o pour 12 canaux

// Vide l'anneau ; le premier `record()` à `now_ms` ouvre la tranche courante.
void listen_noise_ring_reset(listen_noise_ring_t *ring, uint32_t now_ms);

// Enregistre un échantillon. Si plusieurs millisecondes ont sauté (trace,
// parcage), les tranches intermédiaires restent vides et ne comptent pas.
void listen_noise_ring_record(listen_noise_ring_t *ring, uint32_t now_ms, uint16_t sample);

// Balaie les `lookback_ms` dernières tranches non vides (limité à 256 ms).
// `*truncated` est posé si la fenêtre demandée dépasse 256 ms.
// Retourne false si aucun échantillon valide (min/max alors à 0), ou argument
// invalide. `*slices_valid` reçoit toujours le nombre de tranches lues.
bool listen_noise_ring_query(const listen_noise_ring_t *ring, uint32_t now_ms,
                             uint16_t lookback_ms, uint16_t *min, uint16_t *max,
                             bool *truncated, uint16_t *slices_valid);

#endif  // MOTEUR_LISTEN_NOISE_RING_H
