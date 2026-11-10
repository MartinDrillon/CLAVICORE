// Tests unitaires hôte de l'anneau de bruit IPROPI (ADR-0032 §2, §4, IMP-002).
#include <stdio.h>
#include <string.h>

#include "listen_noise_ring.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                 \
    do {                                                            \
        ++g_checks;                                                 \
        if (!(cond)) {                                              \
            ++g_failures;                                           \
            printf("ECHEC %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                           \
    } while (0)

static void test_empty_slice(void) {
    listen_noise_ring_t ring;
    uint16_t min = 1u;
    uint16_t max = 1u;
    uint16_t valid = 1u;
    bool truncated = true;

    listen_noise_ring_reset(&ring, 1000u);
    CHECK(ring.last_ms == 999u);
    CHECK(ring.slice[0] == LISTEN_NOISE_EMPTY);
    CHECK(!listen_noise_ring_query(&ring, 1000u, 1u, &min, &max, &truncated, &valid));
    CHECK(min == 0u && max == 0u && valid == 0u);
    CHECK(!truncated);
}

static void test_one_ms_min_max(void) {
    listen_noise_ring_t ring;
    uint16_t min = 0u;
    uint16_t max = 0u;
    uint16_t valid = 0u;
    bool truncated = false;

    listen_noise_ring_reset(&ring, 2000u);
    listen_noise_ring_record(&ring, 2000u, 12u);
    listen_noise_ring_record(&ring, 2000u, 5u);
    listen_noise_ring_record(&ring, 2000u, 20u);
    CHECK(listen_noise_ring_query(&ring, 2000u, 1u, &min, &max, &truncated, &valid));
    CHECK(min == 5u);
    CHECK(max == 20u);
    CHECK(valid == 1u);
    CHECK(!truncated);
}

static void test_wrap_256(void) {
    listen_noise_ring_t ring;
    uint16_t min = 0u;
    uint16_t max = 0u;
    uint16_t valid = 0u;
    bool truncated = false;

    listen_noise_ring_reset(&ring, 0u);
    for (uint32_t ms = 0u; ms < 256u; ++ms) {
        listen_noise_ring_record(&ring, ms, (uint16_t)(ms + 10u));
    }
    // 257e tranche : écrase la plus ancienne.
    listen_noise_ring_record(&ring, 256u, 4000u);
    CHECK(listen_noise_ring_query(&ring, 256u, 256u, &min, &max, &truncated, &valid));
    CHECK(valid == 256u);
    CHECK(min == 11u);   // 1re tranche restante = ms 1 -> 11
    CHECK(max == 4000u); // tranche ms 256
    CHECK(!truncated);
}

static void test_jump_skip(void) {
    listen_noise_ring_t ring;
    uint16_t min = 0u;
    uint16_t max = 0u;
    uint16_t valid = 0u;
    bool truncated = false;

    listen_noise_ring_reset(&ring, 0u);
    listen_noise_ring_record(&ring, 0u, 100u);
    // Saut de 44 ms : 1..43 restent vides, seule 44 est valide.
    listen_noise_ring_record(&ring, 44u, 7u);
    CHECK(listen_noise_ring_query(&ring, 44u, 10u, &min, &max, &truncated, &valid));
    CHECK(valid == 1u);
    CHECK(min == 7u && max == 7u);
    CHECK(listen_noise_ring_query(&ring, 44u, 256u, &min, &max, &truncated, &valid));
    // Les tranches 1..43 sont vides ; la tranche 0 (100) est encore dans la
    // fenêtre de 256 ms et reste valide.
    CHECK(valid == 2u);
    CHECK(min == 7u);
    CHECK(max == 100u);
}

static void test_lookback_limits(void) {
    listen_noise_ring_t ring;
    uint16_t min = 0u;
    uint16_t max = 0u;
    uint16_t valid = 0u;
    bool truncated = false;

    listen_noise_ring_reset(&ring, 0u);
    listen_noise_ring_record(&ring, 0u, 50u);
    CHECK(listen_noise_ring_query(&ring, 0u, 1u, &min, &max, &truncated, &valid));
    CHECK(!truncated);
    CHECK(valid == 1u);
    CHECK(listen_noise_ring_query(&ring, 0u, 256u, &min, &max, &truncated, &valid));
    CHECK(!truncated);
    CHECK(valid == 1u);
    CHECK(listen_noise_ring_query(&ring, 0u, 257u, &min, &max, &truncated, &valid));
    CHECK(truncated);
    CHECK(valid == 1u);
    CHECK(!listen_noise_ring_query(&ring, 0u, 0u, &min, &max, &truncated, &valid));
}

static void test_stale_producer(void) {
    listen_noise_ring_t ring;
    uint16_t min = 0u;
    uint16_t max = 0u;
    uint16_t valid = 0u;
    bool truncated = false;

    listen_noise_ring_reset(&ring, 0u);
    listen_noise_ring_record(&ring, 0u, 50u);
    // Producteur muet depuis plus d'un tour complet : aucune donnée fraîche.
    CHECK(!listen_noise_ring_query(&ring, 300u, 256u, &min, &max, &truncated, &valid));
    CHECK(valid == 0u);
}

// `last_ms` est l'horloge partagée producteur -> consommateur (Core 1 -> Core 0,
// volatile) : la requête doit s'ancrer sur sa valeur courante, sans supposer un
// cache de `now_ms`.
static void test_last_ms_tracks_producer(void) {
    listen_noise_ring_t ring;
    uint16_t min = 0u;
    uint16_t max = 0u;
    uint16_t valid = 0u;
    bool truncated = false;

    listen_noise_ring_reset(&ring, 500u);
    CHECK(ring.last_ms == 499u);
    listen_noise_ring_record(&ring, 500u, 3u);
    CHECK(ring.last_ms == 500u);
    // Saut : `last_ms` suit la tranche réellement écrite, pas `now_ms` intermédiaire.
    listen_noise_ring_record(&ring, 503u, 9u);
    CHECK(ring.last_ms == 503u);
    // Requête à now_ms en avance sur le producteur : l'ancre reste `last_ms`, et
    // l'âge négatif est traité comme une donnée fraîche.
    CHECK(listen_noise_ring_query(&ring, 505u, 4u, &min, &max, &truncated, &valid));
    CHECK(valid == 2u);
    CHECK(min == 3u && max == 9u);
}

int main(void) {
    test_empty_slice();
    test_one_ms_min_max();
    test_wrap_256();
    test_jump_skip();
    test_lookback_limits();
    test_stale_producer();
    test_last_ms_tracks_producer();

    if (g_failures != 0) {
        printf("test_listen_noise_ring: %d echec(s) sur %d verifications\n", g_failures, g_checks);
        return 1;
    }
    printf("test_listen_noise_ring: %d verifications, 0 echec\n", g_checks);
    return 0;
}
