#ifndef USB_LOG_H
#define USB_LOG_H

#include <stdint.h>

// Journal texte sur USB CDC (ADR-0003 addendum CDC). printf() est redirigé ici.
// Ne bloque jamais ; si le port n'est pas ouvert, les plus anciens octets sont écrasés.
// printf() doit être appelé depuis la boucle principale uniquement (jamais depuis une IRQ).

typedef struct {
    uint32_t dropped_bytes;
} usb_log_stats_t;

void usb_log_init(void);

// À appeler à chaque tour de boucle, après midi_task() (qui exécute tud_task()).
void usb_log_task(void);
usb_log_stats_t usb_log_stats(void);

#endif
