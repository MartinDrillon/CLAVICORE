#include "diag_out.h"

#include <stdarg.h>
#include <stdio.h>

#include "pico/time.h"

// ADR-0017 IMP-002 : le build peut fournir un identifiant (`CLAVIER_BUILD_ID`) ;
// à défaut, la date et l'heure de compilation font office d'identité. Cette
// identité n'est jamais interprétée par le firmware.
#ifndef CLAVIER_BUILD_ID
#define CLAVIER_BUILD_ID (__DATE__ " " __TIME__)
#endif

uint32_t diag_out_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

void diag_out_line(const char *line) {
    if (line == NULL || line[0] == '\0') {
        return;
    }
    printf("[T+%lu ms] %s\n", (unsigned long)diag_out_now_ms(), line);
}

void diag_out_printf(const char *format, ...) {
    char buffer[DIAG_OUT_LINE_MAX];
    va_list args;
    int written;

    if (format == NULL) {
        return;
    }
    va_start(args, format);
    written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    diag_out_line(buffer);
}

const char *diag_out_build_id(void) {
    return CLAVIER_BUILD_ID;
}
