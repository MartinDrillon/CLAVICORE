#include "bringup_console.h"

#include <stdio.h>

#include "pico/stdlib.h"

#include "can_link.h"

#ifndef CLAVIER_CDC_CONSOLE
#define CLAVIER_CDC_CONSOLE 1
#endif

#if CLAVIER_CDC_CONSOLE

#define CONSOLE_LINE_MAX 8u

static char g_line[CONSOLE_LINE_MAX];
static uint8_t g_length;

static void print_help(void) {
    printf("[CONSOLE] t=auto-test c=mode_diagnostic d=table m=etat ?=aide\n");
    printf("[CONSOLE] aucune commande n'ecrit ni n'efface la table\n");
}

static void execute(char command) {
    switch (command) {
    case 't':
    case 'T':
        switch (can_link_request_selftest()) {
        case CAN_LINK_REQUEST_SENT:
            printf("[CONSOLE] t -> auto-test demande (voir [TRIGGER])\n");
            break;
        case CAN_LINK_REQUEST_BUSY:
            printf("[CONSOLE] t -> refuse (occupé, voir [STATUS])\n");
            break;
        default:
            printf("[CONSOLE] t -> echec d'emission CAN\n");
            break;
        }
        break;
    case 'c':
    case 'C': {
        const bool enabled = !can_link_calibration_enabled();

        can_link_set_calibration_enabled(enabled);
        printf("[CONSOLE] c -> mode diagnostic %s\n", enabled ? "ON" : "OFF");
        break;
    }
    case 'd':
    case 'D':
        printf("[CONSOLE] d -> table persistée et correspondance\n");
        can_link_dump_table();
        break;
    case 'm':
    case 'M':
        printf("[CONSOLE] m -> etat\n");
        can_link_dump_status();
        break;
    case '?':
        print_help();
        break;
    default:
        printf("[CONSOLE] commande inconnue (?=aide)\n");
        break;
    }
}

void bringup_console_init(void) {
    g_length = 0u;
    for (uint8_t i = 0u; i < CONSOLE_LINE_MAX; ++i) {
        g_line[i] = '\0';
    }
}

void bringup_console_poll(void) {
    int c;

    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (g_length > 0u) {
                execute(g_line[0]);
                g_length = 0u;
                g_line[0] = '\0';
            }
            continue;
        }
        if (g_length >= (CONSOLE_LINE_MAX - 1u)) {
            g_length = 0u;  // ligne trop longue : ignorée
            g_line[0] = '\0';
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
