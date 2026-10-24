// Tests du point d'émission central ADR-0025 (niveaux, catégories, horodatage).
// `diag_out.c` est compilé avec une horloge Pico simulée.
#include <io.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "diag_out.h"
#include "pico/time.h"

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            fprintf(stderr, "ECHEC %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define CAPTURE_PATH "build/host_tests/diag_out_capture.txt"

absolute_time_t get_absolute_time(void) { return 5000u; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)t; }
void sleep_us(uint64_t us) { (void)us; }

static bool file_contains(const char *needle) {
    char buffer[4096];
    size_t got;
    FILE *f;

    fflush(stdout);
    f = fopen(CAPTURE_PATH, "rb");
    if (f == NULL) {
        return false;
    }
    got = fread(buffer, 1u, sizeof(buffer) - 1u, f);
    fclose(f);
    buffer[got] = '\0';
    return strstr(buffer, needle) != NULL;
}

int main(void) {
    const int console_fd = _dup(_fileno(stdout));

    if (console_fd < 0 || freopen(CAPTURE_PATH, "wb", stdout) == NULL) {
        return 2;
    }
    diag_out_init();

    // L-01/L-02/L-03 : niveau et horodatage.
    diag_out_printf_class(DIAG_CLASS_ALWAYS, "A0");
    CHECK(file_contains("[T+5000 ms] A0"));
    CHECK(diag_out_level() == 1u);

    diag_out_toggle_timestamp();
    diag_out_printf_class(DIAG_CLASS_ALWAYS, "A1");
    CHECK(file_contains("A1"));
    CHECK(!file_contains("[T+5000 ms] A1"));

    // L-06/L-07/L-11/L-12 : notes/RAW/PUSH sont TRACE, donc invisibles à 0/1.
    diag_out_set_level(0u);
    diag_out_printf_class(DIAG_CLASS_TRACE, "TRACE0");
    diag_out_printf_class(DIAG_CLASS_BRINGUP, "BRING0");
    diag_out_printf_class(DIAG_CLASS_NORMAL, "NORM0");
    diag_out_printf_class(DIAG_CLASS_CONSOLE, "CONS0");
    CHECK(!file_contains("TRACE0"));
    CHECK(!file_contains("BRING0"));
    CHECK(file_contains("NORM0"));
    CHECK(file_contains("CONS0"));

    // L-08/L-09 : bring-up visible à 1, trace toujours invisible.
    diag_out_set_level(1u);
    diag_out_printf_class(DIAG_CLASS_BRINGUP, "BRING1");
    diag_out_printf_class(DIAG_CLASS_TRACE, "TRACE1");
    CHECK(file_contains("BRING1"));
    CHECK(!file_contains("TRACE1"));

    // L-25 : niveau 2 disponible seulement si TRACE est compilé.
    CHECK(diag_out_set_level(2u) == diag_out_trace_available());
    if (diag_out_trace_available()) {
        diag_out_printf_class(DIAG_CLASS_TRACE, "TRACE2");
        CHECK(file_contains("TRACE2"));
    }

    // L-10 : ALWAYS reste visible à tous les niveaux.
    diag_out_set_level(0u);
    diag_out_printf_class(DIAG_CLASS_ALWAYS, "ALWAYS0");
    CHECK(file_contains("ALWAYS0"));

    fflush(stdout);
    _dup2(console_fd, _fileno(stdout));
    _close(console_fd);
    printf("%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
