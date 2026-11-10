#include "bringup_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_link.h"
#include "diag_out.h"
#include "listen_params.h"
#include "profile_tuning.h"
#include "rec_console.h"
#include "rec_mode.h"
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
    diag_out_printf("[CONSOLE] e=mode REC / ecoute IPROPI (e ?)\n");
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

// ---------------------------------------------------------------------------
//  Console `e` : mode REC / écoute IPROPI (ADR-0032)
// ---------------------------------------------------------------------------

#define LISTEN_TOKEN_MAX 32u
#define LISTEN_DIFF_MAX 1024u

static void print_listen_help(void) {
    diag_out_printf_class(
        DIAG_CLASS_CONSOLE,
        "[LISTEN] e show | e get <champ> | e set <champ> <valeur> [<champ> <valeur> ...]\n");
    diag_out_printf_class(DIAG_CLASS_CONSOLE,
                          "[LISTEN] e diff | e reset | e rec | e bruit | e raz\n");
    diag_out_printf_class(
        DIAG_CLASS_CONSOLE,
        "[LISTEN] e trace <touche> | e stream <touche>|off   (touche = MI3, RE#3 ou 36..127)\n");
    diag_out_printf_class(
        DIAG_CLASS_CONSOLE,
        "[LISTEN] reglage en RAM Clavier (perdu au reboot), aucune valeur n'est une limite\n");
}

static const char *next_token(const char *text, char *out, size_t capacity) {
    size_t used = 0u;

    text = skip_ws(text);
    while (*text != '\0' && *text != ' ' && *text != '\t') {
        if (used + 1u < capacity) {
            out[used] = *text;
        }
        ++used;
        ++text;
    }
    if (capacity > 0u) {
        out[(used < capacity) ? used : (capacity - 1u)] = '\0';
    }
    return skip_ws(text);
}

static bool parse_u32(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long value;

    if (text == NULL || *text == '\0' || out == NULL) {
        return false;
    }
    value = strtoul(text, &end, 0);
    if (end == text || *end != '\0') {
        return false;
    }
    if (value > 0xFFFFFFFFul) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool token_is(const char *text, const char *word) {
    while (*text != '\0' && *word != '\0') {
        char a = *text++;
        char b = *word++;

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return *text == '\0' && *word == '\0';
}

static void listen_show(void) {
    const listen_params_t *params = rec_console_params();
    const rec_state_t *rec = rec_console_state();

    if (params == NULL) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] parametres indisponibles\n");
        return;
    }
    for (uint8_t id = 1u; id <= LISTEN_PARAMS_COUNT; ++id) {
        const char *name = listen_params_field_name(id);
        uint32_t value = 0u;

        if (name != NULL && listen_params_get(params, id, &value)) {
            diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN]   %-20s = %lu\n", name,
                                  (unsigned long)value);
        }
    }
    diag_out_printf_class(
        DIAG_CLASS_CONSOLE, "[LISTEN] actif=%s crc=0x%04X detail=%lu bruit=%s%s\n",
        rec_mode_name(rec_console_mode()), (unsigned)listen_params_crc16(params),
        (unsigned long)params->detail,
        (rec != NULL && rec->noise_enabled) ? "on" : "off",
        rec_console_params_valid() ? "" : " (config invalide)");
}

static void listen_get(const char *text) {
    char name[LISTEN_TOKEN_MAX];
    const listen_params_t *params = rec_console_params();
    uint32_t value = 0u;

    (void)next_token(text, name, sizeof(name));
    if (name[0] == '.') {
        memmove(name, name + 1, strlen(name));
    }
    if (params == NULL || !rec_mode_param_get(params, name, &value)) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] champ inconnu '%s'\n", name);
        return;
    }
    diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] %s = %lu\n", name,
                          (unsigned long)value);
}

// Imprime les champs qui diffèrent entre `before` et `after`. Retourne le
// nombre de changements affichés.
static size_t listen_print_changes(const listen_params_t *before, const listen_params_t *after) {
    size_t changed = 0u;

    for (uint8_t id = 1u; id <= LISTEN_PARAMS_COUNT; ++id) {
        uint32_t old_value = 0u;
        uint32_t new_value = 0u;

        if (!listen_params_get(before, id, &old_value) ||
            !listen_params_get(after, id, &new_value) || old_value == new_value) {
            continue;
        }
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN]   %s %lu -> %lu\n",
                              listen_params_field_name(id), (unsigned long)old_value,
                              (unsigned long)new_value);
        ++changed;
    }
    return changed;
}

// Applique un jeu déjà validé : affiche les différences, coupe `bruit` si
// detail passe à 0, puis diffuse (arrêt/redémarrage si REC est actif).
static void listen_commit(const listen_params_t *candidate) {
    listen_params_t *current = rec_console_params();
    rec_state_t *rec = rec_console_state();
    listen_params_t before;

    if (current == NULL) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] parametres indisponibles\n");
        return;
    }
    before = *current;
    if (listen_print_changes(&before, candidate) == 0u) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] reglage: aucune valeur ne change\n");
    }
    if (candidate->detail == 0u && rec != NULL && rec->noise_enabled) {
        rec->noise_enabled = false;
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] bruit=off (detail=0, aucune mesure de bruit)\n");
    }
    *current = *candidate;
    if (!rec_console_send_params()) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] diffusion impossible (reglage garde en RAM)\n");
    }
}

static void listen_set(const char *text) {
    const listen_params_t *current = rec_console_params();
    listen_params_t candidate;
    const char *cursor = skip_ws(text);

    if (current == NULL) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] parametres indisponibles\n");
        return;
    }
    if (*cursor == '\0') {
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] usage: e set <champ> <valeur> [...]\n");
        return;
    }
    candidate = *current;
    while (*cursor != '\0') {
        char name[LISTEN_TOKEN_MAX];
        char value_text[LISTEN_TOKEN_MAX];
        uint32_t value = 0u;

        cursor = next_token(cursor, name, sizeof(name));
        if (*cursor == '\0') {
            diag_out_printf_class(DIAG_CLASS_CONSOLE,
                                  "[LISTEN] valeur manquante pour '%s'\n", name);
            return;
        }
        cursor = next_token(cursor, value_text, sizeof(value_text));
        if (!parse_u32(value_text, &value)) {
            diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] valeur invalide '%s'\n",
                                  value_text);
            return;
        }
        {
            const char *field_name = name;

            if (field_name[0] == '.') {
                ++field_name;
            }
            if (!rec_mode_param_set(&candidate, field_name, value)) {
                diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] champ inconnu '%s'\n",
                                      name);
                return;
            }
        }
    }
    {
        const char *bad_name = NULL;

        if (rec_mode_params_validate(&candidate, &bad_name) != LISTEN_PARAMS_OK) {
            // Rejet en bloc : aucune affectation n'est publiée (atomique).
            diag_out_printf_class(DIAG_CLASS_CONSOLE,
                                  "[LISTEN] reglage refuse: %s (validation)\n",
                                  (bad_name != NULL) ? bad_name : "?");
            return;
        }
    }
    listen_commit(&candidate);
}

static void listen_diff(void) {
    const listen_params_t *params = rec_console_params();
    char lines[LISTEN_DIFF_MAX];
    size_t count;

    if (params == NULL) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] parametres indisponibles\n");
        return;
    }
    count = rec_mode_params_diff(params, lines, sizeof(lines));
    if (count == 0u) {
        diag_out_printf_class(
            DIAG_CLASS_CONSOLE,
            "[LISTEN] aucune difference avec le fichier listen_config.h\n");
        return;
    }
    diag_out_printf_class(DIAG_CLASS_CONSOLE,
                          "[LISTEN] a recopier dans listen_config.h :\n");
    {
        const char *line = lines;

        while (*line != '\0') {
            const char *end = strchr(line, '\n');
            size_t length = (end != NULL) ? (size_t)(end - line) : strlen(line);

            if (length > 0u) {
                char copy[96];

                if (length >= sizeof(copy)) {
                    length = sizeof(copy) - 1u;
                }
                memcpy(copy, line, length);
                copy[length] = '\0';
                diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN]   %s\n", copy);
            }
            if (end == NULL) {
                break;
            }
            line = end + 1;
        }
    }
}

static void listen_reset(void) {
    const listen_params_t *compiled = rec_console_compiled_params();

    if (compiled == NULL) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] config compilee indisponible\n");
        return;
    }
    listen_commit(compiled);
}

static void listen_rec(void) {
    const rec_mode_t mode = rec_console_mode();

    if (mode != REC_MODE_OFF) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] rec deja actif (%s)\n",
                              rec_mode_name(mode));
        return;
    }
    if (!rec_console_params_valid()) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] config invalide, rec refuse (voir e show)\n");
        return;
    }
    // main.c journalise `[REC] ON SOLE` ou `[REC] refus <raison>`.
    (void)rec_console_enter_sole();
}

static void listen_bruit(void) {
    const listen_params_t *params = rec_console_params();
    rec_state_t *rec = rec_console_state();
    bool enabled = false;

    if (params == NULL || rec == NULL) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] etat indisponible\n");
        return;
    }
    if (!rec_mode_noise_toggle(rec, params->detail != 0u, &enabled)) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] bruit: necessite detail=1\n");
        return;
    }
    diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] bruit=%s\n",
                          enabled ? "on" : "off");
}

static bool listen_resolve(const char *text, char *token, size_t capacity, uint8_t *midi_note,
                           commissioning_note_target_t *target) {
    const commissioning_runtime_t *runtime;

    (void)next_token(text, token, capacity);
    if (token[0] == '\0') {
        return false;
    }
    if (!rec_console_table_valid()) {
        return false;
    }
    runtime = rec_console_runtime();
    if (runtime == NULL) {
        return false;
    }
    return rec_mode_resolve_touch(runtime, token, midi_note, target);
}

static void listen_trace(const char *text) {
    char token[LISTEN_TOKEN_MAX];
    char label[16] = "?";
    uint8_t midi_note = 0u;
    commissioning_note_target_t target;

    if (!rec_console_table_valid()) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[REC] refus REC_NO_TABLE\n");
        return;
    }
    if (!listen_resolve(text, token, sizeof(token), &midi_note, &target)) {
        (void)next_token(text, token, sizeof(token));
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] touche inconnue '%s'\n", token);
        return;
    }
    (void)rec_mode_note_name(midi_note, label, sizeof(label));
    if (!rec_console_trace(midi_note, &target)) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] trace impossible\n");
        return;
    }
    diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] trace %s (uid=0x%04X ch=%u)\n", label,
                          (unsigned)target.uid_tag, (unsigned)target.channel);
}

static void listen_stream(const char *text) {
    char token[LISTEN_TOKEN_MAX];
    uint8_t midi_note = 0u;
    commissioning_note_target_t target;

    if (!CLAVIER_SERIAL_STUDIO) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] stream indisponible (CLAVIER_SERIAL_STUDIO=OFF)\n");
        return;
    }
    (void)next_token(text, token, sizeof(token));
    if (token[0] == '\0') {
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] usage: e stream <touche>|off\n");
        return;
    }
    if (token_is(token, "off")) {
        if (!rec_console_stream_off()) {
            diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] stream off impossible\n");
            return;
        }
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] stream=off\n");
        return;
    }
    if (!rec_console_table_valid()) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[REC] refus REC_NO_TABLE\n");
        return;
    }
    if (!listen_resolve(text, token, sizeof(token), &midi_note, &target)) {
        (void)next_token(text, token, sizeof(token));
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] touche inconnue '%s'\n", token);
        return;
    }
    if (!rec_console_stream(&target)) {
        diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] stream impossible\n");
        return;
    }
    diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] stream uid=0x%04X ch=%u\n",
                          (unsigned)target.uid_tag, (unsigned)target.channel);
}

static void listen_command(const char *args) {
    const char *rest;

    args = skip_ws(args);
    if (*args == '\0' || *args == '?' || take_word(args, "help", &rest)) {
        print_listen_help();
    } else if (take_word(args, "show", &rest)) {
        listen_show();
    } else if (take_word(args, "set", &rest)) {
        listen_set(rest);
    } else if (take_word(args, "get", &rest)) {
        listen_get(rest);
    } else if (take_word(args, "diff", &rest)) {
        listen_diff();
    } else if (take_word(args, "reset", &rest)) {
        listen_reset();
    } else if (take_word(args, "rec", &rest)) {
        listen_rec();
    } else if (take_word(args, "bruit", &rest)) {
        listen_bruit();
    } else if (take_word(args, "trace", &rest)) {
        listen_trace(rest);
    } else if (take_word(args, "stream", &rest)) {
        listen_stream(rest);
    } else if (take_word(args, "raz", &rest)) {
        if (rec_console_raz()) {
            diag_out_printf_class(DIAG_CLASS_CONSOLE,
                                  "[LISTEN] raz (touches relevees)\n");
        } else {
            diag_out_printf_class(DIAG_CLASS_CONSOLE, "[LISTEN] raz impossible\n");
        }
    } else {
        diag_out_printf_class(DIAG_CLASS_CONSOLE,
                              "[LISTEN] sous-commande inconnue (e ? pour l'aide)\n");
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
    if ((*text == 'e' || *text == 'E') && (text[1] == '\0' || text[1] == ' ' || text[1] == '\t')) {
        listen_command(text + 1);
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
