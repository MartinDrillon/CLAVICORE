#ifndef USB_LOG_H
#define USB_LOG_H

// Journal texte sur USB CDC (ADR-0003 addendum CDC). printf() est redirigé ici.
// Ne bloque jamais ; si le port n'est pas ouvert, les plus anciens octets sont écrasés.
// printf() doit être appelé depuis la boucle principale uniquement (jamais depuis une IRQ).

void usb_log_init(void);

// À appeler à chaque tour de boucle, après midi_task() (qui exécute tud_task()).
void usb_log_task(void);

#endif
