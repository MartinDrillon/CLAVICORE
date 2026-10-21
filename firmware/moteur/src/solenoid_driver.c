#include "solenoid_driver.h"

#include "hardware/gpio.h"

#include "config.h"
#include "solenoid_profile.h"

#define US_PER_MS 1000u

// Limites vérifiées à la compilation : un profil incohérent ne compile pas.
_Static_assert(SOLENOID_PULL_DUTY_PERCENT <= 100u && SOLENOID_HOLD_DUTY_PERCENT <= 100u &&
                   SOLENOID_RELEASE_DUTY_PERCENT <= 100u && SOLENOID_PROBE_DUTY_PERCENT <= 100u &&
                   SOLENOID_MAINT_DUTY_PERCENT <= 100u &&
                   SOLENOID_DUTY_MAX_PERCENT <= 100u && SOLENOID_PROBE_DUTY_MAX_PERCENT <= 100u &&
                   SOLENOID_MAINT_DUTY_MAX_PERCENT <= 100u,
               "duty > 100 %");
_Static_assert(SOLENOID_HOLD_MAX_MS > 0u && SOLENOID_PULL_DURATION_MS > 0u &&
                   SOLENOID_PROBE_DURATION_MS > 0u && SOLENOID_MAINT_MAX_MS > 0u,
               "durées nulles");
// ADR-0007 : HOLD borné à 20 s et au duty validé thermiquement ; `* US_PER_MS` tient en uint32_t.
_Static_assert(SOLENOID_HOLD_DUTY_PERCENT <= SOLENOID_HOLD_DUTY_MAX_PERCENT, "duty HOLD > plafond ADR-0007");
_Static_assert(SOLENOID_HOLD_MAX_MS <= 4000000u, "SOLENOID_HOLD_MAX_MS deborde en us sur 32 bits");
_Static_assert(SOLENOID_DIR_LEVEL_PULL != SOLENOID_DIR_LEVEL_RELEASE, "polarités PULL/RELEASE identiques");
// ADR-0012 : l'enveloppe thermique validée (équivalent de 10 s à 100 % de duty)
// est une borne de compilation : un réglage de banc de durée ou de duty ne peut
// pas la dépasser sans casser le build.
_Static_assert(SOLENOID_MAINT_DUTY_PERCENT <= SOLENOID_MAINT_DUTY_MAX_PERCENT,
               "duty MAINTENANCE > plafond ADR-0012");
_Static_assert(SOLENOID_MAINT_MAX_MS * SOLENOID_MAINT_DUTY_PERCENT <=
                   SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT,
               "profil MAINTENANCE hors enveloppe thermique validee (ADR-0012)");
_Static_assert(SOLENOID_MAINT_MAX_MS <= 4000000u,
               "SOLENOID_MAINT_MAX_MS deborde en us sur 32 bits");

static uint32_t clamp_duty(uint32_t percent) {
    return percent > SOLENOID_DUTY_MAX_PERCENT ? SOLENOID_DUTY_MAX_PERCENT : percent;
}

static uint32_t pull_duration_us(void) {
    const uint32_t ms = SOLENOID_PULL_DURATION_MS > SOLENOID_PULL_MAX_MS ? SOLENOID_PULL_MAX_MS
                                                                          : SOLENOID_PULL_DURATION_MS;
    return ms * US_PER_MS;
}

static uint32_t clamp_hold_duty(uint32_t percent) {
    const uint32_t duty = clamp_duty(percent);
    return duty > SOLENOID_HOLD_DUTY_MAX_PERCENT ? SOLENOID_HOLD_DUTY_MAX_PERCENT : duty;
}

static uint32_t probe_duration_us(void) {
    const uint32_t ms = SOLENOID_PROBE_DURATION_MS > SOLENOID_PROBE_MAX_MS ? SOLENOID_PROBE_MAX_MS
                                                                            : SOLENOID_PROBE_DURATION_MS;
    return ms * US_PER_MS;
}

static uint32_t probe_duty_percent(void) {
    const uint32_t capped = SOLENOID_PROBE_DUTY_PERCENT > SOLENOID_PROBE_DUTY_MAX_PERCENT
                                ? SOLENOID_PROBE_DUTY_MAX_PERCENT
                                : SOLENOID_PROBE_DUTY_PERCENT;
    return clamp_duty(capped);
}

// Paramètres PROBE réellement appliqués (après écrêtage). L'auto-test IPROPI
// s'en sert pour positionner sa fenêtre de mesure et estimer la résistance
// (ADR-0009 §5) : il ne doit jamais relire les constantes brutes du profil.
uint32_t solenoid_driver_probe_duration_us(void) {
    return probe_duration_us();
}

uint32_t solenoid_driver_probe_duty_permil(void) {
    return probe_duty_percent() * 10u;
}

static uint32_t maint_duration_us(void) {
    return SOLENOID_MAINT_MAX_MS * US_PER_MS;
}

static uint32_t maint_duty_percent(void) {
    const uint32_t capped = SOLENOID_MAINT_DUTY_PERCENT > SOLENOID_MAINT_DUTY_MAX_PERCENT
                                ? SOLENOID_MAINT_DUTY_MAX_PERCENT
                                : SOLENOID_MAINT_DUTY_PERCENT;
    return clamp_duty(capped);
}

static bool elapsed(uint32_t now_us, uint32_t start_us, uint32_t duration_us) {
    return (uint32_t)(now_us - start_us) >= duration_us;  // robuste au débordement de 32 bits
}

// --- global_sleep ---
static uint32_t g_wake_deadline_us;

void solenoid_global_sleep_init(void) {
    gpio_init(MOTEUR_GLOBAL_SLEEP_GPIO);
    gpio_put(MOTEUR_GLOBAL_SLEEP_GPIO, false);  // nSLEEP bas : drivers endormis
    gpio_set_dir(MOTEUR_GLOBAL_SLEEP_GPIO, GPIO_OUT);
}

void solenoid_global_wake(uint32_t now_us) {
    g_wake_deadline_us = now_us + SOLENOID_WAKE_DELAY_US;
    gpio_put(MOTEUR_GLOBAL_SLEEP_GPIO, true);
}

void solenoid_global_sleep(void) {
    gpio_put(MOTEUR_GLOBAL_SLEEP_GPIO, false);
}

static bool global_awake(void) {
    return gpio_get_out_level(MOTEUR_GLOBAL_SLEEP_GPIO);
}

static bool global_ready(uint32_t now_us) {
    return global_awake() && (uint32_t)(now_us - g_wake_deadline_us) < 0x80000000u;
}

// --- Canal ---
static void set_state(solenoid_driver_t *drv, solenoid_state_t state, uint32_t now_us) {
    drv->state = state;
    drv->state_start_us = now_us;
}

// Coupure immédiate : duty 0, PWM coupé. dir_channel inchangé (PWM nul, donc sûr).
static void cut_output(solenoid_driver_t *drv) {
    pwm_hal_disable(&drv->pwm_channel);
}

static void begin_rest(solenoid_driver_t *drv, uint32_t now_us, uint32_t rest_ms) {
    cut_output(drv);
    drv->rest_duration_us = rest_ms * US_PER_MS;
    set_state(drv, SOLENOID_STATE_REST, now_us);
}

static void enter_fault(solenoid_driver_t *drv, solenoid_fault_t fault, uint32_t now_us) {
    cut_output(drv);
    drv->fault = fault;
    drv->note_off_pending = false;
    set_state(drv, SOLENOID_STATE_FAULT, now_us);
}

// Étape 1 de l'inversion de polarité (ADR-0001) : duty à 0 AVANT de toucher dir_channel.
static void begin_release(solenoid_driver_t *drv, uint32_t now_us) {
    pwm_hal_set_duty_percent(&drv->pwm_channel, 0u);
    set_state(drv, SOLENOID_STATE_DIR_TO_RELEASE, now_us);
}

// Retour à la polarité de jeu (ADR-0001, ADR-0012) : duty ramené à 0, attente du
// délai de sécurité dans DIR_TO_PULL, PUIS réécriture de dir_channel. La durée de
// repos à purger ensuite est mémorisée ici et consommée par le cas DIR_TO_PULL :
// repos court du jeu normal, ou repos maintenance (2 s).
static void begin_dir_to_pull(solenoid_driver_t *drv, uint32_t now_us, uint32_t rest_ms) {
    pwm_hal_set_duty_percent(&drv->pwm_channel, 0u);
    drv->rest_duration_us = rest_ms * US_PER_MS;
    set_state(drv, SOLENOID_STATE_DIR_TO_PULL, now_us);
}

solenoid_result_t solenoid_driver_init(solenoid_driver_t *drv, unsigned int pwm_gpio,
                                       unsigned int dir_gpio, unsigned int fault_gpio) {
    if (drv == NULL) {
        return SOLENOID_ERR_PARAM;
    }
    drv->dir_gpio = dir_gpio;
    drv->fault_gpio = fault_gpio;
    drv->fault = SOLENOID_FAULT_NONE;
    drv->note_off_pending = false;
    drv->hold_timed_out = false;
    drv->rest_duration_us = SOLENOID_MIN_REST_MS * US_PER_MS;

    gpio_init(dir_gpio);
    gpio_put(dir_gpio, SOLENOID_DIR_LEVEL_PULL);
    gpio_set_dir(dir_gpio, GPIO_OUT);

    gpio_init(fault_gpio);
    gpio_set_dir(fault_gpio, GPIO_IN);
    gpio_pull_up(fault_gpio);  // NFAULT open-drain actif bas (ADR-0001 IMP-002)

    if (!pwm_hal_init(&drv->pwm_channel, pwm_gpio, SOLENOID_PWM_FREQ_HZ)) {
        drv->state = SOLENOID_STATE_FAULT;  // jamais utilisable : verrouillé
        drv->fault = SOLENOID_FAULT_NFAULT;
        return SOLENOID_ERR_PWM;
    }
    pwm_hal_disable(&drv->pwm_channel);
    drv->state = SOLENOID_STATE_IDLE;
    drv->state_start_us = 0u;
    return SOLENOID_OK;
}

solenoid_result_t solenoid_driver_note_on(solenoid_driver_t *drv, uint8_t velocity, uint32_t now_us) {
    (void)velocity;  // réservé : modulation du PULL par vélocité plus tard
    if (drv == NULL) {
        return SOLENOID_ERR_PARAM;
    }
    if (drv->state == SOLENOID_STATE_FAULT) {
        return SOLENOID_ERR_FAULT;
    }
    if (drv->state == SOLENOID_STATE_REST) {
        return SOLENOID_ERR_REST;
    }
    if (!global_ready(now_us)) {
        return SOLENOID_ERR_SLEEP;
    }
    if (drv->state != SOLENOID_STATE_IDLE) {
        return SOLENOID_ERR_BUSY;
    }
    if (!gpio_get(drv->fault_gpio)) {  // défaut déjà présent : refus et verrouillage
        enter_fault(drv, SOLENOID_FAULT_NFAULT, now_us);
        return SOLENOID_ERR_FAULT;
    }
    drv->note_off_pending = false;
    drv->hold_timed_out = false;
    // dir_channel est au niveau PULL en IDLE (garanti par DIR_TO_PULL / init).
    pwm_hal_set_duty_percent(&drv->pwm_channel, clamp_duty(SOLENOID_PULL_DUTY_PERCENT));
    pwm_hal_enable(&drv->pwm_channel);
    set_state(drv, SOLENOID_STATE_PULL, now_us);
    return SOLENOID_OK;
}

solenoid_result_t solenoid_driver_note_off(solenoid_driver_t *drv, uint32_t now_us) {
    if (drv == NULL) {
        return SOLENOID_ERR_PARAM;
    }
    if (drv->state == SOLENOID_STATE_PULL) {
        drv->note_off_pending = true;  // PULL va à son terme puis relâche
    } else if (drv->state == SOLENOID_STATE_HOLD) {
        begin_release(drv, now_us);
    } else if (drv->state == SOLENOID_STATE_MAINTENANCE) {
        // Fin de la poussée inverse (ADR-0012) : retour à la polarité de jeu AVANT
        // le repos, sinon le prochain Note On partirait en sens inverse.
        begin_dir_to_pull(drv, now_us, SOLENOID_MAINT_MIN_REST_MS);
    }
    return SOLENOID_OK;
}

solenoid_result_t solenoid_driver_probe_start(solenoid_driver_t *drv, uint32_t now_us) {
    if (drv == NULL) {
        return SOLENOID_ERR_PARAM;
    }
    if (drv->state == SOLENOID_STATE_FAULT) {
        return SOLENOID_ERR_FAULT;
    }
    if (drv->state == SOLENOID_STATE_REST) {
        return SOLENOID_ERR_REST;
    }
    if (!global_ready(now_us)) {
        return SOLENOID_ERR_SLEEP;
    }
    if (drv->state != SOLENOID_STATE_IDLE) {
        return SOLENOID_ERR_BUSY;
    }
    if (!gpio_get(drv->fault_gpio)) {
        enter_fault(drv, SOLENOID_FAULT_NFAULT, now_us);
        return SOLENOID_ERR_FAULT;
    }
    drv->note_off_pending = false;
    drv->hold_timed_out = false;
    gpio_put(drv->dir_gpio, SOLENOID_DIR_LEVEL_PULL);
    pwm_hal_set_duty_percent(&drv->pwm_channel, probe_duty_percent());
    pwm_hal_enable(&drv->pwm_channel);
    set_state(drv, SOLENOID_STATE_PROBE, now_us);
    return SOLENOID_OK;
}

solenoid_result_t solenoid_driver_push_start(solenoid_driver_t *drv, uint32_t now_us) {
    if (drv == NULL) {
        return SOLENOID_ERR_PARAM;
    }
    if (drv->state == SOLENOID_STATE_FAULT) {
        return SOLENOID_ERR_FAULT;
    }
    if (drv->state == SOLENOID_STATE_REST) {
        return SOLENOID_ERR_REST;
    }
    if (!global_ready(now_us)) {
        return SOLENOID_ERR_SLEEP;
    }
    if (drv->state != SOLENOID_STATE_IDLE) {
        return SOLENOID_ERR_BUSY;
    }
    if (!gpio_get(drv->fault_gpio)) {
        enter_fault(drv, SOLENOID_FAULT_NFAULT, now_us);
        return SOLENOID_ERR_FAULT;
    }
    drv->note_off_pending = false;
    drv->hold_timed_out = false;
    // ADR-0012 : polarité INVERSE du jeu normal (REPEL). L'écriture directe de
    // dir_channel est sûre ici : la fonction n'est admise qu'en IDLE (contrôle
    // ci-dessus), état atteint uniquement via REST, donc avec duty nul depuis au
    // moins SOLENOID_MIN_REST_MS (10 ms) >> SOLENOID_DIR_SAFETY_DELAY_US (200 us).
    // Si un chemin vers IDLE sans REST apparaît, passer par un état transitoire
    // calqué sur DIR_TO_RELEASE (duty 0, attente, puis changement de niveau).
    gpio_put(drv->dir_gpio, SOLENOID_MAINT_DIR_LEVEL);
    pwm_hal_set_duty_percent(&drv->pwm_channel, maint_duty_percent());
    pwm_hal_enable(&drv->pwm_channel);
    set_state(drv, SOLENOID_STATE_MAINTENANCE, now_us);
    return SOLENOID_OK;
}

void solenoid_driver_tick(solenoid_driver_t *drv, uint32_t now_us) {
    if (drv->state == SOLENOID_STATE_FAULT) {
        return;
    }
    // Surveillance défaut : prioritaire sur toute transition.
    const bool active = drv->state != SOLENOID_STATE_IDLE;
    // NFAULT n'est significatif qu'après le réveil complet des drivers.
    if (global_ready(now_us) && !gpio_get(drv->fault_gpio)) {
        enter_fault(drv, SOLENOID_FAULT_NFAULT, now_us);
        return;
    }
    if (active && !global_awake() && drv->state != SOLENOID_STATE_REST) {
        enter_fault(drv, SOLENOID_FAULT_SLEEP, now_us);
        return;
    }

    switch (drv->state) {
    case SOLENOID_STATE_PROBE:
        if (elapsed(now_us, drv->state_start_us, probe_duration_us())) {
            begin_rest(drv, now_us, SOLENOID_MIN_REST_MS);
        }
        break;
    case SOLENOID_STATE_MAINTENANCE:
        if (elapsed(now_us, drv->state_start_us, maint_duration_us())) {
            // Coupure locale (invariant 4, ADR-0012) : retour à la polarité de jeu
            // avant le repos, comme au relâchement.
            begin_dir_to_pull(drv, now_us, SOLENOID_MAINT_MIN_REST_MS);
        }
        break;
    case SOLENOID_STATE_PULL:
        if (elapsed(now_us, drv->state_start_us, pull_duration_us())) {
            if (drv->note_off_pending) {
                begin_release(drv, now_us);
            } else {
                pwm_hal_set_duty_percent(&drv->pwm_channel, clamp_hold_duty(SOLENOID_HOLD_DUTY_PERCENT));
                set_state(drv, SOLENOID_STATE_HOLD, now_us);
            }
        }
        break;
    case SOLENOID_STATE_HOLD:
        if (elapsed(now_us, drv->state_start_us, SOLENOID_HOLD_MAX_MS * US_PER_MS)) {
            drv->hold_timed_out = true;  // coupure de sécurité (invariant 4)
            begin_release(drv, now_us);
        }
        break;
    case SOLENOID_STATE_DIR_TO_RELEASE:
        if (elapsed(now_us, drv->state_start_us, SOLENOID_DIR_SAFETY_DELAY_US)) {
            gpio_put(drv->dir_gpio, SOLENOID_DIR_LEVEL_RELEASE);  // duty = 0 depuis begin_release
            pwm_hal_set_duty_percent(&drv->pwm_channel, clamp_duty(SOLENOID_RELEASE_DUTY_PERCENT));
            set_state(drv, SOLENOID_STATE_RELEASE, now_us);
        }
        break;
    case SOLENOID_STATE_RELEASE:
        if (elapsed(now_us, drv->state_start_us, SOLENOID_RELEASE_DURATION_MS * US_PER_MS)) {
            begin_dir_to_pull(drv, now_us, SOLENOID_MIN_REST_MS);
        }
        break;
    case SOLENOID_STATE_DIR_TO_PULL:
        if (elapsed(now_us, drv->state_start_us, SOLENOID_DIR_SAFETY_DELAY_US)) {
            gpio_put(drv->dir_gpio, SOLENOID_DIR_LEVEL_PULL);  // duty nul : sûr
            // Restitution du repos mémorisé par begin_dir_to_pull() : repos court
            // du jeu normal, ou repos maintenance (ADR-0012).
            begin_rest(drv, now_us, drv->rest_duration_us / US_PER_MS);
        }
        break;
    case SOLENOID_STATE_REST:
        if (elapsed(now_us, drv->state_start_us, drv->rest_duration_us)) {
            set_state(drv, SOLENOID_STATE_IDLE, now_us);
        }
        break;
    case SOLENOID_STATE_IDLE:
    case SOLENOID_STATE_FAULT:
    default:
        break;
    }
}

solenoid_result_t solenoid_driver_reset_fault(solenoid_driver_t *drv, uint32_t now_us) {
    if (drv == NULL) {
        return SOLENOID_ERR_PARAM;
    }
    if (drv->state != SOLENOID_STATE_FAULT) {
        return SOLENOID_OK;
    }
    if (drv->pwm_channel.wrap == 0u) {
        return SOLENOID_ERR_PWM;  // init PWM jamais réussie : pas de reprise possible
    }
    if (!global_ready(now_us)) {
        return SOLENOID_ERR_SLEEP;
    }
    if (!gpio_get(drv->fault_gpio)) {
        return SOLENOID_ERR_FAULT_PERSISTS;
    }
    drv->fault = SOLENOID_FAULT_NONE;
    drv->note_off_pending = false;
    cut_output(drv);
    // Remet dir_channel au niveau PULL (duty nul depuis cut_output) puis repos.
    begin_dir_to_pull(drv, now_us, SOLENOID_MIN_REST_MS);
    return SOLENOID_OK;
}

solenoid_state_t solenoid_driver_state(const solenoid_driver_t *drv) {
    return drv->state;
}

solenoid_fault_t solenoid_driver_fault(const solenoid_driver_t *drv) {
    return drv->fault;
}

bool solenoid_driver_hold_timed_out(const solenoid_driver_t *drv) {
    return drv->hold_timed_out;
}

const char *solenoid_state_name(solenoid_state_t state) {
    switch (state) {
    case SOLENOID_STATE_IDLE: return "IDLE";
    case SOLENOID_STATE_PULL: return "PULL";
    case SOLENOID_STATE_HOLD: return "HOLD";
    case SOLENOID_STATE_DIR_TO_RELEASE: return "DIR_TO_RELEASE";
    case SOLENOID_STATE_RELEASE: return "RELEASE";
    case SOLENOID_STATE_DIR_TO_PULL: return "DIR_TO_PULL";
    case SOLENOID_STATE_REST: return "REST";
    case SOLENOID_STATE_PROBE: return "PROBE";
    case SOLENOID_STATE_MAINTENANCE: return "MAINTENANCE";
    case SOLENOID_STATE_FAULT: return "FAULT";
    default: return "?";
    }
}

const char *solenoid_result_name(solenoid_result_t result) {
    switch (result) {
    case SOLENOID_OK: return "OK";
    case SOLENOID_ERR_BUSY: return "BUSY";
    case SOLENOID_ERR_FAULT: return "FAULT";
    case SOLENOID_ERR_SLEEP: return "SLEEP";
    case SOLENOID_ERR_REST: return "REST";
    case SOLENOID_ERR_PARAM: return "PARAM";
    case SOLENOID_ERR_PWM: return "PWM";
    case SOLENOID_ERR_FAULT_PERSISTS: return "FAULT_PERSISTS";
    default: return "?";
    }
}
