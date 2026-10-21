#include "ipropi_selftest.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include "config.h"
#include "ipropi_ohm.h"
#include "solenoid_profile.h"

#define US_PER_MS 1000u
#define US_PER_S 1000000u

// Durée PROBE effectivement appliquée = profil écrêté par le plafond du pilote.
// Sert uniquement aux assertions de compilation ; la valeur d'exécution est
// demandée au pilote (solenoid_driver_probe_duration_us).
#define IPROPI_PROBE_MS_APPLIED                        \
    ((SOLENOID_PROBE_DURATION_MS < SOLENOID_PROBE_MAX_MS) ? SOLENOID_PROBE_DURATION_MS \
                                                          : SOLENOID_PROBE_MAX_MS)

_Static_assert(IPROPI_SELFTEST_RAW_SAMPLES > 1u, "IPROPI raw samples must be non-trivial");
_Static_assert(IPROPI_SELFTEST_ADC_SAMPLE_WINDOW_US >= (US_PER_S / SOLENOID_PWM_FREQ_HZ),
               "la fenetre d'echantillonnage doit couvrir au moins une periode PWM");
_Static_assert((IPROPI_SELFTEST_ADC_SAMPLE_WINDOW_US + IPROPI_SELFTEST_ADC_TAIL_MARGIN_US) <
                   (IPROPI_PROBE_MS_APPLIED * US_PER_MS),
               "fenetre + marge doivent se terminer avant la coupure de l'impulsion PROBE");

typedef enum {
    SELFTEST_IDLE = 0,
    SELFTEST_MUX_SETTLING,
    SELFTEST_PROBING,
    SELFTEST_WAIT_IDLE,
} selftest_state_t;

static const uint8_t g_channel_to_mux_input[MOTEUR_CHANNEL_COUNT] = {9u, 10u, 11u, 12u, 13u, 14u,
                                                                     1u, 2u, 5u, 3u, 4u, 6u};

static solenoid_driver_t *g_drivers;
static uint8_t g_driver_count;
static uint16_t g_uid_tag;
static selftest_state_t g_state;
static uint8_t g_channel;
static uint32_t g_state_since_us;
static bool g_sample_taken;
static bool g_button_prev_pressed;
static bool g_button_consumed;
static uint32_t g_button_since_us;
static bool g_result_ready;
static bool g_calibration_mode;
static uint16_t g_idle_avg;
static can_announce_status_t g_result;
static can_ipropi_raw_t g_raw_reports[CAN_IPROPI_CHANNEL_COUNT];
static uint8_t g_raw_count;
static can_ipropi_ohm_t g_ohm_reports[CAN_IPROPI_CHANNEL_COUNT];
static uint8_t g_ohm_count;

static void mux_disable(void) {
    gpio_put(MOTEUR_MUX_EN_GPIO, true);
}

static void mux_select_channel(uint8_t channel) {
    const uint8_t mux_input = g_channel_to_mux_input[channel];
    gpio_put(MOTEUR_MUX_S0_GPIO, (mux_input & 0x01u) != 0u);
    gpio_put(MOTEUR_MUX_S1_GPIO, (mux_input & 0x02u) != 0u);
    gpio_put(MOTEUR_MUX_S2_GPIO, (mux_input & 0x04u) != 0u);
    gpio_put(MOTEUR_MUX_S3_GPIO, (mux_input & 0x08u) != 0u);
}

static uint16_t average_adc_samples(void) {
    uint32_t sum = 0u;

    for (uint8_t i = 0u; i < IPROPI_SELFTEST_RAW_SAMPLES; ++i) {
        if (i > 0u) {
            // Étalement de la fenêtre : couvre au moins une période PWM et
            // moyenne l'ondulation de courant (ADR-0009 §5b).
            busy_wait_us_32(IPROPI_SELFTEST_ADC_SAMPLE_SPACING_US);
        }
        sum += adc_read();
    }
    return (uint16_t)((sum + (IPROPI_SELFTEST_RAW_SAMPLES / 2u)) / IPROPI_SELFTEST_RAW_SAMPLES);
}

// Instant, mesuré depuis le début de l'impulsion PROBE, où démarrer la fenêtre :
// calée sur la FIN de l'impulsion, moins la marge de sécurité.
static uint32_t probe_sample_delay_us(void) {
    const uint32_t duration_us = solenoid_driver_probe_duration_us();
    const uint32_t lead_us =
        IPROPI_SELFTEST_ADC_SAMPLE_WINDOW_US + IPROPI_SELFTEST_ADC_TAIL_MARGIN_US;

    return (duration_us > lead_us) ? (uint32_t)(duration_us - lead_us) : 0u;
}

// Constantes électriques + duty PROBE réellement appliqué (ADR-0009 §5c).
static ipropi_ohm_config_t ohm_config(void) {
    const ipropi_ohm_config_t config = {
        .vm_millivolt = SOLENOID_VM_MILLIVOLT,
        .vref_millivolt = SOLENOID_ADC_VREF_MILLIVOLT,
        .ipropi_ua_per_a = SOLENOID_IPROPI_UA_PER_A,
        .ipropi_resistor_ohm = SOLENOID_IPROPI_RESISTOR_OHM,
        .rdson_milliohm = SOLENOID_RDSON_MILLIOHM,
        .duty_permil = solenoid_driver_probe_duty_permil(),
    };

    return config;
}

static uint16_t occupancy_delta_adc(uint16_t probe_avg, uint16_t idle_avg) {
    if (probe_avg <= idle_avg) {
        return 0u;
    }
    return (uint16_t)(probe_avg - idle_avg);
}

static bool fault_is_active(uint8_t channel) {
    return !gpio_get(g_drivers[channel].fault_gpio);
}

static uint16_t non_idle_bits(void) {
    uint16_t bits = 0u;

    for (uint8_t i = 0u; i < g_driver_count; ++i) {
        if (solenoid_driver_state(&g_drivers[i]) != SOLENOID_STATE_IDLE) {
            bits |= (uint16_t)(1u << i);
        }
    }
    return bits;
}

static void finish_now(can_selftest_status_t status) {
    g_result.uid_tag = g_uid_tag;
    g_result.test_status = status;
    g_result_ready = true;
    g_state = SELFTEST_IDLE;
    mux_disable();
}

void ipropi_selftest_init(solenoid_driver_t *drivers, uint8_t driver_count, uint16_t uid_tag) {
    g_drivers = drivers;
    g_driver_count = driver_count;
    g_uid_tag = uid_tag;
    g_state = SELFTEST_IDLE;
    g_channel = 0u;
    g_state_since_us = 0u;
    g_sample_taken = false;
    g_button_prev_pressed = false;
    g_button_consumed = false;
    g_button_since_us = 0u;
    g_result_ready = false;
    g_calibration_mode = false;
    g_idle_avg = 0u;
    g_result = (can_announce_status_t){0};
    g_raw_count = 0u;
    g_ohm_count = 0u;
    for (uint8_t i = 0u; i < CAN_IPROPI_CHANNEL_COUNT; ++i) {
        g_raw_reports[i] = (can_ipropi_raw_t){0};
        g_ohm_reports[i] = (can_ipropi_ohm_t){0};
    }

    gpio_init(MOTEUR_MUX_EN_GPIO);
    gpio_put(MOTEUR_MUX_EN_GPIO, true);
    gpio_set_dir(MOTEUR_MUX_EN_GPIO, GPIO_OUT);

    gpio_init(MOTEUR_MUX_S0_GPIO);
    gpio_set_dir(MOTEUR_MUX_S0_GPIO, GPIO_OUT);
    gpio_init(MOTEUR_MUX_S1_GPIO);
    gpio_set_dir(MOTEUR_MUX_S1_GPIO, GPIO_OUT);
    gpio_init(MOTEUR_MUX_S2_GPIO);
    gpio_set_dir(MOTEUR_MUX_S2_GPIO, GPIO_OUT);
    gpio_init(MOTEUR_MUX_S3_GPIO);
    gpio_set_dir(MOTEUR_MUX_S3_GPIO, GPIO_OUT);
    mux_select_channel(0u);

    adc_init();
    adc_gpio_init(MOTEUR_MUX_OUT_GPIO);
    adc_select_input(MOTEUR_MUX_OUT_ADC_INPUT);
}

bool ipropi_selftest_start(uint8_t request_id) {
    if (g_drivers == NULL || g_driver_count == 0u || request_id < CAN_REQUEST_ID_MIN ||
        request_id > CAN_REQUEST_ID_MAX || g_state != SELFTEST_IDLE || g_result_ready) {
        return false;
    }

    g_result = (can_announce_status_t){
        .uid_tag = g_uid_tag,
        .occupation_bits = 0u,
        .reply_to = request_id,
        .test_status = CAN_SELFTEST_STATUS_OK,
        .fault_bits = 0u,
    };

    const uint16_t busy_bits = non_idle_bits();
    if (busy_bits != 0u) {
        g_result.occupation_bits = busy_bits;
        finish_now(CAN_SELFTEST_STATUS_REFUSED_BUSY);
        return true;
    }

    g_channel = 0u;
    g_sample_taken = false;
    g_calibration_mode = false;
    g_idle_avg = 0u;
    g_raw_count = 0u;
    g_ohm_count = 0u;
    for (uint8_t i = 0u; i < CAN_IPROPI_CHANNEL_COUNT; ++i) {
        g_raw_reports[i] = (can_ipropi_raw_t){0};
        g_ohm_reports[i] = (can_ipropi_ohm_t){0};
    }
    mux_select_channel(g_channel);
    gpio_put(MOTEUR_MUX_EN_GPIO, false);
    g_state_since_us = 0u;
    g_state = SELFTEST_MUX_SETTLING;
    return true;
}

static bool ipropi_selftest_start_internal(const can_ipropi_selftest_request_t *request) {
    if (request == NULL || !ipropi_selftest_start(request->request_id)) {
        return false;
    }
    g_calibration_mode = request->calibration_mode;
    return true;
}

void ipropi_selftest_tick(uint32_t now_us) {
    if (g_state == SELFTEST_IDLE) {
        return;
    }
    if (g_state_since_us == 0u) {
        g_state_since_us = now_us;
    }

    switch (g_state) {
    case SELFTEST_MUX_SETTLING:
        if ((uint32_t)(now_us - g_state_since_us) < IPROPI_SELFTEST_MUX_SETTLE_US) {
            return;
        }
        g_idle_avg = average_adc_samples();
        if (solenoid_driver_probe_start(&g_drivers[g_channel], now_us) != SOLENOID_OK) {
            g_result.occupation_bits = non_idle_bits();
            finish_now(CAN_SELFTEST_STATUS_ABORTED);
            return;
        }
        if (g_calibration_mode && g_channel < CAN_IPROPI_CHANNEL_COUNT) {
            g_raw_reports[g_channel] = (can_ipropi_raw_t){
                .uid_tag = g_uid_tag,
                .reply_to = g_result.reply_to,
                .channel = g_channel,
                .idle_avg = g_idle_avg,
                .probe_avg = 0u,
            };
        }
        g_sample_taken = false;
        g_state_since_us = now_us;
        g_state = SELFTEST_PROBING;
        return;

    case SELFTEST_PROBING:
        if (!g_sample_taken) {
            const uint32_t elapsed_us = (uint32_t)(now_us - g_state_since_us);

            if (elapsed_us >= solenoid_driver_probe_duration_us()) {
                // Fenêtre d'échantillonnage ratée (boucle bloquée) : la mesure
                // serait fausse (courant déjà coupé). Test avorté et signalé,
                // jamais une occupation silencieusement erronée (invariant 1).
                g_result.occupation_bits = non_idle_bits();
                finish_now(CAN_SELFTEST_STATUS_ABORTED);
                return;
            }
            if (elapsed_us >= probe_sample_delay_us()) {
                const bool fault = fault_is_active(g_channel);
                const uint16_t probe_avg = average_adc_samples();
                const uint16_t delta_adc = occupancy_delta_adc(probe_avg, g_idle_avg);

                if (g_calibration_mode && g_channel < CAN_IPROPI_CHANNEL_COUNT) {
                    const ipropi_ohm_config_t config = ohm_config();
                    const ipropi_ohm_result_t ohm = ipropi_ohm_compute(
                        &config, probe_avg, g_idle_avg, IPROPI_SELFTEST_THRESHOLD_DELTA_ADC);

                    g_raw_reports[g_channel].probe_avg = probe_avg;
                    if (g_raw_count < (g_channel + 1u)) {
                        g_raw_count = (uint8_t)(g_channel + 1u);
                    }
                    g_ohm_reports[g_channel] = (can_ipropi_ohm_t){
                        .uid_tag = g_uid_tag,
                        .reply_to = g_result.reply_to,
                        .channel = g_channel,
                        .ohm_milliohm = ohm.ohm_milliohm,
                        .i100_ma = ohm.i100_ma,
                    };
                    if (g_ohm_count < (g_channel + 1u)) {
                        g_ohm_count = (uint8_t)(g_channel + 1u);
                    }
                }

                if (fault) {
                    g_result.fault_bits |= (uint16_t)(1u << g_channel);
                    g_result.occupation_bits &= (uint16_t)~(uint16_t)(1u << g_channel);
                } else if (delta_adc >= IPROPI_SELFTEST_THRESHOLD_DELTA_ADC) {
                    g_result.occupation_bits |= (uint16_t)(1u << g_channel);
                }
                g_sample_taken = true;
            }
        }
        if (solenoid_driver_state(&g_drivers[g_channel]) == SOLENOID_STATE_IDLE ||
            solenoid_driver_state(&g_drivers[g_channel]) == SOLENOID_STATE_FAULT) {
            g_state = SELFTEST_WAIT_IDLE;
        }
        return;

    case SELFTEST_WAIT_IDLE:
        if (fault_is_active(g_channel)) {
            g_result.fault_bits |= (uint16_t)(1u << g_channel);
            g_result.occupation_bits &= (uint16_t)~(uint16_t)(1u << g_channel);
        }
        ++g_channel;
        if (g_channel >= g_driver_count) {
            finish_now(g_result.test_status);
            return;
        }
        mux_disable();
        mux_select_channel(g_channel);
        gpio_put(MOTEUR_MUX_EN_GPIO, false);
        g_state_since_us = now_us;
        g_state = SELFTEST_MUX_SETTLING;
        return;

    case SELFTEST_IDLE:
    default: return;
    }
}

bool ipropi_selftest_take_result(ipropi_selftest_result_t *out) {
    if (!g_result_ready || out == NULL) {
        return false;
    }
    out->report_ready = true;
    out->report = g_result;
    out->raw_count = g_raw_count;
    out->ohm_count = g_ohm_count;
    for (uint8_t i = 0u; i < CAN_IPROPI_CHANNEL_COUNT; ++i) {
        out->raw_reports[i] = g_raw_reports[i];
        out->ohm_reports[i] = g_ohm_reports[i];
    }
    g_result_ready = false;
    g_calibration_mode = false;
    g_idle_avg = 0u;
    g_raw_count = 0u;
    g_ohm_count = 0u;
    g_result = (can_announce_status_t){0};
    for (uint8_t i = 0u; i < CAN_IPROPI_CHANNEL_COUNT; ++i) {
        g_raw_reports[i] = (can_ipropi_raw_t){0};
        g_ohm_reports[i] = (can_ipropi_ohm_t){0};
    }
    return true;
}

bool ipropi_selftest_start_request(const can_ipropi_selftest_request_t *request) {
    return ipropi_selftest_start_internal(request);
}

bool ipropi_selftest_is_running(void) {
    return g_state != SELFTEST_IDLE;
}

bool ipropi_selftest_button_update(bool pressed, uint32_t now_us) {
    if (!pressed) {
        g_button_prev_pressed = false;
        g_button_consumed = false;
        return false;
    }
    if (!g_button_prev_pressed) {
        g_button_prev_pressed = true;
        g_button_since_us = now_us;
    }
    if (!g_button_consumed && (uint32_t)(now_us - g_button_since_us) >= IPROPI_SELFTEST_BUTTON_HOLD_US) {
        g_button_consumed = true;
        return true;
    }
    return false;
}
