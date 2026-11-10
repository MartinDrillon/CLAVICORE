#include "listen_core1.h"

#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "config.h"
#include "ipropi_mux.h"
#include "listen_noise_ring.h"
#include "pulse_detector.h"

#define PULSE_RING_MASK (LISTEN_PULSE_RING_SIZE - 1u)
#define STATUS_RING_MASK (LISTEN_STATUS_RING_SIZE - 1u)
#define STREAM_RING_MASK (LISTEN_STREAM_RING_SIZE - 1u)

#define LISTEN_STATUS_PERIOD_US 1000000u

_Static_assert((LISTEN_PULSE_RING_SIZE & PULSE_RING_MASK) == 0u, "taille d'anneau non puissance de 2");
_Static_assert((LISTEN_STATUS_RING_SIZE & STATUS_RING_MASK) == 0u,
               "taille d'anneau non puissance de 2");
_Static_assert((LISTEN_STREAM_RING_SIZE & STREAM_RING_MASK) == 0u,
               "taille d'anneau non puissance de 2");
_Static_assert(CAN_LISTEN_TRACE_FRAME_SAMPLES == 4u, "LISTEN_TRACE attend 4 echantillons");
_Static_assert(CAN_LISTEN_TRACE_SAMPLES == 4096u, "LISTEN_TRACE attend 4096 echantillons");
_Static_assert(MOTEUR_CHANNEL_COUNT == CAN_IPROPI_CHANNEL_COUNT, "canaux Moteur != protocole");

// --- Anneaux SPSC Core 1 (producteur) -> Core 0 (consommateur) ---------------
// Règle : le producteur écrit la case PUIS __dmb() PUIS head ; le consommateur
// lit head, __dmb(), la case, puis avance tail (même schéma que can_link.c).
static listen_pulse_evt_t s_pulse_ring[LISTEN_PULSE_RING_SIZE];
static volatile uint32_t s_pulse_head;
static volatile uint32_t s_pulse_tail;

static listen_status_evt_t s_status_ring[LISTEN_STATUS_RING_SIZE];
static volatile uint32_t s_status_head;
static volatile uint32_t s_status_tail;

static uint16_t s_stream_ring[LISTEN_STREAM_RING_SIZE];
static volatile uint32_t s_stream_head;
static volatile uint32_t s_stream_tail;

// --- Trace : tampon statique unique de 4096 échantillons (8 Ko) -------------
static uint16_t s_trace_buf[CAN_LISTEN_TRACE_SAMPLES];
static volatile bool s_trace_ready;

// --- État partagé -----------------------------------------------------------
// Un seul écrivain de `s_mode` : le Core 0. `s_params` n'est écrit qu'en OFF,
// avant le drapeau de mode, avec barrières.
static volatile uint8_t s_mode;
static volatile uint8_t s_target_channel;
static volatile uint16_t s_target_uid_tag;
static volatile uint32_t s_request_epoch;
static volatile uint16_t s_drive_mask;
static volatile bool s_params_valid;
static volatile bool s_parked;
static volatile uint32_t s_sweep_period_us;
static volatile uint8_t s_overflows;

static listen_params_t s_params;
static uint16_t s_params_crc;
static uint8_t s_uid[CAN_UID_SIZE];
static uint16_t s_board_tag;
static solenoid_driver_t *s_drivers;
static uint8_t s_driver_count;
static bool s_launched;

// --- État Core 1 uniquement -------------------------------------------------
static pulse_detector_t s_pd[MOTEUR_CHANNEL_COUNT];
static listen_noise_ring_t s_noise[MOTEUR_CHANNEL_COUNT];
static uint32_t s_samples_masked;

static void note_overflow(void) {
    // Débordement jamais silencieux : compteur saturant, exposé par STATUS.
    if (s_overflows < 255u) {
        ++s_overflows;
    }
}

static void push_status(can_listen_state_t state, uint8_t refused_param_id) {
    const uint32_t head = s_status_head;

    if ((uint32_t)(head - s_status_tail) >= LISTEN_STATUS_RING_SIZE) {
        note_overflow();
        return;
    }
    listen_status_evt_t *evt = &s_status_ring[head & STATUS_RING_MASK];

    evt->state = state;
    evt->refused_param_id = refused_param_id;
    evt->sweep_period_us = (uint16_t)s_sweep_period_us;
    evt->overflows = s_overflows;
    __dmb();
    s_status_head = head + 1u;
}

static bool push_pulse(uint8_t channel, uint8_t seq, const pulse_event_t *evt) {
    const uint32_t head = s_pulse_head;

    if ((uint32_t)(head - s_pulse_tail) >= LISTEN_PULSE_RING_SIZE) {
        note_overflow();
        return false;  // impulsion perdue : comptée, jamais inventée
    }
    listen_pulse_evt_t *out = &s_pulse_ring[head & PULSE_RING_MASK];

    out->channel = channel;
    out->seq = seq;
    out->base = evt->base;
    out->peak = evt->peak;
    out->t_peak_us = evt->t_peak_us;
    out->t_end_us = evt->t_end_us;
    out->split = evt->split;
    __dmb();
    s_pulse_head = head + 1u;
    return true;
}

static void push_stream(uint16_t sample) {
    const uint32_t head = s_stream_head;

    if ((uint32_t)(head - s_stream_tail) >= LISTEN_STREAM_RING_SIZE) {
        note_overflow();
        return;
    }
    s_stream_ring[head & STREAM_RING_MASK] = sample;
    __dmb();
    s_stream_head = head + 1u;
}

// --- Acquisition ------------------------------------------------------------

static bool drivers_awake(void) {
    // nSLEEP haut = drivers réveillés (ADR-0024). Lecture seule du registre de
    // sortie : le Core 1 ne pilote aucun solénoïde.
    return gpio_get_out_level(MOTEUR_GLOBAL_SLEEP_GPIO);
}

static bool any_channel_fault(void) {
    for (uint8_t ch = 0u; ch < s_driver_count; ++ch) {
        if (!gpio_get(s_drivers[ch].fault_gpio)) {
            return true;
        }
    }
    return false;
}

static uint16_t read_channel_sample(uint32_t settle_us, uint32_t samples_per_dwell) {
    uint32_t samples = samples_per_dwell;

    if (samples == 0u) {
        samples = 1u;  // garde-fou : validate impose 1..64 (borne structurelle)
    } else if (samples > 64u) {
        samples = 64u;
    }
    if (settle_us > 0u) {
        busy_wait_us_32(settle_us);
    }
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < samples; ++i) {
        sum += adc_read();
    }
    return (uint16_t)((sum + (samples / 2u)) / samples);
}

static void session_begin(uint32_t now_us) {
    const uint32_t now_ms = now_us / 1000u;

    for (uint8_t ch = 0u; ch < MOTEUR_CHANNEL_COUNT; ++ch) {
        // `init` copie le jeu d'écoute courant et redémarre la fenêtre de base.
        pulse_detector_init(&s_pd[ch], &s_params, now_us);
        listen_noise_ring_reset(&s_noise[ch], now_ms);
    }
    s_sweep_period_us = 0u;
    s_samples_masked = 0u;
}

static void sweep_channels(const listen_params_t *cfg, bool stream_active, uint8_t stream_channel,
                           uint32_t *stream_counter, uint8_t *seq, uint16_t *previous_mask,
                           uint32_t *blank_until_us) {
    const uint32_t start_us = time_us_32();
    const uint16_t mask = s_drive_mask;
    // Plafond structurel : la comparaison d'échéance utilise la différence
    // signée 32 bits, valable tant que drive_blank_ms < ~35 min.
    const uint64_t blank_us64 = (uint64_t)cfg->drive_blank_ms * 1000u;
    const uint32_t blank_us =
        blank_us64 > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)blank_us64;

    // Un canal qui SORT du masque (non IDLE -> IDLE) reste invalidé pendant
    // drive_blank_ms : le courant de queue du solénoïde ne doit pas être pris
    // pour une impulsion (ADR-0032 §1).
    for (uint8_t ch = 0u; ch < MOTEUR_CHANNEL_COUNT; ++ch) {
        const bool driven = ((mask >> ch) & 1u) != 0u;
        const bool was_driven = ((*previous_mask >> ch) & 1u) != 0u;

        if (was_driven && !driven) {
            const uint32_t deadline = start_us + blank_us;

            // 0 = pas de masquage (drive_blank_ms nul ou échéance nulle).
            blank_until_us[ch] = (blank_us == 0u || deadline == 0u) ? 0u : deadline;
        }
    }
    *previous_mask = mask;

    uint32_t decim = cfg->stream_decim;
    if (decim == 0u) {
        decim = 1u;
    }

    for (uint8_t ch = 0u; ch < MOTEUR_CHANNEL_COUNT; ++ch) {
        if (s_mode == CAN_LISTEN_MODE_OFF) {
            return;  // parcage demandé pendant le balayage
        }
        const uint32_t now_us = time_us_32();
        const uint16_t current_mask = s_drive_mask;
        bool masked = ((current_mask >> ch) & 1u) != 0u;

        if (!masked && blank_until_us[ch] != 0u) {
            if ((int32_t)(now_us - blank_until_us[ch]) < 0) {
                masked = true;
            } else {
                blank_until_us[ch] = 0u;  // échéance purgée : jamais réarmée par un wrap
            }
        }
        if (masked) {
            ++s_samples_masked;
            continue;  // ni détecteur ni bruit sur un canal piloté/masqué
        }

        (void)ipropi_mux_select(ch);
        const uint16_t sample = read_channel_sample(cfg->settle_us, cfg->samples_per_dwell);
        const uint32_t sample_us = time_us_32();

        listen_noise_ring_record(&s_noise[ch], sample_us / 1000u, sample);

        if (stream_active && ch == stream_channel) {
            ++(*stream_counter);
            if ((*stream_counter % decim) == 0u) {
                push_stream(sample);
            }
        }

        pulse_event_t evt;
        if (pulse_detector_feed(&s_pd[ch], sample, sample_us, &evt)) {
            if (push_pulse(ch, *seq, &evt)) {
                *seq = (uint8_t)(*seq + 1u);
            }
        }
    }

    const uint32_t period_us = (uint32_t)(time_us_32() - start_us);
    s_sweep_period_us = period_us > 0xFFFFu ? 0xFFFFu : period_us;
}

typedef enum {
    TRACE_CAPTURED = 0,
    TRACE_TIMEOUT,
    TRACE_ABORTED,
} trace_status_t;

static void trace_reverse(uint16_t *values, uint32_t count) {
    for (uint32_t i = 0u; i < (count / 2u); ++i) {
        const uint16_t tmp = values[i];

        values[i] = values[count - 1u - i];
        values[count - 1u - i] = tmp;
    }
}

// Rotation en place (trois retournements) : aucun second tampon de 8 Ko.
static void trace_rotate_left(uint16_t *values, uint32_t count, uint32_t shift) {
    if (shift == 0u || shift >= count) {
        return;
    }
    trace_reverse(values, shift);
    trace_reverse(values + shift, count - shift);
    trace_reverse(values, count);
}

static uint32_t trace_mean(const uint16_t *values, uint32_t count) {
    uint32_t sum = 0u;

    for (uint32_t i = 0u; i < count; ++i) {
        sum += values[i];
    }
    return (sum + (count / 2u)) / count;
}

// Capture d'un canal, mux fixe. Pré-déclenchement 25 % (1024), déclenchement
// sur base + trace_trig, abandon à trace_timeout_ms (ADR-0032 §3, §11).
//
// La base vient du détecteur si sa fenêtre initiale est terminée (cas normal
// LISTEN -> TRACE). Au démarrage direct en TRACE, le détecteur est encore en
// INIT et sa base n'existe pas : elle est alors la moyenne des 1024 premiers
// échantillons, c'est-à-dire exactement la fenêtre de pré-déclenchement. Le
// seuil est ensuite appliqué à cette fenêtre (déclenchement rétroactif si
// l'événement tombe dedans), sans jamais modifier la base du détecteur.
static trace_status_t trace_capture(const listen_params_t *cfg, uint8_t channel) {
    uint32_t period_us = cfg->trace_period_us;

    if (period_us < 2u) {
        period_us = 2u;  // borne structurelle : validate impose >= 2
    }
    if (cfg->trace_timeout_ms == 0u) {
        return TRACE_TIMEOUT;
    }
    const uint64_t timeout_us = (uint64_t)cfg->trace_timeout_ms * 1000u;

    (void)ipropi_mux_select(channel);
    const bool detector_ready = (s_pd[channel].state != PULSE_STATE_INIT);
    const uint32_t start_us = time_us_32();
    uint32_t next_us = start_us;
    uint32_t sample_count = 0u;
    uint32_t trigger_count = 0u;
    uint32_t trigger_level = 0u;
    bool triggered = false;

    while (true) {
        if (s_mode != CAN_LISTEN_MODE_TRACE) {
            return TRACE_ABORTED;
        }
        const uint32_t now_us = time_us_32();
        if ((int32_t)(now_us - next_us) < 0) {
            busy_wait_us_32(next_us - now_us);
        }
        next_us += period_us;

        const uint16_t sample = adc_read();
        s_trace_buf[sample_count % CAN_LISTEN_TRACE_SAMPLES] = sample;
        ++sample_count;

        if (sample_count == LISTEN_TRACE_PRE_TRIGGER) {
            const uint32_t base = detector_ready ? pulse_detector_baseline(&s_pd[channel])
                                                 : trace_mean(s_trace_buf, sample_count);
            trigger_level = base + cfg->trace_trig;
            // L'événement peut déjà être dans la fenêtre de pré-déclenchement.
            for (uint32_t i = 0u; i < sample_count; ++i) {
                if ((uint32_t)s_trace_buf[i] > trigger_level) {
                    triggered = true;
                    trigger_count = i + 1u;
                    break;
                }
            }
        }

        if (sample_count < LISTEN_TRACE_PRE_TRIGGER) {
            // Le timeout court dès le début, y compris pendant la fenêtre de
            // pré-déclenchement.
            if ((uint64_t)(uint32_t)(time_us_32() - start_us) >= timeout_us) {
                return TRACE_TIMEOUT;
            }
        } else if (!triggered) {
            if ((uint32_t)sample > trigger_level) {
                triggered = true;
                trigger_count = sample_count;
            } else if ((uint64_t)(uint32_t)(time_us_32() - start_us) >= timeout_us) {
                return TRACE_TIMEOUT;
            }
        }
        if (triggered && sample_count >= CAN_LISTEN_TRACE_SAMPLES &&
            sample_count >=
                trigger_count + (CAN_LISTEN_TRACE_SAMPLES - LISTEN_TRACE_PRE_TRIGGER)) {
            break;  // 1024 échantillons avant le déclenchement, 3072 après
        }
    }

    trace_rotate_left(s_trace_buf, CAN_LISTEN_TRACE_SAMPLES,
                      sample_count % CAN_LISTEN_TRACE_SAMPLES);
    __dmb();
    s_trace_ready = true;
    return TRACE_CAPTURED;
}

static void trace_wait_release(void) {
    // Le Core 0 lit les 4096 échantillons puis appelle trace_release().
    while (s_trace_ready && s_mode != CAN_LISTEN_MODE_OFF) {
        tight_loop_contents();
    }
}

static can_listen_state_t active_state(can_listen_mode_t mode, bool fault, bool trace_pending) {
    if (fault) {
        return CAN_LISTEN_STATE_FAULT;
    }
    if (mode == CAN_LISTEN_MODE_TRACE && trace_pending) {
        return CAN_LISTEN_STATE_TRACE;
    }
    if (mode == CAN_LISTEN_MODE_STREAM) {
        return CAN_LISTEN_STATE_STREAM;
    }
    return CAN_LISTEN_STATE_LISTENING;
}

static void listen_core1_main(void) {
    bool active = false;
    bool trace_pending = false;
    uint32_t trace_epoch_seen = 0u;
    uint8_t seq = 0u;
    uint32_t stream_counter = 0u;
    uint16_t previous_mask = 0u;
    uint32_t blank_until_us[MOTEUR_CHANNEL_COUNT] = {0};
    can_listen_state_t last_state = CAN_LISTEN_STATE_IDLE;
    uint32_t last_status_us = time_us_32();

    s_parked = true;
    push_status(CAN_LISTEN_STATE_IDLE, 0u);

    while (true) {
        const can_listen_mode_t mode = (can_listen_mode_t)s_mode;
        __dmb();

        if (mode == CAN_LISTEN_MODE_OFF) {
            if (active || !s_parked) {
                ipropi_mux_disable();
                active = false;
                // Une capture interrompue par OFF peut avoir publié une trace
                // juste après le dernier contrôle de mode : elle est périmée et
                // doit être invalidée AVANT de parquer, sinon le Core 0 peut
                // l'émettre (listen_trace_pump) et forcer un retour LISTEN.
                s_trace_ready = false;
                __dmb();
                s_parked = true;
                push_status(CAN_LISTEN_STATE_IDLE, 0u);
                last_state = CAN_LISTEN_STATE_IDLE;
                last_status_us = time_us_32();
            } else if ((uint32_t)(time_us_32() - last_status_us) >= LISTEN_STATUS_PERIOD_US) {
                push_status(CAN_LISTEN_STATE_IDLE, 0u);
                last_status_us = time_us_32();
            }
            tight_loop_contents();
            continue;
        }

        s_parked = false;

        const uint32_t epoch = s_request_epoch;
        if (epoch != trace_epoch_seen) {
            trace_epoch_seen = epoch;
            if (mode == CAN_LISTEN_MODE_TRACE) {
                trace_pending = true;
            }
        }

        if (!active) {
            session_begin(time_us_32());
            active = true;
            seq = 0u;
            stream_counter = 0u;
            previous_mask = 0u;
            memset(blank_until_us, 0, sizeof(blank_until_us));
            trace_pending = (mode == CAN_LISTEN_MODE_TRACE);
        }

        if (!s_params_valid) {
            // Jamais d'acquisition avec un jeu absent : état nommé, mux rendu.
            ipropi_mux_disable();
            if (last_state != CAN_LISTEN_STATE_PARAM_MISMATCH) {
                push_status(CAN_LISTEN_STATE_PARAM_MISMATCH, 0u);
                last_state = CAN_LISTEN_STATE_PARAM_MISMATCH;
                last_status_us = time_us_32();
            }
            busy_wait_us_32(1000u);
            active = false;
            continue;
        }

        if (!drivers_awake()) {
            ipropi_mux_disable();
            if (last_state != CAN_LISTEN_STATE_UNAVAILABLE_SLEEP) {
                push_status(CAN_LISTEN_STATE_UNAVAILABLE_SLEEP, 0u);
                last_state = CAN_LISTEN_STATE_UNAVAILABLE_SLEEP;
                last_status_us = time_us_32();
            }
            busy_wait_us_32(1000u);
            continue;
        }

        const bool fault = any_channel_fault();
        ipropi_mux_enable();

        if (mode == CAN_LISTEN_MODE_TRACE && trace_pending) {
            trace_pending = false;
            push_status(CAN_LISTEN_STATE_TRACE, 0u);
            last_state = CAN_LISTEN_STATE_TRACE;
            last_status_us = time_us_32();

            const trace_status_t status = trace_capture(&s_params, s_target_channel);
            if (status == TRACE_CAPTURED) {
                trace_wait_release();
            } else if (status == TRACE_TIMEOUT) {
                push_status(CAN_LISTEN_STATE_TRACE_TIMEOUT, 0u);
                last_state = CAN_LISTEN_STATE_TRACE_TIMEOUT;
                last_status_us = time_us_32();
            }
            // TRACE_ABORTED : le mode a changé ; la boucle reprend l'état réel.
        } else {
            const bool stream_active = (mode == CAN_LISTEN_MODE_STREAM);

            sweep_channels(&s_params, stream_active, s_target_channel, &stream_counter, &seq,
                           &previous_mask, blank_until_us);
        }

        // STATUS sur changement d'état, ou 1x/s (jamais silencieux).
        // Le mode peut avoir changé pendant la capture/le balayage : on relit
        // avant de nommer l'état, et OFF est traité en tête de boucle.
        const can_listen_mode_t final_mode = (can_listen_mode_t)s_mode;
        if (final_mode == CAN_LISTEN_MODE_OFF) {
            continue;
        }
        const can_listen_state_t state = active_state(final_mode, fault, trace_pending);
        const uint32_t now_us = time_us_32();
        if (state != last_state ||
            (uint32_t)(now_us - last_status_us) >= LISTEN_STATUS_PERIOD_US) {
            push_status(state, 0u);
            last_state = state;
            last_status_us = now_us;
        }
    }
}

// --- API Core 0 -------------------------------------------------------------

void listen_core1_init(const uint8_t uid[CAN_UID_SIZE], uint16_t uid_tag,
                       solenoid_driver_t *drivers, uint8_t driver_count) {
    s_pulse_head = 0u;
    s_pulse_tail = 0u;
    s_status_head = 0u;
    s_status_tail = 0u;
    s_stream_head = 0u;
    s_stream_tail = 0u;
    s_trace_ready = false;
    s_mode = (uint8_t)CAN_LISTEN_MODE_OFF;
    s_target_channel = 0u;
    s_target_uid_tag = 0u;
    s_request_epoch = 0u;
    s_drive_mask = 0u;
    s_params_valid = false;
    s_parked = true;
    s_sweep_period_us = 0u;
    s_overflows = 0u;
    memset(&s_params, 0, sizeof(s_params));
    s_params_crc = 0u;
    s_board_tag = uid_tag;
    s_drivers = drivers;
    s_driver_count = driver_count;
    s_launched = false;
    s_samples_masked = 0u;
    if (uid != NULL) {
        memcpy(s_uid, uid, CAN_UID_SIZE);
    } else {
        memset(s_uid, 0, sizeof(s_uid));
    }
}

void listen_core1_launch(void) {
    if (s_launched) {
        return;
    }
    s_launched = true;
    multicore_launch_core1(listen_core1_main);
}

bool listen_core1_active(void) {
    return s_mode != (uint8_t)CAN_LISTEN_MODE_OFF;
}

bool listen_core1_apply_params(const listen_params_t *params, uint16_t crc) {
    if (params == NULL) {
        s_params_valid = false;
        return false;
    }
    if (s_mode != (uint8_t)CAN_LISTEN_MODE_OFF) {
        // Contrat : OFF seulement. Refus explicite, jamais silencieux : le
        // Core 0 doit refuser le démarrage (PARAM_MISMATCH) si false.
        return false;
    }
    s_params = *params;
    s_params_crc = crc;
    s_params_valid = true;
    __dmb();
    return true;
}

uint16_t listen_core1_params_crc(void) {
    return s_params_valid ? s_params_crc : 0u;
}

bool listen_core1_set_mode(can_listen_mode_t mode, uint8_t channel, uint16_t target_uid_tag) {
    if (!can_listen_mode_is_valid(mode)) {
        return false;
    }

    can_listen_mode_t effective = mode;
    if (effective == CAN_LISTEN_MODE_STREAM_OFF) {
        effective = CAN_LISTEN_MODE_LISTEN;  // retour au balayage sans quitter l'écoute
    }

    if (effective == CAN_LISTEN_MODE_TRACE || effective == CAN_LISTEN_MODE_STREAM) {
        if (!can_listen_channel_is_valid(channel) || target_uid_tag == 0u) {
            return false;
        }
    } else {
        channel = 0u;
        target_uid_tag = 0u;
    }

    if (effective != CAN_LISTEN_MODE_OFF && !s_params_valid) {
        return false;
    }

    if (effective == CAN_LISTEN_MODE_OFF) {
        s_mode = (uint8_t)CAN_LISTEN_MODE_OFF;
        __dmb();
        s_params_valid = false;
        // Si une trace était prête, la libérer pour que le Core 1 se pare.
        s_trace_ready = false;
        __dmb();
        return true;
    }

    if (s_mode == (uint8_t)CAN_LISTEN_MODE_OFF) {
        // Core 1 parqué : les anneaux peuvent être vidés sans course.
        s_pulse_tail = s_pulse_head;
        s_status_tail = s_status_head;
        s_stream_tail = s_stream_head;
    }

    s_target_channel = channel;
    s_target_uid_tag = target_uid_tag;
    ++s_request_epoch;
    __dmb();
    s_mode = (uint8_t)effective;
    __dmb();
    return true;
}

void listen_core1_set_drive_mask(uint16_t non_idle_mask) {
    s_drive_mask = non_idle_mask;
    __dmb();
}

bool listen_core1_wait_parked(uint32_t timeout_us) {
    const uint32_t start_us = time_us_32();

    while (!s_parked) {
        if (timeout_us == 0u) {
            return false;
        }
        if ((uint32_t)(time_us_32() - start_us) >= timeout_us) {
            return false;
        }
        tight_loop_contents();
    }
    return true;
}

bool listen_core1_take_pulse(listen_pulse_evt_t *out) {
    if (out == NULL) {
        return false;
    }
    const uint32_t tail = s_pulse_tail;

    if (tail == s_pulse_head) {
        return false;
    }
    __dmb();
    *out = s_pulse_ring[tail & PULSE_RING_MASK];
    __dmb();
    s_pulse_tail = tail + 1u;
    return true;
}

bool listen_core1_take_status(listen_status_evt_t *out) {
    if (out == NULL) {
        return false;
    }
    const uint32_t tail = s_status_tail;

    if (tail == s_status_head) {
        return false;
    }
    __dmb();
    *out = s_status_ring[tail & STATUS_RING_MASK];
    __dmb();
    s_status_tail = tail + 1u;
    return true;
}

bool listen_core1_take_stream(uint16_t *out) {
    if (out == NULL) {
        return false;
    }
    const uint32_t tail = s_stream_tail;

    if (tail == s_stream_head) {
        return false;
    }
    __dmb();
    *out = s_stream_ring[tail & STREAM_RING_MASK];
    __dmb();
    s_stream_tail = tail + 1u;
    return true;
}

bool listen_core1_take_noise(uint8_t channel, uint16_t lookback_ms, uint16_t *min,
                             uint16_t *max, bool *truncated) {
    uint16_t slices_valid = 0u;

    if (channel >= MOTEUR_CHANNEL_COUNT) {
        return false;
    }
    return listen_noise_ring_query(&s_noise[channel], time_us_32() / 1000u, lookback_ms, min, max,
                                   truncated, &slices_valid);
}

uint32_t listen_core1_sweep_period_us(void) {
    return s_sweep_period_us;
}

uint8_t listen_core1_overflow_count(void) {
    return s_overflows;
}

bool listen_core1_trace_ready(void) {
    return s_trace_ready;
}

uint16_t listen_core1_trace_sample(uint32_t index) {
    if (index >= CAN_LISTEN_TRACE_SAMPLES) {
        return 0u;
    }
    return s_trace_buf[index];
}

void listen_core1_trace_release(void) {
    s_trace_ready = false;
    __dmb();
}
