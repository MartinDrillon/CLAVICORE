#ifndef MOTEUR_SOLENOID_PROFILE_STORE_H
#define MOTEUR_SOLENOID_PROFILE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "can_protocol.h"
#include "solenoid_profile.h"

// Charge le profil persistant au boot : lit les deux slots flash, applique le
// slot valide de `seq` le plus élevé, ou les valeurs de repli compilées si
// aucun slot n'est valide (ADR-0013 §5). À appeler une fois au démarrage.
void solenoid_profile_store_load(void);

// Profil actif en RAM (jamais NULL). Lu par le driver et l'auto-test.
const solenoid_profile_t *solenoid_profile_active(void);

// CRC32 du payload du profil actif (identité comparée au boot, ADR-0013 §6).
uint32_t solenoid_profile_active_tag(void);

// Origine du profil actif : DEFAULTS (repli) ou NVM (flash).
can_profile_source_t solenoid_profile_active_source(void);

// Écrit `profile` en flash (slot au `seq` le plus faible, `seq + 1`), relit et
// vérifie, puis l'applique en RAM. Doit être appelé hors IRQ, tous canaux IDLE.
// Retourne false si l'écriture ou la vérification échoue (le profil actif est
// alors conservé).
bool solenoid_profile_store_write(const solenoid_profile_t *profile, uint32_t tag);

#endif  // MOTEUR_SOLENOID_PROFILE_STORE_H
