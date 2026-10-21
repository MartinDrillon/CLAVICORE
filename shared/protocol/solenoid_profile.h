// Profil d'activation des solénoïdes — SOURCE UNIQUE du format (ADR-0013, ADR-0014).
//
// Rôle : décrire le profil réglé dans `firmware/clavier/src/solenoid_profile_config.h`,
// transmis aux cartes Moteur par les trames `PROFILE_*` (`0x420`–`0x423`,
// `can_protocol.h`) au lancement de l'auto-test IPROPI, puis persisté en flash
// côté Moteur (deux secteurs A/B).
//
// Principes (ADR-0014 : dérogation à l'invariant 4) :
//  - TOUTES les limites d'activation sont des données du profil ; ce fichier ne
//    définit AUCUN seuil thermique ni fenêtre de courant. Une valeur cohérente
//    mais dangereuse est obéie : la sécurité est procédurale (relecture humaine,
//    essais au banc), pas garantie par le firmware ;
//  - seuls des contrôles STRUCTURELS (représentabilité et cohérence) sont
//    appliqués ; un profil qui les échoue est rejeté en bloc, jamais écrêté ;
//  - sérialisation explicite little-endian, champ par champ, sans `memcpy` de
//    structure : le format est stable et testable sur PC (`tests/host/`).
//
// Le Clavier et le Moteur partagent ce fichier. Toute évolution du format passe
// par une nouvelle `SOLENOID_PROFILE_FORMAT_VERSION` et une ADR (invariant 5).
#ifndef CLAVICORE_SOLENOID_PROFILE_H
#define CLAVICORE_SOLENOID_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Format du payload ------------------------------------------------------

// Version du format canonique. `v1` = 57 octets, décrit ci-dessous.
#define SOLENOID_PROFILE_FORMAT_VERSION 1u

// Taille canonique du payload v1, en octets.
#define SOLENOID_PROFILE_WIRE_SIZE 57u

// Taille maximale admise par le récepteur (marge pour les versions futures).
#define SOLENOID_PROFILE_WIRE_MAX 128u

// --- Bornes STRUCTURELLES (représentabilité / cohérence, pas de sécurité) ----

#define SOLENOID_PROFILE_PWM_FREQ_MIN_HZ 1u
#define SOLENOID_PROFILE_PWM_FREQ_MAX_HZ 100000u
// Durées en ms convertibles en µs sur 32 bits (le driver compare des µs u32).
#define SOLENOID_PROFILE_DURATION_MS_MAX 4000000u

// --- Enregistrement de persistance flash (Moteur) ---------------------------

// `magic` little-endian des octets 'C','V','P','F'.
#define SOLENOID_PROFILE_FLASH_MAGIC 0x46505643u
#define SOLENOID_PROFILE_FLASH_VERSION 1u
// En-tête du slot : magic u32, version u16, header_size u16, payload_len u16,
// reserved u16, seq u32, crc32 u32, reserved[12].
#define SOLENOID_PROFILE_FLASH_HEADER_SIZE 32u
#define SOLENOID_PROFILE_FLASH_SLOT_SIZE 4096u
#define SOLENOID_PROFILE_FLASH_SLOT_COUNT 2u

// --- Structure en RAM -------------------------------------------------------

// L'ordre des champs suit l'ordre du payload canonique (hors l'octet
// `format_version` en tête). Unités : µs pour PULL/RELEASE/PROBE et les délais,
// ms pour HOLD/MAINTENANCE et les repos, % pour les duties, Hz pour la PWM.
//
// Layout du payload v1 (57 octets, little-endian) :
//   [0]     format_version      u8
//   [1..4]  pwm_freq_hz         u32
//   [5..8]  pull_duration_us    u32
//   [9]     pull_duty_pct       u8
//   [10..13] pull_max_us        u32
//   [14]    hold_duty_pct       u8
//   [15]    hold_duty_max_pct   u8
//   [16..19] hold_max_ms        u32
//   [20..23] release_duration_us u32
//   [24]    release_duty_pct    u8
//   [25..28] probe_duration_us  u32
//   [29]    probe_duty_pct      u8
//   [30..33] probe_max_us       u32
//   [34]    probe_duty_max_pct  u8
//   [35]    maint_duty_pct      u8
//   [36]    maint_duty_max_pct  u8
//   [37..40] maint_max_ms       u32
//   [41..44] maint_min_rest_ms  u32
//   [45..48] min_rest_ms        u32
//   [49..50] dir_safety_delay_us u16
//   [51..52] wake_delay_us      u16
//   [53]    dir_level_pull      u8
//   [54]    dir_level_release   u8
//   [55]    dir_level_maint     u8
//   [56]    duty_max_pct        u8
typedef struct {
    uint32_t pwm_freq_hz;
    uint32_t pull_duration_us;
    uint8_t pull_duty_pct;
    uint32_t pull_max_us;
    uint8_t hold_duty_pct;
    uint8_t hold_duty_max_pct;
    uint32_t hold_max_ms;
    uint32_t release_duration_us;
    uint8_t release_duty_pct;
    uint32_t probe_duration_us;
    uint8_t probe_duty_pct;
    uint32_t probe_max_us;
    uint8_t probe_duty_max_pct;
    uint8_t maint_duty_pct;
    uint8_t maint_duty_max_pct;
    uint32_t maint_max_ms;
    uint32_t maint_min_rest_ms;
    uint32_t min_rest_ms;
    uint16_t dir_safety_delay_us;
    uint16_t wake_delay_us;
    uint8_t dir_level_pull;
    uint8_t dir_level_release;
    uint8_t dir_level_maint;
    uint8_t duty_max_pct;
} solenoid_profile_t;

// --- Résultat des contrôles structurels -------------------------------------

typedef enum {
    SOLENOID_PROFILE_OK = 0,
    SOLENOID_PROFILE_ERR_NULL,
    SOLENOID_PROFILE_ERR_VERSION,
    SOLENOID_PROFILE_ERR_LENGTH,
    SOLENOID_PROFILE_ERR_DUTY,        // duty > 100 % ou plafond < valeur opératoire
    SOLENOID_PROFILE_ERR_CONSISTENCY, // durée > son propre plafond, etc.
    SOLENOID_PROFILE_ERR_DIR,         // niveaux de polarité invalides
    SOLENOID_PROFILE_ERR_PWM,         // fréquence hors plage réalisable
    SOLENOID_PROFILE_ERR_DURATION,    // valeur en ms non convertible en µs u32
} solenoid_profile_result_t;

// --- Helpers purs -----------------------------------------------------------

// CRC32 reflété, polynôme 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF
// (même famille que `commissioning_table.c`).
uint32_t solenoid_profile_crc32(const uint8_t *data, size_t length);

// Sérialise le profil dans `out` (au moins SOLENOID_PROFILE_WIRE_SIZE octets).
// Retourne le nombre d'octets écrits, ou 0 si `capacity` est insuffisante.
size_t solenoid_profile_encode(const solenoid_profile_t *profile, uint8_t *out,
                               size_t capacity);

// Désérialise un payload canonique. Vérifie la longueur exacte de la version
// annoncée. Retourne false sur version inconnue ou longueur incorrecte.
bool solenoid_profile_decode(const uint8_t *data, size_t length,
                             solenoid_profile_t *out);

// Contrôles STRUCTURELS (voir SOLENOID_PROFILE_ERR_*). Ne borne aucun seuil
// thermique. À appeler par le Clavier avant l'envoi et par le Moteur à la
// réception.
solenoid_profile_result_t solenoid_profile_validate(const solenoid_profile_t *profile);

// CRC32 du payload canonique d'un profil : identité comparée au boot entre le
// Clavier et chaque carte Moteur (champ `profile_tag32` de PROFILE_STATUS).
uint32_t solenoid_profile_tag(const solenoid_profile_t *profile);

#endif  // CLAVICORE_SOLENOID_PROFILE_H
