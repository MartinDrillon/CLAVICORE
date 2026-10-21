// Test hôte de la console de réglage à chaud (ADR-0015) : bringup_console.c est
// compilé tel quel, can_link et usb_log sont remplacés par des doublures qui
// enregistrent ce que la console demande d'envoyer aux cartes.
#include <stdio.h>
#include <string.h>

#include "bringup_console.h"
#include "can_link.h"
#include "solenoid_profile.h"
#include "usb_log.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("ECHEC %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                    \
    } while (0)

// --- Doublures -----------------------------------------------------------------

static const char *g_input;
static solenoid_profile_t g_compiled;
static solenoid_profile_t g_active;
static solenoid_profile_t g_last_sent;
static int g_send_count;
static int g_selftest_count;
static can_link_request_result_t g_send_result = CAN_LINK_REQUEST_SENT;

int usb_log_read_char(void) {
    if (g_input == NULL || *g_input == '\0') {
        return -1;
    }
    return (unsigned char)*g_input++;
}

can_link_request_result_t can_link_request_selftest(void) {
    ++g_selftest_count;
    return CAN_LINK_REQUEST_SENT;
}
bool can_link_calibration_enabled(void) { return false; }
void can_link_set_calibration_enabled(bool enabled) { (void)enabled; }
void can_link_dump_table(void) {}
void can_link_dump_status(void) {}
const solenoid_profile_t *can_link_profile_active(void) { return &g_active; }
const solenoid_profile_t *can_link_profile_compiled(void) { return &g_compiled; }
can_link_request_result_t can_link_profile_send(const solenoid_profile_t *profile) {
    ++g_send_count;
    g_last_sent = *profile;
    if (g_send_result == CAN_LINK_REQUEST_SENT) {
        g_active = *profile;  // la vraie implémentation promeut après acquittement
    }
    return g_send_result;
}

static solenoid_profile_t make_profile(void) {
    solenoid_profile_t p = {0};

    p.pwm_freq_hz = 20000u;
    p.pull_duration_us = 15000u;
    p.pull_duty_pct = 50u;
    p.pull_max_us = 50000u;
    p.hold_duty_pct = 25u;
    p.hold_duty_max_pct = 25u;
    p.hold_max_ms = 20000u;
    p.release_duration_us = 5000u;
    p.release_duty_pct = 50u;
    p.probe_duration_us = 5000u;
    p.probe_duty_pct = 20u;
    p.probe_max_us = 10000u;
    p.probe_duty_max_pct = 20u;
    p.maint_duty_pct = 50u;
    p.maint_duty_max_pct = 100u;
    p.maint_max_ms = 20000u;
    p.maint_min_rest_ms = 2000u;
    p.min_rest_ms = 10u;
    p.dir_safety_delay_us = 200u;
    p.wake_delay_us = 2000u;
    p.dir_level_pull = 0u;
    p.dir_level_release = 1u;
    p.dir_level_maint = 1u;
    p.duty_max_pct = 100u;
    return p;
}

static void reset(void) {
    g_compiled = make_profile();
    g_active = g_compiled;
    g_last_sent = g_compiled;
    g_send_count = 0;
    g_selftest_count = 0;
    g_send_result = CAN_LINK_REQUEST_SENT;
    bringup_console_init();
}

static void type(const char *text) {
    g_input = text;
    bringup_console_poll();
}

int main(void) {
    reset();
    puts("--- p set (forme nom valeur)");
    type("p set pull_duration_us 12000\r\n");
    CHECK(g_send_count == 1 && g_last_sent.pull_duration_us == 12000u);
    CHECK(g_active.pull_duration_us == 12000u);

    puts("--- raccourci initialiseur C");
    type(".pull_duration_us = 10000u,\n");
    CHECK(g_send_count == 2 && g_last_sent.pull_duration_us == 10000u);

    puts("--- multi-affectation atomique (hold_duty_pct exige hold_duty_max_pct)");
    type(".hold_duty_max_pct = 40u, .hold_duty_pct = 30u, .min_rest_ms = 12u,\n");
    CHECK(g_send_count == 3);
    CHECK(g_last_sent.hold_duty_pct == 30u && g_last_sent.hold_duty_max_pct == 40u &&
          g_last_sent.min_rest_ms == 12u);

    puts("--- refus : rien n'est envoye");
    type("p set hold_duty_pct 90\n");        // > plafond
    type("p set pwm_freq_hz 25000\n");       // verrouille
    type("p set champ_inconnu 1\n");
    type("p set pull_duty_pct 300\n");       // hors u8
    type("p set pull_duration_us 60000\n");  // > pull_max_us
    type("p set\n");
    CHECK(g_send_count == 3);

    puts("--- aucune valeur ne change");
    type("p set min_rest_ms 12\n");
    CHECK(g_send_count == 3);

    puts("--- show / get / diff / fields / aide");
    type("p show\np get pull_duration_us\np get .hold_duty_pct\np diff\np fields\np ?\np\n");
    CHECK(g_send_count == 3);

    puts("--- send / reset");
    type("p send\n");
    CHECK(g_send_count == 4 && g_last_sent.min_rest_ms == 12u);
    type("p reset\n");
    CHECK(g_send_count == 5 && g_last_sent.min_rest_ms == 10u && g_last_sent.pull_duration_us == 15000u);

    puts("--- refus de l'envoi (voie occupee) : le profil actif ne change pas");
    reset();
    g_send_result = CAN_LINK_REQUEST_BUSY;
    type("p set pull_duty_pct 70\n");
    CHECK(g_send_count == 1 && g_active.pull_duty_pct == 50u);

    puts("--- commandes historiques toujours servies, 'pull...' n'est pas une commande p");
    reset();
    type("t\n");
    CHECK(g_selftest_count == 1);
    type("pull_duration_us 5\n");
    CHECK(g_send_count == 0);

    puts("--- ligne trop longue ignoree en entier, la suivante est servie");
    {
        char line[400];

        memset(line, 'x', sizeof(line));
        line[sizeof(line) - 1u] = '\0';
        reset();
        type(line);
        type("\n");
        type("p set pull_duty_pct 60\n");
        CHECK(g_send_count == 1 && g_last_sent.pull_duty_pct == 60u);
    }

    puts("--- retour arriere");
    reset();
    type("p set pull_duty_pct 6x\x7f" "0\n");
    CHECK(g_send_count == 1 && g_last_sent.pull_duty_pct == 60u);

    printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
