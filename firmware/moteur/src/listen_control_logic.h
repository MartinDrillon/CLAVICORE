// Politique pure de réception d'un LISTEN_CONTROL (ADR-0032 §3), testable sur
// hôte sans SDK ni état global. `can_link.c` applique l'action retournée.
#ifndef MOTEUR_LISTEN_CONTROL_LOGIC_H
#define MOTEUR_LISTEN_CONTROL_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#include "can_protocol.h"

typedef enum {
    LISTEN_CONTROL_APPLY = 0,    // hors écoute : valider le staging puis appliquer
    LISTEN_CONTROL_REUSE,        // CRC du jeu déjà appliqué : changer de mode seul
    LISTEN_CONTROL_REFUSE,       // CRC différent pendant l'écoute : PARAM_MISMATCH
    LISTEN_CONTROL_STAY_LISTEN,  // TRACE/STREAM ciblant une autre carte : ignorer
} listen_control_action_t;

static inline listen_control_action_t listen_control_action(can_listen_mode_t mode,
                                                            uint16_t target_uid_tag,
                                                            uint16_t board_tag,
                                                            bool listen_active,
                                                            uint16_t applied_crc,
                                                            uint16_t control_crc) {
    // Une trame TRACE/STREAM ciblée ne concerne que la carte nommée : les
    // autres continuent leur balayage (LISTEN), elles ne s'arrêtent jamais.
    if ((mode == CAN_LISTEN_MODE_TRACE || mode == CAN_LISTEN_MODE_STREAM) &&
        target_uid_tag != 0u && target_uid_tag != board_tag) {
        return LISTEN_CONTROL_STAY_LISTEN;
    }
    if (!listen_active) {
        return LISTEN_CONTROL_APPLY;
    }
    // En cours d'écoute, seul le jeu déjà appliqué au Core 1 est acceptable ;
    // il n'est alors pas réappliqué (contrat « OFF seulement », ADR-0032 §2).
    return (control_crc == applied_crc) ? LISTEN_CONTROL_REUSE : LISTEN_CONTROL_REFUSE;
}

#endif  // MOTEUR_LISTEN_CONTROL_LOGIC_H
