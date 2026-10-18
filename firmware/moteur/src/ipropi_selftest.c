#include "ipropi_selftest.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"

#include "config.h"

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
static can_announce_status_t g_result;

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

static bool threshold_is_default(void) {
    return IPROPI_SELFTEST_THRESHOLD_ADC == IPROPI_SELFTEST_THRESHOLD_ADC_DEFAULT;
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
    g_result = (can_announce_status_t){0};

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
        .test_status = threshold_is_default() ? CAN_SELFTEST_STATUS_UNCALIBRATED
                                              : CAN_SELFTEST_STATUS_OK,
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
    mux_select_channel(g_channel);
    gpio_put(MOTEUR_MUX_EN_GPIO, false);
    g_state_since_us = 0u;
    g_state = SELFTEST_MUX_SETTLING;
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
        if (solenoid_driver_probe_start(&g_drivers[g_channel], now_us) != SOLENOID_OK) {
            g_result.occupation_bits = non_idle_bits();
            finish_now(CAN_SELFTEST_STATUS_ABORTED);
            return;
        }
        g_sample_taken = false;
        g_state_since_us = now_us;
        g_state = SELFTEST_PROBING;
        return;

    case SELFTEST_PROBING:
        if (!g_sample_taken &&
            (uint32_t)(now_us - g_state_since_us) >= IPROPI_SELFTEST_ADC_SAMPLE_DELAY_US) {
            const bool fault = fault_is_active(g_channel);
            const uint16_t sample = adc_read();

            if (fault) {
                g_result.fault_bits |= (uint16_t)(1u << g_channel);
                g_result.occupation_bits &= (uint16_t)~(uint16_t)(1u << g_channel);
            } else if (sample >= IPROPI_SELFTEST_THRESHOLD_ADC) {
                g_result.occupation_bits |= (uint16_t)(1u << g_channel);
            }
            g_sample_taken = true;
        }
        if (solenoid_driver_state(&g_drivers[g_channel]) == SOLENOID_STATE_IDLE) {
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
    g_result_ready = false;
    g_result = (can_announce_status_t){0};
    return true;
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
