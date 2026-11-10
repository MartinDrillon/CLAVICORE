// Règles pures de relance de l'écoute ADR-0032 §3 (régression « e trace /
// e stream en REC ») : le jeu LISTEN_PARAM n'est rediffusé que s'il diffère,
// et une trame TRACE/STREAM ciblée ne concerne que la carte nommée.
// La couche CAN elle-même reste couverte par la procédure de banc.
#include <stdio.h>

#include "can_protocol.h"
#include "listen_control_logic.h"
#include "listen_start_logic.h"

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("ECHEC %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                    \
    } while (0)

#define BOARD_TAG 0x1234u
#define OTHER_TAG 0x5678u
#define CRC_A 0xAAAAu
#define CRC_B 0xBBBBu

static void test_moteur_apply_hors_ecoute(void) {
    // Hors écoute : le jeu doit être validé puis appliqué avant le mode.
    CHECK(listen_control_action(CAN_LISTEN_MODE_LISTEN, 0u, BOARD_TAG, false, 0u, CRC_A) ==
          LISTEN_CONTROL_APPLY);
    CHECK(listen_control_action(CAN_LISTEN_MODE_TRACE, BOARD_TAG, BOARD_TAG, false, 0u,
                                CRC_A) == LISTEN_CONTROL_APPLY);
}

static void test_moteur_reuse_meme_crc(void) {
    // Écoute active + même CRC : pas de réapplication (la cause du défaut).
    CHECK(listen_control_action(CAN_LISTEN_MODE_TRACE, BOARD_TAG, BOARD_TAG, true, CRC_A,
                                CRC_A) == LISTEN_CONTROL_REUSE);
    CHECK(listen_control_action(CAN_LISTEN_MODE_STREAM, BOARD_TAG, BOARD_TAG, true, CRC_A,
                                CRC_A) == LISTEN_CONTROL_REUSE);
    CHECK(listen_control_action(CAN_LISTEN_MODE_LISTEN, 0u, BOARD_TAG, true, CRC_A, CRC_A) ==
          LISTEN_CONTROL_REUSE);
}

static void test_moteur_refus_crc_different(void) {
    // Écoute active + CRC différent : refus nommé, jamais de substitution.
    CHECK(listen_control_action(CAN_LISTEN_MODE_TRACE, BOARD_TAG, BOARD_TAG, true, CRC_A,
                                CRC_B) == LISTEN_CONTROL_REFUSE);
    CHECK(listen_control_action(CAN_LISTEN_MODE_LISTEN, 0u, BOARD_TAG, true, CRC_A, CRC_B) ==
          LISTEN_CONTROL_REFUSE);
}

static void test_moteur_carte_non_ciblee(void) {
    // Une autre carte est ciblée : elle reste en LISTEN, quel que soit le CRC.
    CHECK(listen_control_action(CAN_LISTEN_MODE_TRACE, OTHER_TAG, BOARD_TAG, true, CRC_A,
                                CRC_A) == LISTEN_CONTROL_STAY_LISTEN);
    CHECK(listen_control_action(CAN_LISTEN_MODE_STREAM, OTHER_TAG, BOARD_TAG, true, CRC_A,
                                CRC_A) == LISTEN_CONTROL_STAY_LISTEN);
    CHECK(listen_control_action(CAN_LISTEN_MODE_TRACE, OTHER_TAG, BOARD_TAG, true, CRC_A,
                                CRC_B) == LISTEN_CONTROL_STAY_LISTEN);
    // Mais une cible nulle n'est pas « une autre carte » : le refus reste nommé
    // par listen_core1_set_mode, jamais silencieux.
    CHECK(listen_control_action(CAN_LISTEN_MODE_TRACE, 0u, BOARD_TAG, true, CRC_A, CRC_A) ==
          LISTEN_CONTROL_REUSE);
    // LISTEN est diffusé : l'uid cible ne doit pas écarter la carte.
    CHECK(listen_control_action(CAN_LISTEN_MODE_LISTEN, OTHER_TAG, BOARD_TAG, true, CRC_A,
                                CRC_A) == LISTEN_CONTROL_REUSE);
}

static void test_clavier_relance(void) {
    // Défaut d'origine : en REC, e trace renvoyait le jeu -> 18 refus bruyants.
    CHECK(listen_start_action(true, CRC_A, CRC_A) == LISTEN_START_SKIP_PARAMS);
    CHECK(listen_start_action(true, CRC_A, CRC_B) == LISTEN_START_STOP_FIRST);
    CHECK(listen_start_action(false, CRC_A, CRC_A) == LISTEN_START_SEND_PARAMS);
    // `e set` hors REC : aucun jeu envoyé jusque-là (CRC 0).
    CHECK(listen_start_action(false, 0u, CRC_A) == LISTEN_START_SEND_PARAMS);

    // Après un OFF diffusé, un contrôle ciblé exige un LISTEN intermédiaire
    // pour que les cartes non ciblées reprennent le balayage.
    CHECK(listen_start_needs_listen_relay(LISTEN_START_STOP_FIRST, true));
    CHECK(!listen_start_needs_listen_relay(LISTEN_START_STOP_FIRST, false));
    CHECK(!listen_start_needs_listen_relay(LISTEN_START_SKIP_PARAMS, true));
    CHECK(!listen_start_needs_listen_relay(LISTEN_START_SEND_PARAMS, true));
}

int main(void) {
    test_moteur_apply_hors_ecoute();
    test_moteur_reuse_meme_crc();
    test_moteur_refus_crc_different();
    test_moteur_carte_non_ciblee();
    test_clavier_relance();

    printf("test_listen_control_logic: %d verifications, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
