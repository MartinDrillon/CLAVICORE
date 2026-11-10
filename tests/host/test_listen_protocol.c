// Tests unitaires hôte des trames d'écoute IPROPI (ADR-0032) et du jeu
// canonique de paramètres : codecs des 8 trames, packing 12 bits, DLC stricts,
// validation et sérialisation de listen_params.
#include <stdio.h>
#include <string.h>

#include "can_protocol.h"
#include "listen_params.h"

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

// Jeu de référence de `k_listen_config` (ADR-0032 §5, valeurs figées ici pour
// que le vecteur CRC doré reste stable même si le Clavier évolue).
static listen_params_t golden_params(void) {
    listen_params_t p = {
        .settle_us = 15u,
        .samples_per_dwell = 2u,
        .baseline_init_ms = 200u,
        .baseline_tau_ms = 500u,
        .rise_thr = 5u,
        .return_thr = 3u,
        .return_hold_us = 1000u,
        .split_pct = 30u,
        .min_pulse_us = 500u,
        .drive_blank_ms = 50u,
        .trace_period_us = 20u,
        .trace_trig = 5u,
        .trace_timeout_ms = 10000u,
        .stream_decim = 1u,
        .vel_fixed = 100u,
        .vel_min_delta = 0u,
        .vel_max_delta = 0u,
        .detail = 1u,
    };
    return p;
}

static void check_bytes(const uint8_t *actual, const uint8_t *expected, size_t length) {
    const bool same = memcmp(actual, expected, length) == 0;

    CHECK(same);
    if (!same) {
        size_t i;
        for (i = 0u; i < length; ++i) {
            printf("  [%u] attendu=0x%02X obtenu=0x%02X\n", (unsigned)i,
                   (unsigned)expected[i], (unsigned)actual[i]);
        }
    }
}

// --- LISTEN_PULSE (0x110) --------------------------------------------------

static void check_pulse_roundtrip(const can_listen_pulse_t *in) {
    uint8_t data[CAN_DLC_LISTEN_PULSE];
    can_listen_pulse_t out;
    const can_listen_pulse_t zero = {0};

    memset(data, 0xAA, sizeof(data));
    out = zero;
    CHECK(can_encode_listen_pulse(data, in) == CAN_PROTO_OK);
    CHECK(can_decode_listen_pulse(data, CAN_DLC_LISTEN_PULSE, &out) == CAN_PROTO_OK);
    CHECK(out.uid_tag == in->uid_tag);
    CHECK(out.channel == in->channel);
    CHECK(out.seq == in->seq);
    CHECK(out.base == in->base);
    CHECK(out.peak == in->peak);
    CHECK(out.split == in->split);
    CHECK(out.saturated == in->saturated);
}

static void test_listen_pulse(void) {
    const can_listen_pulse_t low = {0u, 0u, 0u, 0u, 0u, false, false};
    const can_listen_pulse_t high = {0xFFFFu, 11u, 255u, 4095u, 4095u, true, true};
    const can_listen_pulse_t mid = {0x1234u, 5u, 7u, 100u, 200u, true, false};
    const uint8_t expected[CAN_DLC_LISTEN_PULSE] = {0x34u, 0x12u, 0x1Bu, 0xA5u,
                                                    0x34u, 0x72u, 0x56u, 0x00u};
    const can_listen_pulse_t vector = {0x1234u, 11u, 0xA5u, 0x234u, 0x567u, true, false};
    uint8_t data[CAN_DLC_LISTEN_PULSE];
    can_listen_pulse_t out;
    can_listen_pulse_t bad;

    check_pulse_roundtrip(&low);
    check_pulse_roundtrip(&high);
    check_pulse_roundtrip(&mid);

    // Vecteur exact §1.5.
    CHECK(can_encode_listen_pulse(data, &vector) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));
    memset(&out, 0, sizeof(out));
    CHECK(can_decode_listen_pulse(expected, CAN_DLC_LISTEN_PULSE, &out) == CAN_PROTO_OK);
    CHECK(out.channel == 11u && out.seq == 0xA5u && out.base == 0x234u &&
          out.peak == 0x567u && out.split && !out.saturated);

    // Octet réservé : ignoré en réception, émis à 0.
    data[7] = 0xFFu;
    CHECK(can_decode_listen_pulse(data, CAN_DLC_LISTEN_PULSE, &out) == CAN_PROTO_OK);
    CHECK(can_encode_listen_pulse(data, &vector) == CAN_PROTO_OK);
    CHECK(data[7] == 0u);

    // Valeurs refusées.
    bad = mid;
    bad.channel = 12u;
    CHECK(can_encode_listen_pulse(data, &bad) == CAN_PROTO_BAD_VALUE);
    bad = mid;
    bad.base = 4096u;
    CHECK(can_encode_listen_pulse(data, &bad) == CAN_PROTO_BAD_VALUE);
    bad = mid;
    bad.peak = 4096u;
    CHECK(can_encode_listen_pulse(data, &bad) == CAN_PROTO_BAD_VALUE);
    CHECK(can_encode_listen_pulse(data, &vector) == CAN_PROTO_OK);
    data[2] = 0x0Cu;  // canal 12
    CHECK(can_decode_listen_pulse(data, CAN_DLC_LISTEN_PULSE, &out) == CAN_PROTO_BAD_VALUE);

    // DLC et arguments.
    CHECK(can_decode_listen_pulse(expected, CAN_DLC_LISTEN_PULSE - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_pulse(expected, CAN_DLC_LISTEN_PULSE + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_pulse(NULL, CAN_DLC_LISTEN_PULSE, &out) == CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_pulse(expected, CAN_DLC_LISTEN_PULSE, NULL) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_pulse(NULL, &vector) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_pulse(data, NULL) == CAN_PROTO_BAD_ARG);

    // Helpers de validité.
    CHECK(can_listen_channel_is_valid(0u));
    CHECK(can_listen_channel_is_valid(11u));
    CHECK(!can_listen_channel_is_valid(12u));
}

// --- LISTEN_CONTROL (0x404) ------------------------------------------------

static void check_control_roundtrip(const can_listen_control_t *in) {
    uint8_t data[CAN_DLC_LISTEN_CONTROL];
    can_listen_control_t out;

    memset(&out, 0, sizeof(out));
    CHECK(can_encode_listen_control(data, in) == CAN_PROTO_OK);
    CHECK(can_decode_listen_control(data, CAN_DLC_LISTEN_CONTROL, &out) == CAN_PROTO_OK);
    CHECK(out.proto_version == in->proto_version);
    CHECK(out.mode == in->mode);
    CHECK(out.param_crc16 == in->param_crc16);
    CHECK(out.target_uid_tag == in->target_uid_tag);
    CHECK(out.channel == in->channel);
}

static void test_listen_control(void) {
    const can_listen_control_t off = {1u, CAN_LISTEN_MODE_OFF, 0u, 0u, 0u};
    const can_listen_control_t listen = {1u, CAN_LISTEN_MODE_LISTEN, 0xBEEFu, 0u, 0u};
    const can_listen_control_t trace = {1u, CAN_LISTEN_MODE_TRACE, 0xBEEFu, 0x1234u, 11u};
    const can_listen_control_t stream = {1u, CAN_LISTEN_MODE_STREAM, 0u, 0xFFFFu, 0u};
    const can_listen_control_t stream_off = {1u, CAN_LISTEN_MODE_STREAM_OFF, 0u, 0u, 0u};
    const uint8_t expected[CAN_DLC_LISTEN_CONTROL] = {0x01u, 0x02u, 0xEFu, 0xBEu,
                                                      0x34u, 0x12u, 0x05u, 0x00u};
    can_listen_control_t vector = trace;
    uint8_t data[CAN_DLC_LISTEN_CONTROL];
    can_listen_control_t out;

    check_control_roundtrip(&off);
    check_control_roundtrip(&listen);
    check_control_roundtrip(&trace);
    check_control_roundtrip(&stream);
    check_control_roundtrip(&stream_off);

    vector.channel = 5u;
    CHECK(can_encode_listen_control(data, &vector) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));

    // Octet réservé ignoré / émis à 0.
    data[7] = 0xCCu;
    CHECK(can_decode_listen_control(data, CAN_DLC_LISTEN_CONTROL, &out) == CAN_PROTO_OK);
    CHECK(can_encode_listen_control(data, &vector) == CAN_PROTO_OK);
    CHECK(data[7] == 0u);

    // Mode inconnu (encode et decode).
    out = vector;
    out.mode = (can_listen_mode_t)9;
    CHECK(can_encode_listen_control(data, &out) == CAN_PROTO_BAD_VALUE);
    CHECK(can_encode_listen_control(data, &vector) == CAN_PROTO_OK);
    data[1] = 9u;
    CHECK(can_decode_listen_control(data, CAN_DLC_LISTEN_CONTROL, &out) ==
          CAN_PROTO_BAD_VALUE);

    // TRACE/STREAM exigent une cible et un canal valides.
    out = trace;
    out.target_uid_tag = 0u;
    CHECK(can_encode_listen_control(data, &out) == CAN_PROTO_BAD_VALUE);
    out = trace;
    out.channel = 12u;
    CHECK(can_encode_listen_control(data, &out) == CAN_PROTO_BAD_VALUE);
    out = trace;
    out.target_uid_tag = 0u;
    out.channel = 0u;  // decode : la cible nulle est refusée aussi
    CHECK(can_encode_listen_control(data, &trace) == CAN_PROTO_OK);
    data[4] = 0u;
    data[5] = 0u;
    CHECK(can_decode_listen_control(data, CAN_DLC_LISTEN_CONTROL, &out) ==
          CAN_PROTO_BAD_VALUE);
    data[4] = 0x34u;
    data[5] = 0x12u;
    data[6] = 12u;
    CHECK(can_decode_listen_control(data, CAN_DLC_LISTEN_CONTROL, &out) ==
          CAN_PROTO_BAD_VALUE);

    // DLC et arguments.
    CHECK(can_decode_listen_control(expected, CAN_DLC_LISTEN_CONTROL - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_control(expected, CAN_DLC_LISTEN_CONTROL + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_control(NULL, CAN_DLC_LISTEN_CONTROL, &out) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_control(expected, CAN_DLC_LISTEN_CONTROL, NULL) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_control(NULL, &vector) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_control(data, NULL) == CAN_PROTO_BAD_ARG);

    CHECK(can_listen_mode_is_valid(CAN_LISTEN_MODE_OFF));
    CHECK(can_listen_mode_is_valid(CAN_LISTEN_MODE_STREAM_OFF));
    CHECK(!can_listen_mode_is_valid((can_listen_mode_t)5));
    CHECK(!can_listen_mode_is_valid((can_listen_mode_t)9));
}

// --- LISTEN_PARAM (0x405) --------------------------------------------------

static void test_listen_param(void) {
    const can_listen_param_t first = {1u, 1u, 0u};
    const can_listen_param_t last = {1u, 18u, 0xFFFFFFFFu};
    const can_listen_param_t vector = {1u, 13u, 0x12345678u};
    const uint8_t expected[CAN_DLC_LISTEN_PARAM] = {0x01u, 0x0Du, 0x78u, 0x56u,
                                                    0x34u, 0x12u, 0x00u, 0x00u};
    uint8_t data[CAN_DLC_LISTEN_PARAM];
    can_listen_param_t out;

    memset(&out, 0, sizeof(out));
    CHECK(can_encode_listen_param(data, &first) == CAN_PROTO_OK);
    CHECK(can_decode_listen_param(data, CAN_DLC_LISTEN_PARAM, &out) == CAN_PROTO_OK);
    CHECK(out.proto_version == 1u && out.param_id == 1u && out.value == 0u);
    CHECK(can_encode_listen_param(data, &last) == CAN_PROTO_OK);
    CHECK(can_decode_listen_param(data, CAN_DLC_LISTEN_PARAM, &out) == CAN_PROTO_OK);
    CHECK(out.param_id == 18u && out.value == 0xFFFFFFFFu);

    CHECK(can_encode_listen_param(data, &vector) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));

    // Réservé non nul : ignoré en réception, émis à 0.
    data[6] = 0xFFu;
    data[7] = 0xFFu;
    CHECK(can_decode_listen_param(data, CAN_DLC_LISTEN_PARAM, &out) == CAN_PROTO_OK);
    CHECK(can_encode_listen_param(data, &vector) == CAN_PROTO_OK);
    CHECK(data[6] == 0u && data[7] == 0u);

    CHECK(can_decode_listen_param(expected, CAN_DLC_LISTEN_PARAM - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_param(expected, CAN_DLC_LISTEN_PARAM + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_param(NULL, CAN_DLC_LISTEN_PARAM, &out) == CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_param(expected, CAN_DLC_LISTEN_PARAM, NULL) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_param(NULL, &vector) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_param(data, NULL) == CAN_PROTO_BAD_ARG);
}

// --- LISTEN_NOISE_QUERY (0x406) --------------------------------------------

static void test_listen_noise_query(void) {
    const can_listen_noise_query_t query = {0x1234u, 7u, 9u, 16u};
    const uint8_t expected[CAN_DLC_LISTEN_NOISE_QUERY] = {0x34u, 0x12u, 0x07u, 0x09u,
                                                          0x10u, 0x00u};
    uint8_t data[CAN_DLC_LISTEN_NOISE_QUERY];
    can_listen_noise_query_t out;
    can_listen_noise_query_t bad;

    memset(&out, 0, sizeof(out));
    CHECK(can_encode_listen_noise_query(data, &query) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));
    CHECK(can_decode_listen_noise_query(expected, CAN_DLC_LISTEN_NOISE_QUERY, &out) ==
          CAN_PROTO_OK);
    CHECK(out.target_uid_tag == 0x1234u && out.channel == 7u && out.request_id == 9u &&
          out.lookback_ms == 16u);

    // Bornes acceptées.
    bad = query;
    bad.channel = 11u;
    bad.request_id = 127u;
    bad.lookback_ms = 65535u;
    CHECK(can_encode_listen_noise_query(data, &bad) == CAN_PROTO_OK);
    CHECK(can_decode_listen_noise_query(data, CAN_DLC_LISTEN_NOISE_QUERY, &out) ==
          CAN_PROTO_OK);
    CHECK(out.channel == 11u && out.request_id == 127u && out.lookback_ms == 65535u);

    // Valeurs refusées.
    bad = query;
    bad.channel = 12u;
    CHECK(can_encode_listen_noise_query(data, &bad) == CAN_PROTO_BAD_VALUE);
    bad = query;
    bad.lookback_ms = 0u;
    CHECK(can_encode_listen_noise_query(data, &bad) == CAN_PROTO_BAD_VALUE);
    CHECK(can_encode_listen_noise_query(data, &query) == CAN_PROTO_OK);
    data[2] = 12u;
    CHECK(can_decode_listen_noise_query(data, CAN_DLC_LISTEN_NOISE_QUERY, &out) ==
          CAN_PROTO_BAD_VALUE);
    data[2] = 7u;
    data[4] = 0u;
    data[5] = 0u;
    CHECK(can_decode_listen_noise_query(data, CAN_DLC_LISTEN_NOISE_QUERY, &out) ==
          CAN_PROTO_BAD_VALUE);

    CHECK(can_decode_listen_noise_query(expected, CAN_DLC_LISTEN_NOISE_QUERY - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_noise_query(expected, CAN_DLC_LISTEN_NOISE_QUERY + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_noise_query(NULL, CAN_DLC_LISTEN_NOISE_QUERY, &out) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_noise_query(expected, CAN_DLC_LISTEN_NOISE_QUERY, NULL) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_noise_query(NULL, &query) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_noise_query(data, NULL) == CAN_PROTO_BAD_ARG);
}

// --- LISTEN_STATUS (0x426) -------------------------------------------------

static void test_listen_status(void) {
    const can_listen_status_t status = {0xABCDu, CAN_LISTEN_STATE_TRACE_TIMEOUT, 13u,
                                        0x1234u, 200u};
    const uint8_t expected[CAN_DLC_LISTEN_STATUS] = {0xCDu, 0xABu, 0x07u, 0x0Du,
                                                     0x34u, 0x12u, 0xC8u, 0x00u};
    uint8_t data[CAN_DLC_LISTEN_STATUS];
    can_listen_status_t out;
    can_listen_status_t bad;

    memset(&out, 0, sizeof(out));
    CHECK(can_encode_listen_status(data, &status) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));
    CHECK(can_decode_listen_status(expected, CAN_DLC_LISTEN_STATUS, &out) == CAN_PROTO_OK);
    CHECK(out.uid_tag == 0xABCDu && out.state == CAN_LISTEN_STATE_TRACE_TIMEOUT &&
          out.refused_param_id == 13u && out.sweep_period_us == 0x1234u &&
          out.overflows == 200u);

    // Tous les états connus vont et viennent.
    for (int state = 0; state <= 8; ++state) {
        bad = status;
        bad.state = (can_listen_state_t)state;
        CHECK(can_encode_listen_status(data, &bad) == CAN_PROTO_OK);
        CHECK(can_decode_listen_status(data, CAN_DLC_LISTEN_STATUS, &out) == CAN_PROTO_OK);
        CHECK(out.state == (can_listen_state_t)state);
    }

    // Bornes libres.
    bad = status;
    bad.refused_param_id = 255u;
    bad.sweep_period_us = 0xFFFFu;
    bad.overflows = 255u;
    CHECK(can_encode_listen_status(data, &bad) == CAN_PROTO_OK);
    CHECK(can_decode_listen_status(data, CAN_DLC_LISTEN_STATUS, &out) == CAN_PROTO_OK);
    CHECK(out.refused_param_id == 255u && out.sweep_period_us == 0xFFFFu &&
          out.overflows == 255u);

    // État inconnu.
    bad = status;
    bad.state = (can_listen_state_t)9;
    CHECK(can_encode_listen_status(data, &bad) == CAN_PROTO_BAD_VALUE);
    CHECK(can_encode_listen_status(data, &status) == CAN_PROTO_OK);
    data[2] = 9u;
    CHECK(can_decode_listen_status(data, CAN_DLC_LISTEN_STATUS, &out) ==
          CAN_PROTO_BAD_VALUE);
    data[2] = 0u;  // réservé ignoré
    data[7] = 0xFFu;
    CHECK(can_decode_listen_status(data, CAN_DLC_LISTEN_STATUS, &out) == CAN_PROTO_OK);
    CHECK(can_encode_listen_status(data, &status) == CAN_PROTO_OK);
    CHECK(data[7] == 0u);

    CHECK(can_decode_listen_status(expected, CAN_DLC_LISTEN_STATUS - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_status(expected, CAN_DLC_LISTEN_STATUS + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_status(NULL, CAN_DLC_LISTEN_STATUS, &out) == CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_status(expected, CAN_DLC_LISTEN_STATUS, NULL) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_status(NULL, &status) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_status(data, NULL) == CAN_PROTO_BAD_ARG);

    CHECK(can_listen_state_is_valid(CAN_LISTEN_STATE_IDLE));
    CHECK(can_listen_state_is_valid(CAN_LISTEN_STATE_FAULT));
    CHECK(!can_listen_state_is_valid((can_listen_state_t)9));
}

// --- LISTEN_NOISE_REPORT (0x427) -------------------------------------------

static void test_listen_noise_report(void) {
    const can_listen_noise_report_t report = {0x1234u, 3u, 4u, 0x123u, 0xABCu, true};
    const uint8_t expected[CAN_DLC_LISTEN_NOISE_REPORT] = {0x34u, 0x12u, 0x03u, 0x04u,
                                                           0x23u, 0xC1u, 0xABu, 0x01u};
    uint8_t data[CAN_DLC_LISTEN_NOISE_REPORT];
    can_listen_noise_report_t out;
    can_listen_noise_report_t bad;

    memset(&out, 0, sizeof(out));
    CHECK(can_encode_listen_noise_report(data, &report) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));
    CHECK(can_decode_listen_noise_report(expected, CAN_DLC_LISTEN_NOISE_REPORT, &out) ==
          CAN_PROTO_OK);
    CHECK(out.uid_tag == 0x1234u && out.channel == 3u && out.request_id == 4u &&
          out.min == 0x123u && out.max == 0xABCu && out.truncated);

    // Bornes.
    bad = report;
    bad.channel = 0u;
    bad.min = 0u;
    bad.max = 4095u;
    bad.truncated = false;
    CHECK(can_encode_listen_noise_report(data, &bad) == CAN_PROTO_OK);
    CHECK(can_decode_listen_noise_report(data, CAN_DLC_LISTEN_NOISE_REPORT, &out) ==
          CAN_PROTO_OK);
    CHECK(out.channel == 0u && out.min == 0u && out.max == 4095u && !out.truncated);

    // Valeurs refusées.
    bad = report;
    bad.channel = 12u;
    CHECK(can_encode_listen_noise_report(data, &bad) == CAN_PROTO_BAD_VALUE);
    bad = report;
    bad.min = 4096u;
    CHECK(can_encode_listen_noise_report(data, &bad) == CAN_PROTO_BAD_VALUE);
    bad = report;
    bad.max = 4096u;
    CHECK(can_encode_listen_noise_report(data, &bad) == CAN_PROTO_BAD_VALUE);
    bad = report;
    bad.min = 0xABCu;
    bad.max = 0x123u;
    CHECK(can_encode_listen_noise_report(data, &bad) == CAN_PROTO_BAD_VALUE);

    CHECK(can_encode_listen_noise_report(data, &report) == CAN_PROTO_OK);
    data[2] = 12u;
    CHECK(can_decode_listen_noise_report(data, CAN_DLC_LISTEN_NOISE_REPORT, &out) ==
          CAN_PROTO_BAD_VALUE);
    // min > max : min = 0xABD, max = 0x123.
    data[2] = 3u;
    data[4] = 0xBDu;
    data[5] = 0x31u;  // min[11:8]=1 | max[3:0]=3
    data[6] = 0x12u;
    CHECK(can_decode_listen_noise_report(data, CAN_DLC_LISTEN_NOISE_REPORT, &out) ==
          CAN_PROTO_BAD_VALUE);

    // Drapeaux : seul le bit 0 est significatif, les autres sont ignorés.
    CHECK(can_encode_listen_noise_report(data, &report) == CAN_PROTO_OK);
    data[7] = 0xFEu;
    CHECK(can_decode_listen_noise_report(data, CAN_DLC_LISTEN_NOISE_REPORT, &out) ==
          CAN_PROTO_OK);
    CHECK(!out.truncated);
    data[7] = 0xFFu;
    CHECK(can_decode_listen_noise_report(data, CAN_DLC_LISTEN_NOISE_REPORT, &out) ==
          CAN_PROTO_OK);
    CHECK(out.truncated);

    CHECK(can_decode_listen_noise_report(expected, CAN_DLC_LISTEN_NOISE_REPORT - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_noise_report(expected, CAN_DLC_LISTEN_NOISE_REPORT + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_noise_report(NULL, CAN_DLC_LISTEN_NOISE_REPORT, &out) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_noise_report(expected, CAN_DLC_LISTEN_NOISE_REPORT, NULL) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_noise_report(NULL, &report) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_noise_report(data, NULL) == CAN_PROTO_BAD_ARG);
}

// --- LISTEN_PULSE_TIMING (0x428) -------------------------------------------

static void test_listen_pulse_timing(void) {
    const can_listen_pulse_timing_t timing = {0x0102u, 0x33u, 0x0456u, 0x0789u};
    const uint8_t expected[CAN_DLC_LISTEN_PULSE_TIMING] = {0x02u, 0x01u, 0x33u, 0x56u,
                                                           0x04u, 0x89u, 0x07u, 0x00u};
    const can_listen_pulse_timing_t saturated = {0xFFFFu, 255u, 0xFFFFu, 0xFFFFu};
    uint8_t data[CAN_DLC_LISTEN_PULSE_TIMING];
    can_listen_pulse_timing_t out;

    memset(&out, 0, sizeof(out));
    CHECK(can_encode_listen_pulse_timing(data, &timing) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));
    CHECK(can_decode_listen_pulse_timing(expected, CAN_DLC_LISTEN_PULSE_TIMING, &out) ==
          CAN_PROTO_OK);
    CHECK(out.uid_tag == 0x0102u && out.seq == 0x33u && out.t_peak_100us == 0x0456u &&
          out.t_end_peak_100us == 0x0789u);

    CHECK(can_encode_listen_pulse_timing(data, &saturated) == CAN_PROTO_OK);
    CHECK(can_decode_listen_pulse_timing(data, CAN_DLC_LISTEN_PULSE_TIMING, &out) ==
          CAN_PROTO_OK);
    CHECK(out.uid_tag == 0xFFFFu && out.seq == 255u && out.t_peak_100us == 0xFFFFu &&
          out.t_end_peak_100us == 0xFFFFu);

    // Réservé ignoré / émis à 0.
    data[7] = 0xAAu;
    CHECK(can_decode_listen_pulse_timing(data, CAN_DLC_LISTEN_PULSE_TIMING, &out) ==
          CAN_PROTO_OK);
    CHECK(can_encode_listen_pulse_timing(data, &timing) == CAN_PROTO_OK);
    CHECK(data[7] == 0u);

    CHECK(can_decode_listen_pulse_timing(expected, CAN_DLC_LISTEN_PULSE_TIMING - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_pulse_timing(expected, CAN_DLC_LISTEN_PULSE_TIMING + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_pulse_timing(NULL, CAN_DLC_LISTEN_PULSE_TIMING, &out) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_pulse_timing(expected, CAN_DLC_LISTEN_PULSE_TIMING, NULL) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_pulse_timing(NULL, &timing) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_pulse_timing(data, NULL) == CAN_PROTO_BAD_ARG);
}

// --- LISTEN_TRACE (0x429) --------------------------------------------------

static void test_listen_trace(void) {
    const can_listen_trace_t trace = {0xBEEFu, {0x123u, 0x456u, 0x789u, 0xABCu}};
    const uint8_t expected[CAN_DLC_LISTEN_TRACE] = {0xEFu, 0xBEu, 0x23u, 0x61u,
                                                    0x45u, 0x89u, 0xC7u, 0xABu};
    const can_listen_trace_t zeros = {0u, {0u, 0u, 0u, 0u}};
    const can_listen_trace_t full = {0xFFFFu, {4095u, 4095u, 4095u, 4095u}};
    uint8_t data[CAN_DLC_LISTEN_TRACE];
    can_listen_trace_t out;
    can_listen_trace_t bad;

    memset(&out, 0, sizeof(out));
    CHECK(can_encode_listen_trace(data, &trace) == CAN_PROTO_OK);
    check_bytes(data, expected, sizeof(expected));
    CHECK(can_decode_listen_trace(expected, CAN_DLC_LISTEN_TRACE, &out) == CAN_PROTO_OK);
    CHECK(out.seq == 0xBEEFu && out.samples[0] == 0x123u && out.samples[1] == 0x456u &&
          out.samples[2] == 0x789u && out.samples[3] == 0xABCu);

    CHECK(can_encode_listen_trace(data, &zeros) == CAN_PROTO_OK);
    CHECK(can_decode_listen_trace(data, CAN_DLC_LISTEN_TRACE, &out) == CAN_PROTO_OK);
    CHECK(out.seq == 0u && out.samples[0] == 0u && out.samples[3] == 0u);
    CHECK(can_encode_listen_trace(data, &full) == CAN_PROTO_OK);
    CHECK(can_decode_listen_trace(data, CAN_DLC_LISTEN_TRACE, &out) == CAN_PROTO_OK);
    CHECK(out.seq == 0xFFFFu && out.samples[3] == 4095u);

    // Échantillon hors 12 bits refusé à l'encodage.
    bad = trace;
    bad.samples[2] = 4096u;
    CHECK(can_encode_listen_trace(data, &bad) == CAN_PROTO_BAD_VALUE);

    CHECK(can_decode_listen_trace(expected, CAN_DLC_LISTEN_TRACE - 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_trace(expected, CAN_DLC_LISTEN_TRACE + 1u, &out) ==
          CAN_PROTO_BAD_DLC);
    CHECK(can_decode_listen_trace(NULL, CAN_DLC_LISTEN_TRACE, &out) == CAN_PROTO_BAD_ARG);
    CHECK(can_decode_listen_trace(expected, CAN_DLC_LISTEN_TRACE, NULL) ==
          CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_trace(NULL, &trace) == CAN_PROTO_BAD_ARG);
    CHECK(can_encode_listen_trace(data, NULL) == CAN_PROTO_BAD_ARG);
}

// --- listen_params ---------------------------------------------------------

static void test_listen_params_fields(void) {
    uint8_t id;

    CHECK(listen_params_field_count() == LISTEN_PARAMS_COUNT);
    CHECK(LISTEN_PARAMS_WIRE_SIZE == 72u);

    // Noms et ids 1..18 sans trou, noms uniques, aller-retour nom<->id.
    for (uint8_t current = 1u; current <= LISTEN_PARAMS_COUNT; ++current) {
        const char *name = listen_params_field_name(current);

        CHECK(name != NULL);
        CHECK(name != NULL && name[0] != '\0');
        CHECK(listen_params_field_id(name, &id));
        CHECK(id == current);
        if (name == NULL) {
            continue;
        }
        for (uint8_t other = (uint8_t)(current + 1u); other <= LISTEN_PARAMS_COUNT; ++other) {
            CHECK(strcmp(name, listen_params_field_name(other)) != 0);
        }
    }
    CHECK(listen_params_field_name(LISTEN_PARAM_INVALID) == NULL);
    CHECK(listen_params_field_name(19u) == NULL);
    CHECK(!listen_params_field_id("inconnu", &id));
    CHECK(!listen_params_field_id(NULL, &id));
    CHECK(!listen_params_field_id("settle_us", NULL));

    // get/set de chaque champ, ids seuls contrôlés.
    {
        listen_params_t p = {0};

        for (uint8_t current = 1u; current <= LISTEN_PARAMS_COUNT; ++current) {
            uint32_t value = 0x1000u + current;

            CHECK(listen_params_set(&p, current, value));
            CHECK(listen_params_get(&p, current, &value));
            CHECK(value == 0x1000u + current);
        }
        CHECK(!listen_params_set(&p, LISTEN_PARAM_INVALID, 1u));
        CHECK(!listen_params_set(&p, 19u, 1u));
        {
            uint32_t dummy = 0u;

            CHECK(!listen_params_get(&p, LISTEN_PARAM_INVALID, &dummy));
            CHECK(!listen_params_get(&p, 1u, NULL));
            CHECK(!listen_params_get(NULL, 1u, &dummy));
        }
        CHECK(!listen_params_set(NULL, 1u, 1u));
    }
}

static void test_listen_params_validate(void) {
    listen_params_t p = golden_params();
    uint8_t bad_id = 0xAAu;

    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_OK);
    CHECK(bad_id == 0u);

    // samples_per_dwell 1..64.
    p = golden_params();
    p.samples_per_dwell = 0u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_SAMPLES_PER_DWELL);
    p.samples_per_dwell = 65u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_SAMPLES_PER_DWELL);

    // return_thr < rise_thr.
    p = golden_params();
    p.return_thr = p.rise_thr;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_RETURN_THR);

    // split_pct 1..99.
    p = golden_params();
    p.split_pct = 0u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_SPLIT_PCT);
    p.split_pct = 100u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_SPLIT_PCT);

    // trace_period_us >= 2.
    p = golden_params();
    p.trace_period_us = 1u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_TRACE_PERIOD_US);

    // trace_timeout_ms >= 1.
    p = golden_params();
    p.trace_timeout_ms = 0u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_TRACE_TIMEOUT_MS);

    // stream_decim >= 1.
    p = golden_params();
    p.stream_decim = 0u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_STREAM_DECIM);

    // vel_fixed 1..127.
    p = golden_params();
    p.vel_fixed = 0u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_VEL_FIXED);
    p.vel_fixed = 128u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_VEL_FIXED);

    // (min, max) = (0, 0) ou (min > 0, max > 0, min < max).
    p = golden_params();
    p.vel_min_delta = 0u;
    p.vel_max_delta = 10u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_VEL_MIN_DELTA);
    p = golden_params();
    p.vel_min_delta = 10u;
    p.vel_max_delta = 0u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_VEL_MIN_DELTA);
    p = golden_params();
    p.vel_min_delta = 10u;
    p.vel_max_delta = 10u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_VEL_MIN_DELTA);
    p = golden_params();
    p.vel_min_delta = 20u;
    p.vel_max_delta = 10u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_VEL_MIN_DELTA);

    p = golden_params();
    p.vel_min_delta = 10u;
    p.vel_max_delta = 20u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_OK);
    CHECK(bad_id == 0u);

    // detail 0 ou 1.
    p = golden_params();
    p.detail = 2u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_DETAIL);

    // Le premier champ fautif est celui rapporté.
    p = golden_params();
    p.split_pct = 0u;
    p.detail = 2u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_SPLIT_PCT);
    p = golden_params();
    p.detail = 2u;
    CHECK(listen_params_validate(&p, &bad_id) == LISTEN_PARAMS_ERR_FIELD);
    CHECK(bad_id == LISTEN_PARAM_DETAIL);

    // bad_id optionnel et argument NULL.
    CHECK(listen_params_validate(NULL, &bad_id) == LISTEN_PARAMS_ERR_ARG);
    CHECK(bad_id == 0u);
    p = golden_params();
    CHECK(listen_params_validate(&p, NULL) == LISTEN_PARAMS_OK);
}

static void test_listen_params_serialize(void) {
    listen_params_t p = golden_params();
    uint8_t wire[LISTEN_PARAMS_WIRE_SIZE];
    uint8_t wire2[LISTEN_PARAMS_WIRE_SIZE];

    memset(wire, 0xAA, sizeof(wire));
    CHECK(listen_params_serialize(&p, wire) == LISTEN_PARAMS_WIRE_SIZE);

    // Vecteur doré : 72 octets little-endian, ordre des ids 1..18.
    CHECK(wire[0] == 0x0Fu && wire[1] == 0x00u && wire[2] == 0x00u && wire[3] == 0x00u);
    CHECK(wire[4] == 0x02u && wire[7] == 0x00u);
    CHECK(wire[8] == 0xC8u && wire[9] == 0x00u);           // 200
    CHECK(wire[16] == 0x05u && wire[20] == 0x03u);         // rise_thr, return_thr
    CHECK(wire[28] == 0x1Eu);                              // split_pct 30
    CHECK(wire[48] == 0x10u && wire[49] == 0x27u);         // trace_timeout_ms 10000
    CHECK(wire[56] == 0x64u);                              // vel_fixed 100
    CHECK(wire[68] == 0x01u && wire[71] == 0x00u);         // detail 1

    // CRC doré : fige le format du jeu de k_listen_config.
    CHECK(listen_params_crc16(&p) == 0x9C51u);

    // Un seul champ modifié change le CRC.
    p.detail = 0u;
    CHECK(listen_params_crc16(&p) != 0x9C51u);
    CHECK(listen_params_serialize(&p, wire2) == LISTEN_PARAMS_WIRE_SIZE);
    CHECK(memcmp(wire, wire2, sizeof(wire)) != 0);
    CHECK(wire2[68] == 0x00u);

    // Arguments nuls.
    CHECK(listen_params_serialize(NULL, wire) == 0u);
    CHECK(listen_params_serialize(&p, NULL) == 0u);
    CHECK(listen_params_crc16(NULL) == 0xFFFFu);
}

int main(void) {
    test_listen_pulse();
    test_listen_control();
    test_listen_param();
    test_listen_noise_query();
    test_listen_status();
    test_listen_noise_report();
    test_listen_pulse_timing();
    test_listen_trace();
    test_listen_params_fields();
    test_listen_params_validate();
    test_listen_params_serialize();

    if (g_failures != 0) {
        printf("%d echec(s) sur %d verifications\n", g_failures, g_checks);
        return 1;
    }
    printf("OK: %d verifications\n", g_checks);
    return 0;
}
