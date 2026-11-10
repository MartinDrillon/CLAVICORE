#ifndef USB_LOG_H
#define USB_LOG_H

#include <stddef.h>
#include <stdint.h>

#ifndef CLAVIER_SERIAL_STUDIO
#define CLAVIER_SERIAL_STUDIO 0
#endif

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

// Lit un octet reçu sur le CDC (saisie PuTTY). Retourne -1 s'il n'y en a pas.
// Non bloquant. À appeler depuis la boucle principale uniquement.
int usb_log_read_char(void);

// Second port CDC (ADR-0032 §11.3) : sortie Serial Studio Quick Plot. Le port 1
// reste le journal/console ; ces fonctions n'écrivent que sur le port 2 et ne
// bloquent jamais. `CLAVIER_SERIAL_STUDIO=0` les réduit à des no-op, sans
// descripteur ni code ajouté au binaire.
#if CLAVIER_SERIAL_STUDIO
void serial_studio_init(void);
void serial_studio_write(const char *text, size_t length);
void serial_studio_printf(const char *format, ...);
void serial_studio_task(void);
#else
static inline void serial_studio_init(void) {}
static inline void serial_studio_write(const char *text, size_t length) {
    (void)text;
    (void)length;
}
static inline void serial_studio_printf(const char *format, ...) { (void)format; }
static inline void serial_studio_task(void) {}
#endif

#endif
