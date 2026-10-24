#include "diag_out.h"

#include <stdarg.h>
#include <stdio.h>

#include "pico/time.h"

static uint8_t g_level = 1u;
static bool g_timestamp_enabled = true;

void diag_out_init(void) {
    const int configured = (int)CLAVIER_DIAG_DEFAULT_LEVEL;

    if (configured < 0 || configured > 2) {
        g_level = 1u;
    } else {
        g_level = (uint8_t)configured;
    }
#if !CLAVIER_DIAG_TRACE
    if (g_level > 1u) {
        g_level = 1u;
    }
#endif
    g_timestamp_enabled = true;
}

bool diag_out_set_level(uint8_t level) {
    if (level > 2u) {
        return false;
    }
    if (level == 2u && !diag_out_trace_available()) {
        return false;
    }
    g_level = level;
    return true;
}

uint8_t diag_out_level(void) {
    return g_level;
}

bool diag_out_trace_available(void) {
    return CLAVIER_DIAG_TRACE != 0;
}

bool diag_out_is_enabled(diag_class_t category) {
    return diag_class_enabled_for_level(category, g_level, diag_out_trace_available());
}

void diag_out_toggle_timestamp(void) {
    g_timestamp_enabled = !g_timestamp_enabled;
}

bool diag_out_timestamp_enabled(void) {
    return g_timestamp_enabled;
}

// ADR-0017 IMP-002 : le build peut fournir un identifiant (`CLAVIER_BUILD_ID`) ;
// à défaut, la date et l'heure de compilation font office d'identité. Cette
// identité n'est jamais interprétée par le firmware.
#ifndef CLAVIER_BUILD_ID
#define CLAVIER_BUILD_ID (__DATE__ " " __TIME__)
#endif

uint32_t diag_out_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

void diag_out_line_class(diag_class_t category, const char *line) {
    if (line == NULL || line[0] == '\0' || !diag_out_is_enabled(category)) {
        return;
    }
    if (g_timestamp_enabled) {
        printf("[T+%lu ms] %s\n", (unsigned long)diag_out_now_ms(), line);
    } else {
        printf("%s\n", line);
    }
}

void diag_out_line(const char *line) {
    diag_out_line_class(DIAG_CLASS_ALWAYS, line);
}

static void diag_out_vprintf_class(diag_class_t category, const char *format, va_list args) {
    char buffer[DIAG_OUT_LINE_MAX];
    int written;
    size_t length;

    written = vsnprintf(buffer, sizeof(buffer), format, args);
    if (written < 0) {
        return;
    }
    // Les formats convertis depuis `printf("...\n")` portent déjà un saut de
    // ligne ; on retire les fins de ligne pour ne pas produire de ligne vide.
    length = ((size_t)written >= sizeof(buffer)) ? (sizeof(buffer) - 1u) : (size_t)written;
    while (length > 0u && (buffer[length - 1u] == '\n' || buffer[length - 1u] == '\r')) {
        --length;
    }
    buffer[length] = '\0';
    diag_out_line_class(category, buffer);
}

void diag_out_printf_class(diag_class_t category, const char *format, ...) {
    va_list args;

    if (format == NULL || !diag_out_is_enabled(category)) {
        return;
    }
    va_start(args, format);
    diag_out_vprintf_class(category, format, args);
    va_end(args);
}

void diag_out_printf(const char *format, ...) {
    va_list args;

    if (format == NULL) {
        return;
    }
    va_start(args, format);
    diag_out_vprintf_class(DIAG_CLASS_ALWAYS, format, args);
    va_end(args);
}

const char *diag_out_build_id(void) {
    return CLAVIER_BUILD_ID;
}
