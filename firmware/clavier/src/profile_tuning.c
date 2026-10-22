// Voir profile_tuning.h : logique pure, aucune dépendance au SDK.
#include "profile_tuning.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    uint16_t offset;
    uint8_t size;
    const char *lock_reason;  // NULL = réglable à chaud
} field_t;

#define FIELD(member, reason)                                      \
    {                                                              \
        #member, (uint16_t)offsetof(solenoid_profile_t, member),   \
            (uint8_t)sizeof(((solenoid_profile_t *)0)->member), reason \
    }

#define LOCK_PWM "appliquee a l'init du Moteur seulement (fichier + reboot)"
#define LOCK_PROBE "reserve a l'auto-test IPROPI (fichier + auto-test)"
#define LOCK_DIR "polarite : le niveau n'est pas reecrit a l'arret (fichier)"
#define LOCK_WAKE "appliquee au reveil des drivers seulement (fichier)"
#define LOCK_DEADTIME "delai de securite de polarite (fichier)"

// Même ordre que `solenoid_profile_t` / solenoid_profile_config.h.
static const field_t g_fields[] = {
    FIELD(pwm_freq_hz, LOCK_PWM),
    FIELD(impact_duration_us, NULL),
    FIELD(impact_duty_pct, NULL),
    FIELD(impact_max_us, NULL),
    FIELD(pull_duration_us, NULL),
    FIELD(pull_duty_pct, NULL),
    FIELD(pull_max_us, NULL),
    FIELD(hold_duty_pct, NULL),
    FIELD(hold_duty_max_pct, NULL),
    FIELD(hold_max_ms, NULL),
    FIELD(release_duration_us, NULL),
    FIELD(release_duty_pct, NULL),
    FIELD(probe_duration_us, LOCK_PROBE),
    FIELD(probe_duty_pct, LOCK_PROBE),
    FIELD(probe_max_us, LOCK_PROBE),
    FIELD(probe_duty_max_pct, LOCK_PROBE),
    FIELD(maint_duty_pct, NULL),
    FIELD(maint_duty_max_pct, NULL),
    FIELD(maint_max_ms, NULL),
    FIELD(maint_min_rest_ms, NULL),
    FIELD(min_rest_ms, NULL),
    FIELD(dir_safety_delay_us, LOCK_DEADTIME),
    FIELD(wake_delay_us, LOCK_WAKE),
    FIELD(dir_level_pull, LOCK_DIR),
    FIELD(dir_level_release, LOCK_DIR),
    FIELD(dir_level_maint, LOCK_DIR),
    FIELD(duty_max_pct, NULL),
};

#define FIELD_COUNT (sizeof(g_fields) / sizeof(g_fields[0]))

static uint32_t read_field(const solenoid_profile_t *profile, const field_t *field) {
    const uint8_t *base = (const uint8_t *)profile + field->offset;

    if (field->size == 1u) {
        return (uint32_t)base[0];
    }
    if (field->size == 2u) {
        uint16_t value;

        memcpy(&value, base, sizeof(value));
        return (uint32_t)value;
    }
    {
        uint32_t value;

        memcpy(&value, base, sizeof(value));
        return value;
    }
}

static uint32_t field_max(const field_t *field) {
    if (field->size == 1u) {
        return 0xFFu;
    }
    if (field->size == 2u) {
        return 0xFFFFu;
    }
    return 0xFFFFFFFFu;
}

static void write_field(solenoid_profile_t *profile, const field_t *field, uint32_t value) {
    uint8_t *base = (uint8_t *)profile + field->offset;

    if (field->size == 1u) {
        base[0] = (uint8_t)value;
    } else if (field->size == 2u) {
        const uint16_t narrow = (uint16_t)value;

        memcpy(base, &narrow, sizeof(narrow));
    } else {
        memcpy(base, &value, sizeof(value));
    }
}

size_t profile_tuning_field_count(void) {
    return FIELD_COUNT;
}

const char *profile_tuning_field_name(size_t index) {
    return index < FIELD_COUNT ? g_fields[index].name : "";
}

bool profile_tuning_field_is_tunable(size_t index) {
    return index < FIELD_COUNT && g_fields[index].lock_reason == NULL;
}

const char *profile_tuning_field_lock_reason(size_t index) {
    return index < FIELD_COUNT ? g_fields[index].lock_reason : NULL;
}

uint32_t profile_tuning_field_value(const solenoid_profile_t *profile, size_t index) {
    if (profile == NULL || index >= FIELD_COUNT) {
        return 0u;
    }
    return read_field(profile, &g_fields[index]);
}

bool profile_tuning_find(const char *name, size_t *index_out) {
    if (name == NULL) {
        return false;
    }
    for (size_t i = 0u; i < FIELD_COUNT; ++i) {
        if (strcmp(g_fields[i].name, name) == 0) {
            if (index_out != NULL) {
                *index_out = i;
            }
            return true;
        }
    }
    return false;
}

size_t profile_tuning_format_field(const solenoid_profile_t *profile, size_t index, char *out,
                                   size_t capacity) {
    int written;

    if (profile == NULL || out == NULL || index >= FIELD_COUNT || capacity == 0u) {
        return 0u;
    }
    written = snprintf(out, capacity, ".%s = %lu%s,", g_fields[index].name,
                       (unsigned long)read_field(profile, &g_fields[index]), "u");
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return (size_t)written;
}

size_t profile_tuning_diff_count(const solenoid_profile_t *a, const solenoid_profile_t *b) {
    size_t count = 0u;

    if (a == NULL || b == NULL) {
        return 0u;
    }
    for (size_t i = 0u; i < FIELD_COUNT; ++i) {
        if (read_field(a, &g_fields[i]) != read_field(b, &g_fields[i])) {
            ++count;
        }
    }
    return count;
}

// --- Analyse ------------------------------------------------------------------

static bool is_separator(char c) {
    return c == ' ' || c == '\t' || c == ',' || c == ';';
}

static bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static const char *skip_blanks(const char *p) {
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return p;
}

static void set_report_field(profile_tuning_report_t *report, const char *name) {
    if (report != NULL) {
        snprintf(report->field, sizeof(report->field), "%s", name);
    }
}

// Lit un entier décimal ou hexadécimal (0x...), suffixes u/U/l/L tolérés.
// Retourne NULL si ce n'est pas un nombre ou si la valeur dépasse 32 bits.
static const char *parse_number(const char *p, uint32_t *value_out) {
    uint64_t value = 0u;
    bool any = false;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        for (;; ++p) {
            uint32_t digit;

            if (*p >= '0' && *p <= '9') {
                digit = (uint32_t)(*p - '0');
            } else if (*p >= 'a' && *p <= 'f') {
                digit = (uint32_t)(*p - 'a') + 10u;
            } else if (*p >= 'A' && *p <= 'F') {
                digit = (uint32_t)(*p - 'A') + 10u;
            } else {
                break;
            }
            value = (value << 4u) | digit;
            if (value > 0xFFFFFFFFu) {
                return NULL;
            }
            any = true;
        }
    } else {
        for (; *p >= '0' && *p <= '9'; ++p) {
            value = value * 10u + (uint64_t)(*p - '0');
            if (value > 0xFFFFFFFFu) {
                return NULL;
            }
            any = true;
        }
    }
    if (!any) {
        return NULL;
    }
    while (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L') {
        ++p;
    }
    if (*p != '\0' && !is_separator(*p) && !(p[0] == '/' && p[1] == '/')) {
        return NULL;
    }
    *value_out = (uint32_t)value;
    return p;
}

profile_tuning_result_t profile_tuning_apply(const char *text, solenoid_profile_t *profile,
                                             profile_tuning_report_t *report) {
    solenoid_profile_t work;
    const char *p = text;
    size_t count = 0u;
    solenoid_profile_result_t structure;

    if (report != NULL) {
        memset(report, 0, sizeof(*report));
        report->structure = SOLENOID_PROFILE_OK;
    }
    if (text == NULL || profile == NULL) {
        return PROFILE_TUNING_ERR_SYNTAX;
    }
    work = *profile;

    for (;;) {
        char name[PROFILE_TUNING_NAME_MAX];
        size_t name_len = 0u;
        size_t index;
        uint32_t value;
        const char *after;

        while (is_separator(*p)) {
            ++p;
        }
        if (*p == '\0' || (p[0] == '/' && p[1] == '/')) {
            break;
        }
        if (*p == '.') {
            ++p;
        }
        while (is_name_char(*p)) {
            if (name_len + 1u >= sizeof(name)) {
                return PROFILE_TUNING_ERR_SYNTAX;
            }
            name[name_len++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
            ++p;
        }
        name[name_len] = '\0';
        if (name_len == 0u) {
            return PROFILE_TUNING_ERR_SYNTAX;
        }
        set_report_field(report, name);

        p = skip_blanks(p);
        if (*p == '=') {
            ++p;
        }
        p = skip_blanks(p);
        after = parse_number(p, &value);
        if (!profile_tuning_find(name, &index)) {
            return PROFILE_TUNING_ERR_UNKNOWN_FIELD;
        }
        if (g_fields[index].lock_reason != NULL) {
            return PROFILE_TUNING_ERR_LOCKED;
        }
        if (after == NULL) {
            return PROFILE_TUNING_ERR_NUMBER;
        }
        if (value > field_max(&g_fields[index])) {
            if (report != NULL) {
                report->range_max = field_max(&g_fields[index]);
            }
            return PROFILE_TUNING_ERR_RANGE;
        }
        write_field(&work, &g_fields[index], value);
        ++count;
        p = after;
    }

    if (count == 0u) {
        return PROFILE_TUNING_ERR_EMPTY;
    }
    if (report != NULL) {
        report->field[0] = '\0';
        report->assigned = count;
    }
    structure = solenoid_profile_validate(&work);
    if (structure != SOLENOID_PROFILE_OK) {
        if (report != NULL) {
            report->structure = structure;
        }
        return PROFILE_TUNING_ERR_STRUCTURE;
    }
    *profile = work;
    return PROFILE_TUNING_OK;
}

static const char *structure_text(solenoid_profile_result_t structure) {
    switch (structure) {
    case SOLENOID_PROFILE_ERR_DUTY:
        return "duty > 100 % ou au-dessus d'un plafond "
               "(hold_duty_pct <= hold_duty_max_pct, tout duty <= duty_max_pct)";
    case SOLENOID_PROFILE_ERR_CONSISTENCY:
        return "duree au-dessus de son plafond (pull_duration_us <= pull_max_us)";
    case SOLENOID_PROFILE_ERR_DIR:
        return "niveaux de polarite invalides";
    case SOLENOID_PROFILE_ERR_PWM:
        return "frequence PWM hors plage";
    case SOLENOID_PROFILE_ERR_DURATION:
        return "duree en ms > 4000000";
    default:
        return "controle structurel en echec";
    }
}

const char *profile_tuning_describe(profile_tuning_result_t result,
                                    const profile_tuning_report_t *report, char *out,
                                    size_t capacity) {
    const char *field = (report != NULL) ? report->field : "";
    size_t index;

    if (out == NULL || capacity == 0u) {
        return "";
    }
    switch (result) {
    case PROFILE_TUNING_OK:
        snprintf(out, capacity, "ok");
        break;
    case PROFILE_TUNING_ERR_EMPTY:
        snprintf(out, capacity, "aucune affectation (ex. p set pull_duration_us 12000)");
        break;
    case PROFILE_TUNING_ERR_SYNTAX:
        snprintf(out, capacity, "syntaxe invalide (attendu: nom valeur, ou .nom = valeur,)");
        break;
    case PROFILE_TUNING_ERR_UNKNOWN_FIELD:
        snprintf(out, capacity, "champ inconnu '%s' (p fields pour la liste)", field);
        break;
    case PROFILE_TUNING_ERR_LOCKED:
        snprintf(out, capacity, "champ '%s' non reglable a chaud: %s", field,
                 profile_tuning_find(field, &index) ? g_fields[index].lock_reason : "?");
        break;
    case PROFILE_TUNING_ERR_NUMBER:
        snprintf(out, capacity, "valeur invalide pour '%s' (entier <= 4294967295, ou 0x..)",
                 field);
        break;
    case PROFILE_TUNING_ERR_RANGE:
        snprintf(out, capacity, "valeur trop grande pour '%s' (max %lu)", field,
                 (unsigned long)(report != NULL ? report->range_max : 0u));
        break;
    case PROFILE_TUNING_ERR_STRUCTURE:
        snprintf(out, capacity, "profil incoherent: %s",
                 structure_text(report != NULL ? report->structure : SOLENOID_PROFILE_OK));
        break;
    default:
        snprintf(out, capacity, "erreur");
        break;
    }
    return out;
}
