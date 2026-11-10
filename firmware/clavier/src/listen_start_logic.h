// Politique pure de démarrage/relance de l'écoute (ADR-0032 §3), testable sur
// hôte : le jeu de 18 LISTEN_PARAM n'est rediffusé que s'il diffère de celui
// déjà envoyé au Moteur.
#ifndef CLAVIER_LISTEN_START_LOGIC_H
#define CLAVIER_LISTEN_START_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LISTEN_START_SEND_PARAMS = 0,  // pas d'écoute en cours : diffuser le jeu
    LISTEN_START_SKIP_PARAMS,      // même CRC déjà envoyé : contrôle seul
    LISTEN_START_STOP_FIRST,       // CRC différent en écoute : OFF, jeu, contrôle
} listen_start_action_t;

static inline listen_start_action_t listen_start_action(bool listen_active,
                                                        uint16_t sent_crc,
                                                        uint16_t wanted_crc) {
    if (!listen_active) {
        return LISTEN_START_SEND_PARAMS;
    }
    return (sent_crc == wanted_crc) ? LISTEN_START_SKIP_PARAMS : LISTEN_START_STOP_FIRST;
}

// Un OFF de STOP_FIRST est diffusé : si le contrôle final est ciblé
// (TRACE/STREAM), les cartes non ciblées resteraient OFF et ignoreraient ce
// contrôle. Il faut d'abord un LISTEN diffusé (même CRC) pour les remettre au
// balayage ; le contrôle ciblé ne fait ensuite que changer le mode de sa cible.
static inline bool listen_start_needs_listen_relay(listen_start_action_t action,
                                                   bool targeted_control) {
    return action == LISTEN_START_STOP_FIRST && targeted_control;
}

#endif  // CLAVIER_LISTEN_START_LOGIC_H
