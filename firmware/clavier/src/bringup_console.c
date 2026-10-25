#include "bringup_console.h"

#include <stdio.h>
#include <string.h>

#include "can_link.h"
#include "diag_out.h"
#include "profile_tuning.h"
#include "usb_log.h"

#ifndef CLAVIER_CDC_CONSOLE
#define CLAVIER_CDC_CONSOLE 1
#endif

#if CLAVIER_CDC_CONSOLE

// Une ligne tient une affectation multiple type `.a = 1u, .b = 2u,` (ADR-0015).
#define CONSOLE_LINE_MAX 128u

static char g_line[CONSOLE_LINE_MAX];
static uint8_t g_length;
static bool g_overflow;
static const char *skip_ws(const char *s);  // ligne trop longue : tout ignorer jusqu'à la fin de ligne

static void print_help(void) {
    diag_out_printf("[CONSOLE] t=auto-test c=mode_diagnostic d=table m=etat m stats=compteurs cartes l=niveau_journal ?=aide p=profil (p ?)\n");
    diag_out_printf("[CONSOLE] l 0=verdicts l 1=bring-up l 2=trace l t=horodatage\n");
    diag_out_printf("[CONSOLE] aucune commande n'ecrit ni n'efface la table\n");
}

static void print_log_state(void) {
    diag_out_printf("[CONSOLE] log niveau=%u timestamp=%s trace=%s\n",
                    (unsigned)diag_out_level(),
                    diag_out_timestamp_enabled() ? "on" : "off",
                    diag_out_trace_available() ? "on" : "off");
}

static void log_command(const char *args) {
    const char *token = skip_ws(args);
    const char *rest;

    if (*token == '\0') {
        print_log_state();
        return;
    }
    if (token[0] == 't' || token[0] == 'T') {
        rest = skip_ws(token + 1);
        if (*rest == '\0') {
            diag_out_toggle_timestamp();
            print_log_state();
            return;
        }
    } else if (token[0] >= '0' && token[0] <= '2') {
        rest = skip_ws(token + 1);
        if (*rest == '\0') {
            const uint8_t level = (uint8_t)(token[0] - '0');

            if (!diag_out_set_level(level)) {
                diag_out_printf("[CONSOLE] l -> niveau 2 indisponible (CLAVIER_DIAG_TRACE=OFF)\n");
                return;
            }
            print_log_state();
            return;
        }
    }
    diag_out_printf("[CONSOLE] l -> usage: l [0|1|2|t]\n");
}

static void print_profile_help(void) {
    diag_out_printf("[PROFILE] p set <champ> <valeur> [<champ> <valeur> ...]  regle et diffuse aux cartes\n");
    diag_out_printf("[PROFILE] .<champ> = <valeur>u, ...                       idem (copier-coller du .h)\n");
    diag_out_printf("[PROFILE] p get <champ> | p show | p diff | p fields\n");
    diag_out_printf("[PROFILE] p send   re-diffuse le profil actif    p reset   diffuse le profil du fichier\n");
    diag_out_printf("[PROFILE] reglage = RAM Clavier (perdu au reboot Clavier), cartes: voir ADR-0015\n");
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    return s;
}

// Vrai si `s` commence par le mot `word` suivi d'un blanc ou de la fin ; `*rest`
// pointe alors après le mot (blancs sautés).
static bool take_word(const char *s, const char *word, const char **rest) {
    const size_t length = strlen(word);

    if (strncmp(s, word, length) != 0 || (s[length] != '\0' && s[length] != ' ' && s[length] != '\t')) {
        return false;
    }
    *rest = skip_ws(s + length);
    return true;
}

static void print_field_line(const solenoid_profile_t *profile, size_t index) {
    char text[64];

    if (profile_tuning_format_field(profile, index, text, sizeof(text)) > 0u) {
        diag_out_printf("[PROFILE]   %s\n", text);
    }
}

static void profile_show(void) {
    const solenoid_profile_t *active = can_link_profile_active();
    const size_t changed = profile_tuning_diff_count(active, can_link_profile_compiled());

    diag_out_printf("[PROFILE] actif fmt=%u tag=0x%08lX (%u champ(s) differe(nt) du fichier compile)\n",
           (unsigned)SOLENOID_PROFILE_FORMAT_VERSION, (unsigned long)solenoid_profile_tag(active),
           (unsigned)changed);
    for (size_t i = 0u; i < profile_tuning_field_count(); ++i) {
        print_field_line(active, i);
    }
}

static void profile_diff(void) {
    const solenoid_profile_t *active = can_link_profile_active();
    const solenoid_profile_t *compiled = can_link_profile_compiled();
    size_t shown = 0u;

    for (size_t i = 0u; i < profile_tuning_field_count(); ++i) {
        if (profile_tuning_field_value(active, i) != profile_tuning_field_value(compiled, i)) {
            if (shown == 0u) {
                diag_out_printf("[PROFILE] a recopier dans solenoid_profile_config.h :\n");
            }
            print_field_line(active, i);
            ++shown;
        }
    }
    if (shown == 0u) {
        diag_out_printf("[PROFILE] aucune difference avec le fichier compile\n");
    }
}

static void profile_fields(void) {
    for (size_t i = 0u; i < profile_tuning_field_count(); ++i) {
        const char *reason = profile_tuning_field_lock_reason(i);

        if (reason == NULL) {
            diag_out_printf("[PROFILE]   %-22s reglable\n", profile_tuning_field_name(i));
        } else {
            diag_out_printf("[PROFILE]   %-22s verrouille: %s\n", profile_tuning_field_name(i), reason);
        }
    }
}

static void profile_get(const char *name) {
    size_t index;
    char token[PROFILE_TUNING_NAME_MAX];
    size_t n = 0u;

    if (*name == '.') {
        ++name;
    }
    while (*name != '\0' && *name != ' ' && *name != '\t' && n + 1u < sizeof(token)) {
        token[n++] = *name++;
    }
    token[n] = '\0';
    if (!profile_tuning_find(token, &index)) {
        diag_out_printf("[PROFILE] champ inconnu '%s' (p fields pour la liste)\n", token);
        return;
    }
    print_field_line(can_link_profile_active(), index);
}

static void profile_set(const char *text) {
    solenoid_profile_t candidate = *can_link_profile_active();
    profile_tuning_report_t report;
    char message[160];
    const profile_tuning_result_t result = profile_tuning_apply(text, &candidate, &report);

    if (result != PROFILE_TUNING_OK) {
        diag_out_printf("[PROFILE] reglage refuse (rien n'est envoye): %s\n",
               profile_tuning_describe(result, &report, message, sizeof(message)));
        return;
    }
    if (profile_tuning_diff_count(can_link_profile_active(), &candidate) == 0u) {
        diag_out_printf("[PROFILE] reglage: aucune valeur ne change (p send pour re-diffuser)\n");
        return;
    }
    for (size_t i = 0u; i < profile_tuning_field_count(); ++i) {
        const uint32_t before = profile_tuning_field_value(can_link_profile_active(), i);
        const uint32_t after = profile_tuning_field_value(&candidate, i);

        if (before != after) {
            diag_out_printf("[PROFILE]   %s %lu -> %lu\n", profile_tuning_field_name(i),
                   (unsigned long)before, (unsigned long)after);
        }
    }
    (void)can_link_profile_send(&candidate);
}

static void profile_command(const char *args) {
    const char *rest;

    args = skip_ws(args);
    if (*args == '\0' || *args == '?' || take_word(args, "help", &rest)) {
        print_profile_help();
    } else if (take_word(args, "set", &rest)) {
        profile_set(rest);
    } else if (take_word(args, "get", &rest)) {
        profile_get(rest);
    } else if (take_word(args, "show", &rest)) {
        profile_show();
    } else if (take_word(args, "diff", &rest)) {
        profile_diff();
    } else if (take_word(args, "fields", &rest)) {
        profile_fields();
    } else if (take_word(args, "send", &rest)) {
        (void)can_link_profile_send(can_link_profile_active());
    } else if (take_word(args, "reset", &rest)) {
        (void)can_link_profile_send(can_link_profile_compiled());
    } else {
        diag_out_printf("[PROFILE] sous-commande inconnue (p ? pour l'aide)\n");
    }
}

static void execute_line(const char *line) {
    const char *text = skip_ws(line);

    if (*text == '\0') {
        return;
    }
    if (*text == '.') {
        profile_set(text);  // raccourci : `.champ = valeur,`
        return;
    }
    if ((*text == 'p' || *text == 'P') && (text[1] == '\0' || text[1] == ' ' || text[1] == '\t')) {
        profile_command(text + 1);
        return;
    }
    switch (text[0]) {
    case 't':
    case 'T':
        switch (can_link_request_selftest()) {
        case CAN_LINK_REQUEST_SENT:
            diag_out_printf("[CONSOLE] t -> auto-test demande (voir [TRIGGER])\n");
            break;
        case CAN_LINK_REQUEST_BUSY:
            diag_out_printf("[CONSOLE] t -> refuse (occupé, voir [STATUS])\n");
            break;
        default:
            diag_out_printf("[CONSOLE] t -> echec d'emission CAN\n");
            break;
        }
        break;
    case 'c':
    case 'C': {
        const bool enabled = !can_link_calibration_enabled();

        can_link_set_calibration_enabled(enabled);
        diag_out_printf("[CONSOLE] c -> mode diagnostic %s\n", enabled ? "ON" : "OFF");
        break;
    }
    case 'd':
    case 'D':
        diag_out_printf("[CONSOLE] d -> table persistée et correspondance\n");
        can_link_dump_table();
        break;
    case 'm':
    case 'M': {
        const char *rest = skip_ws(text + 1);

        if (take_word(rest, "stats", &rest)) {
            switch (can_link_stats_request_start()) {
            case CAN_LINK_REQUEST_SENT:
                diag_out_printf("[CONSOLE] m stats -> lecture des compteurs des cartes\n");
                break;
            case CAN_LINK_REQUEST_BUSY:
                diag_out_printf("[CONSOLE] m stats -> refuse (occupe, voir [STATUS])\n");
                break;
            default:
                diag_out_printf("[CONSOLE] m stats -> echec d'emission CAN\n");
                break;
            }
        } else {
            diag_out_printf("[CONSOLE] m -> etat\n");
            can_link_dump_status();
        }
        break;
    }
    case 'l':
    case 'L':
        log_command(text + 1);
        break;
    case '?':
        print_help();
        break;
    default:
        diag_out_printf("[CONSOLE] commande inconnue (?=aide)\n");
        break;
    }
}

void bringup_console_init(void) {
    g_length = 0u;
    g_overflow = false;
    for (uint8_t i = 0u; i < CONSOLE_LINE_MAX; ++i) {
        g_line[i] = '\0';
    }
}

void bringup_console_poll(void) {
    int c;

    // Lecture directe du FIFO CDC : getchar_timeout_us() ne recevrait rien (le
    // pilote stdio du journal n'a pas d'entrée, voir usb_log_read_char()).
    while ((c = usb_log_read_char()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (g_overflow) {
                g_overflow = false;
            } else if (g_length > 0u) {
                execute_line(g_line);
                g_length = 0u;
                g_line[0] = '\0';
            }
            continue;
        }
        if (c == 0x08 || c == 0x7F) {  // retour arrière
            if (g_length > 0u) {
                --g_length;
                g_line[g_length] = '\0';
            }
            continue;
        }
        if (g_overflow) {
            continue;
        }
        if (g_length >= (CONSOLE_LINE_MAX - 1u)) {
            g_overflow = true;  // ligne trop longue : ignorée en entier
            g_length = 0u;
            g_line[0] = '\0';
            diag_out_printf("[CONSOLE] ligne trop longue (>%u), ignoree\n", (unsigned)(CONSOLE_LINE_MAX - 1u));
            continue;
        }
        g_line[g_length] = (char)c;
        ++g_length;
        g_line[g_length] = '\0';
    }
}

#else

void bringup_console_init(void) {
}

void bringup_console_poll(void) {
}

#endif  // CLAVIER_CDC_CONSOLE
