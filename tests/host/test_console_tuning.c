// Test hôte de la console de réglage à chaud (ADR-0015) : bringup_console.c est
// compilé tel quel, can_link et usb_log sont remplacés par des doublures qui
// enregistrent ce que la console demande d'envoyer aux cartes.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bringup_console.h"
#include "can_link.h"
#include "commissioning_table.h"
#include "diag_out.h"
#include "listen_config.h"
#include "listen_params.h"
#include "rec_console.h"
#include "rec_mode.h"
#include "solenoid_profile.h"
#include "usb_log.h"

static int g_checks = 0;
static int g_failures = 0;

// Sortie de la console capturée : les réponses `e` sont au niveau 0 et le test
// vérifie le parsing (`e set`) et les lignes de `e diff`.
static char g_output[8192];
static size_t g_output_len;

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

// --- Pont console `e` (rec_console.h) ------------------------------------------
// L'état vit normalement dans main.c ; ici la console est testée avec un état
// local et les compteurs de diffusion.
static listen_params_t g_listen_params;
static rec_state_t g_rec;
static rec_mode_t g_rec_mode;
static commissioning_runtime_t g_runtime;
static int g_listen_send_count;
static int g_listen_enter_count;
static int g_listen_raz_count;
static int g_listen_trace_count;
static int g_listen_stream_count;
static int g_listen_stream_off_count;

listen_params_t *rec_console_params(void) { return &g_listen_params; }
const listen_params_t *rec_console_compiled_params(void) { return &k_listen_config; }
rec_state_t *rec_console_state(void) { return &g_rec; }
const commissioning_runtime_t *rec_console_runtime(void) { return &g_runtime; }
bool rec_console_table_valid(void) { return true; }
bool rec_console_params_valid(void) { return true; }
rec_mode_t rec_console_mode(void) { return g_rec_mode; }
bool rec_console_enter_sole(void) {
    ++g_listen_enter_count;
    g_rec_mode = REC_MODE_SOLE;
    return true;
}
bool rec_console_raz(void) {
    ++g_listen_raz_count;
    rec_mode_raz(&g_rec);
    return true;
}
bool rec_console_send_params(void) {
    ++g_listen_send_count;
    return true;
}
bool rec_console_trace(uint8_t midi_note, const commissioning_note_target_t *target) {
    (void)midi_note;
    (void)target;
    ++g_listen_trace_count;
    return true;
}
bool rec_console_stream(const commissioning_note_target_t *target) {
    (void)target;
    ++g_listen_stream_count;
    return true;
}
bool rec_console_stream_off(void) {
    ++g_listen_stream_off_count;
    return true;
}

// Doublure du journal du Clavier (ADR-0017) : diag_out.c dépend du SDK Pico, il
// n'est pas compilé ici. La sortie est capturée pour vérifier les réponses de
// la console, pas l'horodatage de ses lignes.
static void append_output(const char *format, va_list args) {
    int written;

    if (g_output_len >= sizeof(g_output) - 1u) {
        return;
    }
    written = vsnprintf(g_output + g_output_len, sizeof(g_output) - g_output_len, format,
                        args);
    if (written > 0) {
        size_t added = (size_t)written;

        if (added > sizeof(g_output) - 1u - g_output_len) {
            added = sizeof(g_output) - 1u - g_output_len;
        }
        g_output_len += added;
    }
}
void diag_out_printf(const char *format, ...) {
    va_list args;

    va_start(args, format);
    append_output(format, args);
    va_end(args);
}
void diag_out_line(const char *line) {
    if (line != NULL) {
        diag_out_printf("%s\n", line);
    }
}
void diag_out_printf_class(diag_class_t category, const char *format, ...) {
    va_list args;

    (void)category;
    va_start(args, format);
    append_output(format, args);
    va_end(args);
}
void diag_out_line_class(diag_class_t category, const char *line) {
    (void)category;
    if (line != NULL) {
        diag_out_printf("%s\n", line);
    }
}
static uint8_t g_diag_level = 1u;
static bool g_diag_timestamp = true;
bool diag_out_set_level(uint8_t level) {
    if (level > 2u) {
        return false;
    }
    g_diag_level = level;
    return true;
}
uint8_t diag_out_level(void) {
    return g_diag_level;
}
bool diag_out_trace_available(void) {
    return true;
}
bool diag_out_is_enabled(diag_class_t category) {
    (void)category;
    return true;
}
void diag_out_toggle_timestamp(void) {
    g_diag_timestamp = !g_diag_timestamp;
}
bool diag_out_timestamp_enabled(void) {
    return g_diag_timestamp;
}
uint32_t diag_out_now_ms(void) {
    return 0u;
}
const char *diag_out_build_id(void) {
    return "test";
}

// Doublures des commandes de diagnostic (ADR-0020) : la console est testée sur
// ce qu'elle demande, pas sur l'effet côté CAN.
static int g_stats_request_count;

can_link_request_result_t can_link_stats_request_start(void) {
    ++g_stats_request_count;
    return CAN_LINK_REQUEST_SENT;
}

static solenoid_profile_t make_profile(void) {
    solenoid_profile_t p = {0};

    p.pwm_freq_hz = 20000u;
    p.impact_duration_us = 800u;
    p.impact_duty_pct = 100u;
    p.pull_duration_us = 15000u;
    p.pull_duty_pct = 50u;
    p.hold_duty_pct = 25u;
    p.hold_duty_max_pct = 25u;
    p.hold_max_ms = 20000u;
    p.release_duration_us = 5000u;
    p.release_duty_pct = 50u;
    p.probe_duration_us = 5000u;
    p.probe_duty_pct = 20u;
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
    g_diag_level = 1u;
    g_diag_timestamp = true;
    memset(&g_listen_params, 0, sizeof(g_listen_params));
    g_listen_params = k_listen_config;
    memset(&g_rec, 0, sizeof(g_rec));
    rec_mode_set_params(&g_rec, &g_listen_params);
    g_rec_mode = REC_MODE_OFF;
    memset(&g_runtime, 0, sizeof(g_runtime));
    g_listen_send_count = 0;
    g_listen_enter_count = 0;
    g_listen_raz_count = 0;
    g_listen_trace_count = 0;
    g_listen_stream_count = 0;
    g_listen_stream_off_count = 0;
    g_output[0] = '\0';
    g_output_len = 0u;
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

    puts("--- IMPACT (ADR-0019) : reglable, la duree est seule autorite (ADR-0014)");
    reset();
    type("p set impact_duration_us 1200\n");
    CHECK(g_send_count == 1 && g_last_sent.impact_duration_us == 1200u);
    reset();
    type("p set impact_duration_us 2500\n");  // accepte : plus de plafond de duree
    CHECK(g_send_count == 1 && g_last_sent.impact_duration_us == 2500u);
    reset();
    type("p set impact_duration_us 0\n");  // desactive la phase
    CHECK(g_send_count == 1 && g_last_sent.impact_duration_us == 0u);

    puts("--- retour arriere");
    reset();
    type("p set pull_duty_pct 6x\x7f" "0\n");
    CHECK(g_send_count == 1 && g_last_sent.pull_duty_pct == 60u);

    puts("--- m stats et niveaux de journal (ADR-0025)");
    reset();
    type("m\n");
    CHECK(g_stats_request_count == 0);
    type("m stats\n");
    CHECK(g_stats_request_count == 1);
    type("l\n");
    CHECK(g_diag_level == 1u);
    type("l 2\n");
    CHECK(g_diag_level == 2u);
    type("l t\n");
    CHECK(!g_diag_timestamp);
    type("l 0\n");
    CHECK(g_diag_level == 0u);
    type("n\n");
    CHECK(g_diag_level == 0u);
    type("l 3\n");
    CHECK(g_diag_level == 0u);

    puts("--- e show / get (ADR-0032)");
    reset();
    type("e show\n");
    CHECK(strstr(g_output, "settle_us") != NULL);
    CHECK(strstr(g_output, "actif=OFF") != NULL);
    type("e get rise_thr\n");
    CHECK(strstr(g_output, "rise_thr = 5") != NULL);
    type("e get champ_inconnu\n");
    CHECK(strstr(g_output, "champ inconnu") != NULL);

    puts("--- e set multi-affectation puis e diff");
    reset();
    type("e set rise_thr 9 return_thr 4\n");
    CHECK(g_listen_send_count == 1);
    CHECK(g_listen_params.rise_thr == 9u && g_listen_params.return_thr == 4u);
    CHECK(strstr(g_output, "rise_thr 5 -> 9") != NULL);
    g_output[0] = '\0';
    g_output_len = 0u;
    type("e diff\n");
    CHECK(strstr(g_output, ".rise_thr = 9,") != NULL);
    CHECK(strstr(g_output, ".return_thr = 4,") != NULL);

    puts("--- e set refuse en bloc : aucune valeur ne change, rien n'est diffuse");
    reset();
    type("e set rise_thr 2\n");  // return_thr (3) doit rester < rise_thr
    CHECK(g_listen_send_count == 0);
    CHECK(g_listen_params.rise_thr == 5u);
    CHECK(strstr(g_output, "reglage refuse") != NULL);
    reset();
    type("e set rise_thr 9 return_thr 40\n");
    CHECK(g_listen_send_count == 0);
    CHECK(g_listen_params.rise_thr == 5u && g_listen_params.return_thr == 3u);
    reset();
    type("e set champ_inconnu 1\n");
    CHECK(g_listen_send_count == 0);
    CHECK(strstr(g_output, "champ inconnu") != NULL);

    puts("--- e reset revient au fichier compile et diffuse");
    reset();
    type("e set rise_thr 9\n");
    CHECK(g_listen_send_count == 1);
    g_output[0] = '\0';
    g_output_len = 0u;
    type("e reset\n");
    CHECK(g_listen_send_count == 2);
    CHECK(g_listen_params.rise_thr == 5u);
    CHECK(strstr(g_output, "rise_thr 9 -> 5") != NULL);

    puts("--- e bruit exige detail=1, se coupe avec detail=0");
    reset();
    g_listen_params.detail = 0u;
    type("e bruit\n");
    CHECK(!g_rec.noise_enabled);
    CHECK(strstr(g_output, "necessite detail=1") != NULL);
    g_listen_params.detail = 1u;
    type("e bruit\n");
    CHECK(g_rec.noise_enabled);
    CHECK(strstr(g_output, "bruit=on") != NULL);
    type("e bruit\n");
    CHECK(!g_rec.noise_enabled);
    reset();
    g_listen_params.detail = 1u;
    g_rec.noise_enabled = true;
    type("e set detail 0\n");
    CHECK(!g_rec.noise_enabled);
    CHECK(g_listen_send_count == 1);
    CHECK(strstr(g_output, "bruit=off") != NULL);

    puts("--- e stream refuse quand CLAVIER_SERIAL_STUDIO=OFF");
    reset();
    type("e stream MI3\n");
    CHECK(g_listen_stream_count == 0);
    CHECK(strstr(g_output, "indisponible (CLAVIER_SERIAL_STUDIO=OFF)") != NULL);

    puts("--- e rec / e raz");
    reset();
    type("e rec\n");
    CHECK(g_listen_enter_count == 1 && g_rec_mode == REC_MODE_SOLE);
    type("e raz\n");
    CHECK(g_listen_raz_count == 1);

    printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
