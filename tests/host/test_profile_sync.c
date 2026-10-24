// Tests hôte des règles pures ADR-0023 (reprises, tags, sonde NVM).
// Le parseur CAN complet reste couvert par la procédure de banc
// tests/procedure_adr0023_sync_profil.md ; ce harnais verrouille les décisions
// qui ne doivent jamais régresser.
#include <stdio.h>

#include "can_protocol.h"
#include "profile_sync_logic.h"

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

static void test_nominal_and_retry(void) {
    profile_sync_history_t h = {0};

    CHECK(profile_sync_history_append(&h, 1u));
    CHECK(profile_sync_classify_ack(false, profile_sync_history_contains(&h, 1u),
                                    CAN_PROFILE_RESULT_OK, 0x1234u, 0x1234u) ==
          PROFILE_ACK_CONFIRM);
    CHECK(profile_sync_all_confirmed(2u, 1u, 0u) == false);
    CHECK(profile_sync_all_confirmed(2u, 1u, 1u) == true);
    CHECK(profile_sync_next_action(true, false, false, false, false, 1u, true) ==
          PROFILE_ACTION_RETRY);
    CHECK(profile_sync_next_action(true, false, false, false, true, 2u, false) ==
          PROFILE_ACTION_SUCCESS);
}

static void test_retryable_and_terminal(void) {
    CHECK(profile_sync_result_retryable(CAN_PROFILE_RESULT_NO_TRANSFER));
    CHECK(profile_sync_result_retryable(CAN_PROFILE_RESULT_BAD_SEQUENCE));
    CHECK(profile_sync_result_retryable(CAN_PROFILE_RESULT_BAD_CRC));
    CHECK(!profile_sync_result_retryable(CAN_PROFILE_RESULT_INVALID_FIELD));
    CHECK(!profile_sync_result_retryable(CAN_PROFILE_RESULT_BUSY));
    CHECK(!profile_sync_result_retryable(CAN_PROFILE_RESULT_FLASH_ERROR));
    CHECK(!profile_sync_result_retryable(CAN_PROFILE_RESULT_UNSUPPORTED_VERSION));

    CHECK(profile_sync_classify_ack(false, true, CAN_PROFILE_RESULT_NO_TRANSFER, 0u, 0x1234u) ==
          PROFILE_ACK_RETRYABLE);
    CHECK(profile_sync_classify_ack(false, true, CAN_PROFILE_RESULT_INVALID_FIELD, 0u, 0x1234u) ==
          PROFILE_ACK_TERMINAL);
    CHECK(profile_sync_classify_ack(false, true, CAN_PROFILE_RESULT_UNSUPPORTED_VERSION, 0u,
                                    0x1234u) == PROFILE_ACK_TERMINAL);
}

static void test_tag_and_duplicates(void) {
    CHECK(profile_sync_classify_ack(false, true, CAN_PROFILE_RESULT_OK, 0x2222u, 0x1234u) ==
          PROFILE_ACK_DIVERGENT);
    CHECK(profile_sync_classify_ack(false, false, CAN_PROFILE_RESULT_OK, 0x1234u, 0x1234u) ==
          PROFILE_ACK_IGNORE);
    CHECK(profile_sync_classify_ack(true, true, CAN_PROFILE_RESULT_OK, 0x1234u, 0x1234u) ==
          PROFILE_ACK_IGNORE);
    CHECK(profile_sync_classify_ack(true, true, CAN_PROFILE_RESULT_OK, 0x2222u, 0x1234u) ==
          PROFILE_ACK_DIVERGENT);
    CHECK(profile_sync_classify_ack(true, true, CAN_PROFILE_RESULT_NO_TRANSFER, 0u, 0x1234u) ==
          PROFILE_ACK_IGNORE);
}

static void test_probe(void) {
    CHECK(profile_sync_classify_probe(true, true, false, 0x1234u, 0x1234u) ==
          PROFILE_PROBE_RECONCILE);
    CHECK(profile_sync_classify_probe(true, false, true, 0x1234u, 0x1234u) ==
          PROFILE_PROBE_FALLBACK);
    CHECK(profile_sync_classify_probe(true, true, false, 0x2222u, 0x1234u) ==
          PROFILE_PROBE_DIVERGENT);
    CHECK(profile_sync_classify_probe(false, false, false, 0u, 0x1234u) == PROFILE_PROBE_FAIL);

    CHECK(profile_sync_next_action(true, false, true, false, false, 3u, false) ==
          PROFILE_ACTION_WAIT);
    CHECK(profile_sync_next_action(true, false, true, true, false, 3u, false) ==
          PROFILE_ACTION_FAILURE);
    CHECK(profile_sync_next_action(true, false, true, false, true, 3u, false) ==
          PROFILE_ACTION_SUCCESS);
}

static void test_bounds_and_terminal(void) {
    profile_sync_history_t h = {0};

    CHECK(profile_sync_history_append(&h, 1u));
    CHECK(profile_sync_history_append(&h, 2u));
    CHECK(profile_sync_history_append(&h, 3u));
    CHECK(!profile_sync_history_append(&h, 4u));
    CHECK(profile_sync_history_contains(&h, 2u));
    CHECK(!profile_sync_history_contains(&h, 4u));
    CHECK(!profile_sync_history_append(&h, 0u));

    CHECK(profile_sync_next_action(true, false, false, false, false, 3u, true) ==
          PROFILE_ACTION_START_PROBE);
    CHECK(profile_sync_next_action(true, true, false, false, true, 1u, false) ==
          PROFILE_ACTION_FAILURE);
    CHECK(profile_sync_next_action(false, false, false, false, false, 1u, true) ==
          PROFILE_ACTION_WAIT);
    CHECK(profile_sync_all_confirmed(0u, 0u, 0u) == true);
}

int main(void) {
    // H-01/H-02/H-16 : succès direct, reprise après timeout, zéro attendu.
    puts("H-01 H-02 H-16");
    test_nominal_and_retry();
    // H-03/H-07 : refus réessayables contre refus terminaux.
    puts("H-03 H-07");
    test_retryable_and_terminal();
    // H-04/H-05/H-06/H-08/H-09 : tardif, doublon, divergence, déjà confirmé,
    // identifiant hors opération.
    puts("H-04 H-05 H-06 H-08 H-09");
    test_tag_and_duplicates();
    // H-10 à H-14 : réconciliation NVM, DEFAULTS, divergence, sonde absente et
    // succès après confirmation complète pendant la fenêtre de sonde.
    puts("H-10 H-11 H-12 H-13 H-14");
    test_probe();
    // H-15/H-17/H-18/H-19/H-20 : borne de reprise/sonde, IDs, opération
    // inactive, limite à trois essais, échec terminal prioritaire.
    puts("H-15 H-17 H-18 H-19 H-20");
    test_bounds_and_terminal();

    printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
