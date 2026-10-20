// Feature « clock-test-mode1 » (docs/specs/feature/clock-test-module.md).
// Diagnostic uniquement : sortie carrée 1 MHz (clk_sys / 150 si clk_sys = 150 MHz)
// sur GPIO41 pour mesurer la fréquence réelle de clk_sys. Désactivable :
// cmake -DMOTEUR_ENABLE_CLOCK_TEST=OFF (aucun code compilé, stubs ci-dessous).
#ifndef MOTEUR_CLOCK_TEST_H
#define MOTEUR_CLOCK_TEST_H

#include <stdbool.h>
#include <stdint.h>

#ifndef MOTEUR_ENABLE_CLOCK_TEST
#define MOTEUR_ENABLE_CLOCK_TEST 1  // défaut ON, aligné sur l'option CMake
#endif

typedef enum {
    CLK_TEST_IDLE = 0,
    CLK_TEST_ACTIVE,
} clock_test_state_t;

typedef enum {
    CLK_TEST_EVENT_NONE = 0,
    CLK_TEST_EVENT_ENTERED,  // vient de passer ACTIVE (appui long 5,0 s)
    CLK_TEST_EVENT_EXITED,   // vient de repasser IDLE (nouvel appui)
} clock_test_event_t;

#if MOTEUR_ENABLE_CLOCK_TEST

void clock_test_init(void);

// pressed : état ANTI-REBONDI du bouton (true = appuyé). Non bloquant.
// Appeler à chaque tour de boucle.
clock_test_event_t clock_test_update(bool pressed, uint32_t now_us);

clock_test_state_t clock_test_state(void);

#else  // stubs : main.c compile sans #if

static inline void clock_test_init(void) {}
static inline clock_test_event_t clock_test_update(bool pressed, uint32_t now_us) {
    (void)pressed;
    (void)now_us;
    return CLK_TEST_EVENT_NONE;
}
static inline clock_test_state_t clock_test_state(void) { return CLK_TEST_IDLE; }

#endif

#endif  // MOTEUR_CLOCK_TEST_H
