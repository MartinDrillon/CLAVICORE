// Tests unitaires hôte du détecteur d'impulsion IPROPI (ADR-0032 §2, IMP-002).
// Module pur : aucune dépendance SDK, horodatage fourni par le test.
//
// Les paramètres sont figés ici (comme le fera le banc pour `k_listen_config`) ;
// `settle_us` et les champs propres à l'acquisition n'ont aucun effet.
#include <stdio.h>

#include "pulse_detector.h"

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

#define SAMPLE_PERIOD_US 1000u

static listen_params_t base_params(void) {
    listen_params_t p = {0};

    p.settle_us = 15u;
    p.samples_per_dwell = 2u;
    p.baseline_init_ms = 0u;  // base = premier échantillon, sauf test dédié
    p.baseline_tau_ms = 0u;   // suivi désactivé, sauf test dédié
    p.rise_thr = 5u;
    p.return_thr = 3u;
    p.return_hold_us = 0u;
    p.split_pct = 30u;
    p.min_pulse_us = 500u;
    p.drive_blank_ms = 50u;
    p.trace_period_us = 20u;
    p.trace_trig = 5u;
    p.trace_timeout_ms = 10000u;
    p.stream_decim = 1u;
    p.vel_fixed = 100u;
    p.detail = 1u;
    return p;
}

// Alimente une suite d'échantillons régulièrement espacés et compte les
// impulsions rapportées. `out_first`/`out_last` reçoivent la première/dernière.
static uint32_t feed_series(pulse_detector_t *pd, const uint16_t *samples, size_t count,
                            uint32_t start_us, pulse_event_t *out_first, pulse_event_t *out_last) {
    uint32_t events = 0u;

    for (size_t i = 0u; i < count; ++i) {
        pulse_event_t evt = {0};
        const uint32_t now_us = start_us + (uint32_t)i * SAMPLE_PERIOD_US;

        if (pulse_detector_feed(pd, samples[i], now_us, &evt)) {
            ++events;
            if (events == 1u && out_first != NULL) {
                *out_first = evt;
            }
            if (out_last != NULL) {
                *out_last = evt;
            }
        }
    }
    return events;
}

static void test_isolated_press(void) {
    listen_params_t params = base_params();
    pulse_detector_t pd;
    const uint16_t samples[] = {100u, 100u, 100u, 110u, 120u, 108u, 102u};
    pulse_event_t evt = {0};

    pulse_detector_init(&pd, &params, 0u);
    CHECK(feed_series(&pd, samples, sizeof(samples) / sizeof(samples[0]), 0u, &evt, NULL) == 1u);
    CHECK(evt.base == 100u);
    CHECK(evt.peak == 120u);
    CHECK(evt.delta == 20u);
    // t0 = dernier échantillon calme = 2000 us ; crête à 4000, fin à 6000.
    CHECK(evt.t_peak_us == 2000u);
    CHECK(evt.t_end_us == 4000u);
    CHECK(!evt.split);
    CHECK(pd.rejected == 0u);
    CHECK(pd.state == PULSE_STATE_IDLE);
}

static void test_press_and_release_separate(void) {
    listen_params_t params = base_params();
    pulse_detector_t pd;
    // Appui : crête 120 puis retour. Relâchement séparé : crête 125 puis retour.
    const uint16_t samples[] = {100u, 100u, 120u, 104u, 100u, 100u, 125u, 104u, 100u};
    pulse_event_t first = {0};
    pulse_event_t last = {0};

    pulse_detector_init(&pd, &params, 0u);
    CHECK(feed_series(&pd, samples, sizeof(samples) / sizeof(samples[0]), 0u, &first, &last) == 2u);
    CHECK(first.peak == 120u);
    CHECK(last.peak == 125u);
    CHECK(!first.split);
    CHECK(!last.split);
    CHECK(first.t_end_us >= first.t_peak_us);
    CHECK(last.t_end_us >= last.t_peak_us);
}

static void test_trill_split(void) {
    listen_params_t params = base_params();
    pulse_detector_t pd;
    // Crête 130, creux à 25 % de (130-100) = 105, puis remontée > creux+5.
    const uint16_t samples[] = {100u, 100u, 120u, 130u, 105u, 112u, 101u};
    pulse_event_t evt = {0};
    uint32_t events = 0u;

    pulse_detector_init(&pd, &params, 0u);
    for (size_t i = 0u; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        pulse_event_t out = {0};
        const uint32_t now_us = (uint32_t)i * SAMPLE_PERIOD_US;

        if (pulse_detector_feed(&pd, samples[i], now_us, &out)) {
            ++events;
            if (events == 1u) {
                evt = out;
            }
        }
    }
    CHECK(events == 2u);
    CHECK(evt.split);
    CHECK(evt.peak == 130u);
    // t0 = 1000 (dernier calme), t_peak = 3000, fin au creux = 4000.
    CHECK(evt.t_peak_us == 2000u);
    CHECK(evt.t_end_us == 3000u);
}

static void test_noise_no_event(void) {
    listen_params_t params = base_params();
    pulse_detector_t pd;
    const uint16_t samples[] = {100u, 103u, 97u, 102u, 98u, 101u, 99u, 100u};
    pulse_event_t evt = {0};

    pulse_detector_init(&pd, &params, 0u);
    CHECK(feed_series(&pd, samples, sizeof(samples) / sizeof(samples[0]), 0u, &evt, NULL) == 0u);
    CHECK(pulse_detector_baseline(&pd) == 100u);
}

static void test_noise_stable_with_ema(void) {
    listen_params_t params = base_params();

    params.baseline_tau_ms = 500u;
    pulse_detector_t pd;
    pulse_detector_init(&pd, &params, 0u);
    CHECK(!pulse_detector_feed(&pd, 100u, 0u, NULL));  // init (base = 100)
    uint32_t events = 0u;
    for (uint32_t i = 1u; i <= 2000u; ++i) {
        const uint16_t sample = ((i % 2u) == 0u) ? 102u : 98u;

        if (pulse_detector_feed(&pd, sample, i * SAMPLE_PERIOD_US, NULL)) {
            ++events;
        }
    }
    CHECK(events == 0u);
    const int base = (int)pulse_detector_baseline(&pd);
    CHECK(base >= 98 && base <= 102);
}

static void test_short_pulse_rejected(void) {
    listen_params_t params = base_params();
    pulse_detector_t pd;
    // Impulsion de 200 us (< min_pulse_us = 500), échantillonnage à 100 us.
    const uint16_t samples[] = {100u, 100u, 120u, 100u};
    uint32_t events = 0u;

    pulse_detector_init(&pd, &params, 0u);
    for (size_t i = 0u; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        if (pulse_detector_feed(&pd, samples[i], (uint32_t)i * 100u, NULL)) {
            ++events;
        }
    }
    CHECK(events == 0u);
    CHECK(pd.rejected == 1u);
}

static void test_slow_drift(void) {
    listen_params_t params = base_params();

    // Dérive de +20 points/s, échantillonnage 50 ms : +1 point par échantillon.
    // tau = 100 ms => retard stationnaire = 20 points/s x 0,1 s = 2 points.
    params.baseline_tau_ms = 100u;
    pulse_detector_t pd;
    pulse_detector_init(&pd, &params, 0u);
    uint32_t events = 0u;
    uint16_t sample = 100u;
    for (uint32_t i = 0u; i <= 100u; ++i) {
        sample = (uint16_t)(100u + i);
        if (pulse_detector_feed(&pd, sample, i * 50000u, NULL)) {
            ++events;
        }
    }
    CHECK(events == 0u);
    const int lag = (int)sample - (int)pulse_detector_baseline(&pd);
    CHECK(lag >= -2 && lag <= 2);
}

static void test_baseline_init_window(void) {
    listen_params_t params = base_params();

    params.baseline_init_ms = 100u;
    pulse_detector_t pd;
    uint32_t events = 0u;

    pulse_detector_init(&pd, &params, 0u);
    // 101 échantillons de 0 à 100 ms ; un saut isolé à 50 ms ne doit ni être
    // détecté ni interrompre l'init.
    for (uint32_t i = 0u; i <= 100u; ++i) {
        const uint16_t sample = (i == 50u) ? 500u : 100u;

        if (pulse_detector_feed(&pd, sample, i * 1000u, NULL)) {
            ++events;
        }
        if (i < 100u) {
            CHECK(pd.state == PULSE_STATE_INIT);
        }
    }
    CHECK(events == 0u);
    CHECK(pd.state == PULSE_STATE_IDLE);
    // Moyenne = (100 x 100 + 500) / 101 = 104 (arrondie).
    CHECK(pulse_detector_baseline(&pd) == 104u);
    CHECK(pd.rejected == 0u);
}

static void test_duration_not_truncated(void) {
    listen_params_t params = base_params();

    params.min_pulse_us = 0u;
    pulse_detector_t pd;
    pulse_detector_init(&pd, &params, 0u);
    CHECK(!pulse_detector_feed(&pd, 100u, 0u, NULL));
    CHECK(!pulse_detector_feed(&pd, 100u, 1000u, NULL));     // t0 = 1000
    CHECK(!pulse_detector_feed(&pd, 130u, 2000u, NULL));     // crête
    pulse_event_t evt = {0};
    CHECK(pulse_detector_feed(&pd, 100u, 7001000u, &evt));   // fin 7 s plus tard
    CHECK(evt.t_peak_us == 1000u);
    CHECK(evt.t_end_us == 7000000u);  // 7 001 000 - 1 000 : µs, pas une trame u16
    CHECK(evt.delta == 30u);
}

int main(void) {
    test_isolated_press();
    test_press_and_release_separate();
    test_trill_split();
    test_noise_no_event();
    test_noise_stable_with_ema();
    test_short_pulse_rejected();
    test_slow_drift();
    test_baseline_init_window();
    test_duration_not_truncated();

    if (g_failures != 0) {
        printf("test_pulse_detector: %d echec(s) sur %d verifications\n", g_failures, g_checks);
        return 1;
    }
    printf("test_pulse_detector: %d verifications, 0 echec\n", g_checks);
    return 0;
}
