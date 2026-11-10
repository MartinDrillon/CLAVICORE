# Impact ADR-0032 — mode REC : écoute IPROPI, clavier maître MIDI

Fiche de lot pour le **Codeur** et le **Validateur**.
[ADR-0032](../adr/adr-0032-mode-rec-ecoute-ipropi-clavier-maitre.md) est
**`Accepted`** (2026-11-10) ; le §11 « Précisions d'implémentation » tranche les
points laissés ouverts (MIDI, `e bruit`, `trace_timeout_ms`, vélocité, refus
croisés, validation). Cette fiche ne décide rien de plus : tout écart est une
question ouverte en fin de document.

Comportement observable : sur le clavier acoustique, une touche jouée à la main
produit une note MIDI (USB, même canal que la matrice) sans que le solénoïde
soit alimenté, avec un journal `[LISTEN]`/`[REC]` sur PuTTY ; les seuils se
règlent à chaud par la console `e` ; l'option `CLAVIER_SERIAL_STUDIO=ON` ajoute
un second port COM qui trace les courbes dans Serial Studio.

Trois lots **séquentiels, chacun compilable seul** : Lot 1 `shared/protocol` +
tests hôte ; Lot 2 Moteur ; Lot 3 Clavier. Ne pas fusionner deux lots dans un
même commit.

---

## 0. Vérifications préalables (faites)

| Point | Constat |
|---|---|
| IDs `0x110`, `0x404`–`0x406`, `0x426`–`0x429` | **libres** — plan actuel `shared/protocol/can_protocol.h:26-76` |
| Commande console `e` | **libre** — dispatch `firmware/clavier/src/bringup_console.c:218-293` (prises : `t c d m l ? p .`) |
| Second CDC | faisable — `firmware/clavier/src/tusb_config.h:13`, endpoints libres `usb_descriptors.c:11-15` |
| `CAN_PROTO_VERSION` | reste **1** (aucune trame existante modifiée) |
| MIDI DIN / MIDI IN | **absents du code** ; §11 : hors périmètre, les notes captées suivent `midi.c` (USB) |

Aucune limite électrique n'est ajoutée, durcie ni retirée : `settle_us`,
`drive_blank_ms`, `trace_timeout_ms`, seuils et vélocité sont des **paramètres
d'écoute**, pas des limites d'activation ; l'auto-test garde
`IPROPI_SELFTEST_MUX_SETTLE_US = 150` (`firmware/moteur/src/ipropi_selftest.h:12`).

---

# Lot 1 — `shared/protocol` : trames, paramètres, codecs

## 1.1 Fichiers

| Fichier | Action | Ancrage / raison |
|---|---|---|
| `shared/protocol/can_protocol.h` | modifier | plan d'IDs `:26-49`, DLC `:51-65`, enums `:178-230`, prototypes `:328-340` et fin `:423-438` |
| `shared/protocol/can_protocol.c` | modifier | modèle des codecs `:70-103` ; helpers `:12-34` |
| `shared/protocol/listen_params.h` | **créer** | ids de champs, struct, validate, CRC16, sérialisation |
| `shared/protocol/listen_params.c` | **créer** | idem |
| `firmware/moteur/CMakeLists.txt` | modifier | ajouter `listen_params.c` aux sources `:41-53` (compilé par le Moteur, sans valeurs) |
| `firmware/clavier/CMakeLists.txt` | modifier | ajouter `listen_params.c` aux sources `:38-57` |
| `tests/host/test_listen_protocol.c` | **créer** | codecs des 8 trames + `listen_params` |
| `tests/host/run_host_tests.ps1` | modifier | nouveau bloc `test_listen_protocol.exe` après `:100` |

## 1.2 Constantes CAN (à ajouter dans `can_protocol.h`)

```c
// --- Mode REC : écoute IPROPI (ADR-0032) -----------------------------------
#define CAN_ID_LISTEN_PULSE        0x110u  // Moteur -> Clavier
#define CAN_ID_LISTEN_CONTROL      0x404u  // Clavier -> diffusion
#define CAN_ID_LISTEN_PARAM        0x405u  // Clavier -> diffusion
#define CAN_ID_LISTEN_NOISE_QUERY  0x406u  // Clavier -> diffusion
#define CAN_ID_LISTEN_STATUS       0x426u  // Moteur -> Clavier
#define CAN_ID_LISTEN_NOISE_REPORT 0x427u  // Moteur -> Clavier
#define CAN_ID_LISTEN_PULSE_TIMING 0x428u  // Moteur -> Clavier
#define CAN_ID_LISTEN_TRACE        0x429u  // Moteur -> Clavier

#define CAN_DLC_LISTEN_PULSE        8u
#define CAN_DLC_LISTEN_CONTROL      8u
#define CAN_DLC_LISTEN_PARAM        8u
#define CAN_DLC_LISTEN_NOISE_QUERY  6u
#define CAN_DLC_LISTEN_STATUS       8u
#define CAN_DLC_LISTEN_NOISE_REPORT 8u
#define CAN_DLC_LISTEN_PULSE_TIMING 8u
#define CAN_DLC_LISTEN_TRACE        8u

// Champs 12 bits et drapeaux (octets de contrôle).
#define CAN_LISTEN_ADC_MASK          0x0FFFu  // base, crête, min, max
#define CAN_LISTEN_CHANNEL_MASK      0x0Fu
#define CAN_LISTEN_PULSE_FLAG_SPLIT     0x10u  // bit 4 : coupure de trille
#define CAN_LISTEN_PULSE_FLAG_SATURATED 0x20u  // bit 5 : durée > 0xFFFF x 100 µs
#define CAN_LISTEN_NOISE_FLAG_TRUNCATED 0x01u  // fenêtre > 256 ms
#define CAN_LISTEN_TIMING_SATURATED 0xFFFFu    // 100 µs -> 6,55 s
#define CAN_LISTEN_TRACE_FRAME_SAMPLES 4u
#define CAN_LISTEN_TRACE_SAMPLES 4096u
```

## 1.3 Enums et structures (commentées, à mettre dans `can_protocol.h`)

```c
// Mode porté par LISTEN_CONTROL[1] (0 OFF, 1 ÉCOUTE, 2 TRACE, 3 STREAM,
// 4 STREAM_OFF). STREAM_OFF revient au balayage sans quitter l'écoute.
typedef enum {
    CAN_LISTEN_MODE_OFF = 0,
    CAN_LISTEN_MODE_LISTEN = 1,
    CAN_LISTEN_MODE_TRACE = 2,
    CAN_LISTEN_MODE_STREAM = 3,
    CAN_LISTEN_MODE_STREAM_OFF = 4,
} can_listen_mode_t;

// État porté par LISTEN_STATUS[2]. Aucun état n'est silencieux : une écoute
// qui ne peut pas tourner est toujours nommée.
typedef enum {
    CAN_LISTEN_STATE_IDLE = 0,             // éteinte, Core 1 parqué
    CAN_LISTEN_STATE_LISTENING = 1,        // balayage 12 canaux
    CAN_LISTEN_STATE_TRACE = 2,            // capture d'un canal en cours
    CAN_LISTEN_STATE_STREAM = 3,           // flux d'un canal pendant le balayage
    CAN_LISTEN_STATE_PARAM_MISMATCH = 4,   // CRC ou validation du jeu refusé
    CAN_LISTEN_STATE_BUSY_SELFTEST = 5,    // LISTEN_CONTROL reçu pendant 0x401
    CAN_LISTEN_STATE_UNAVAILABLE_SLEEP = 6,// nSLEEP bas
    CAN_LISTEN_STATE_TRACE_TIMEOUT = 7,    // trace_timeout_ms écoulé
    CAN_LISTEN_STATE_FAULT = 8,            // FAULT actif sur au moins un canal
} can_listen_state_t;

// LISTEN_PULSE (0x110, DLC 8) — une impulsion = appui OU relâchement (IPROPI
// mesure |i|, le sens n'est pas lisible).
typedef struct {
    uint16_t uid_tag;
    uint8_t channel;      // 0..11
    uint8_t seq;          // recopie dans LISTEN_PULSE_TIMING
    uint16_t base;        // ligne de base ADC au début de l'impulsion (0..4095)
    uint16_t peak;        // maximum de l'impulsion (0..4095)
    bool split;           // fin par coupure de trille
    bool saturated;       // durée saturée (voir LISTEN_PULSE_TIMING)
} can_listen_pulse_t;

// LISTEN_CONTROL (0x404, DLC 8) — Clavier vers diffusion.
// `param_crc16` = listen_params_crc16() du jeu envoyé avant l'ordre.
typedef struct {
    uint8_t proto_version;
    can_listen_mode_t mode;
    uint16_t param_crc16;
    uint16_t target_uid_tag;  // TRACE/STREAM ; 0 en OFF/LISTEN
    uint8_t channel;          // 0..11 en TRACE/STREAM ; 0 sinon
} can_listen_control_t;

// LISTEN_PARAM (0x405, DLC 8) — un champ, valeur u32 little-endian.
typedef struct {
    uint8_t proto_version;
    uint8_t param_id;   // listen_param_id_t (1..18)
    uint32_t value;
} can_listen_param_t;

// LISTEN_NOISE_QUERY (0x406, DLC 6) — Clavier vers diffusion.
typedef struct {
    uint16_t target_uid_tag;
    uint8_t channel;
    uint8_t request_id;    // 1..127, recopié dans le rapport
    uint16_t lookback_ms;  // <= 256, sinon le rapport est marqué tronqué
} can_listen_noise_query_t;

// LISTEN_STATUS (0x426, DLC 8) — état réel de la carte.
typedef struct {
    uint16_t uid_tag;
    can_listen_state_t state;
    uint8_t refused_param_id;  // 0 sauf refus de paramètre
    uint16_t sweep_period_us;  // période mesurée d'un balayage des 12 canaux
    uint8_t overflows;         // anneaux pleins, saturant à 255
} can_listen_status_t;

// LISTEN_NOISE_REPORT (0x427, DLC 8) — bruit du voisin demandé.
typedef struct {
    uint16_t uid_tag;
    uint8_t channel;
    uint8_t request_id;
    uint16_t min;   // 0..4095
    uint16_t max;   // 0..4095
    bool truncated; // fenêtre demandée > 256 ms
} can_listen_noise_report_t;

// LISTEN_PULSE_TIMING (0x428, DLC 8) — durée du même `seq`, unité 100 µs,
// saturée à 0xFFFF (6,55 s) ; `saturated` de LISTEN_PULSE est posé alors.
typedef struct {
    uint16_t uid_tag;
    uint8_t seq;
    uint16_t t_peak_100us;      // t_peak - t0
    uint16_t t_end_peak_100us;  // t_end - t_peak
} can_listen_pulse_timing_t;

// LISTEN_TRACE (0x429, DLC 8) — 4 échantillons 12 bits. Utilisée pour `trace`
// ET `stream`. Pas d'uid_tag (ADR §11) : le Clavier l'attribue à sa cible.
typedef struct {
    uint16_t seq;
    uint16_t samples[CAN_LISTEN_TRACE_FRAME_SAMPLES];  // 0..4095
} can_listen_trace_t;
```

## 1.4 Prototypes de codecs (à ajouter en fin de `can_protocol.h`)

```c
// LISTEN_PULSE (0x110, DLC 8)
can_proto_result_t can_encode_listen_pulse(uint8_t out[CAN_DLC_LISTEN_PULSE],
                                           const can_listen_pulse_t *pulse);
can_proto_result_t can_decode_listen_pulse(const uint8_t *data, uint8_t dlc,
                                           can_listen_pulse_t *pulse);
// LISTEN_CONTROL (0x404, DLC 8)
can_proto_result_t can_encode_listen_control(uint8_t out[CAN_DLC_LISTEN_CONTROL],
                                             const can_listen_control_t *control);
can_proto_result_t can_decode_listen_control(const uint8_t *data, uint8_t dlc,
                                             can_listen_control_t *control);
// LISTEN_PARAM (0x405, DLC 8)
can_proto_result_t can_encode_listen_param(uint8_t out[CAN_DLC_LISTEN_PARAM],
                                           const can_listen_param_t *param);
can_proto_result_t can_decode_listen_param(const uint8_t *data, uint8_t dlc,
                                           can_listen_param_t *param);
// LISTEN_NOISE_QUERY (0x406, DLC 6)
can_proto_result_t can_encode_listen_noise_query(uint8_t out[CAN_DLC_LISTEN_NOISE_QUERY],
                                                 const can_listen_noise_query_t *query);
can_proto_result_t can_decode_listen_noise_query(const uint8_t *data, uint8_t dlc,
                                                 can_listen_noise_query_t *query);
// LISTEN_STATUS (0x426, DLC 8)
can_proto_result_t can_encode_listen_status(uint8_t out[CAN_DLC_LISTEN_STATUS],
                                            const can_listen_status_t *status);
can_proto_result_t can_decode_listen_status(const uint8_t *data, uint8_t dlc,
                                            can_listen_status_t *status);
// LISTEN_NOISE_REPORT (0x427, DLC 8)
can_proto_result_t can_encode_listen_noise_report(uint8_t out[CAN_DLC_LISTEN_NOISE_REPORT],
                                                  const can_listen_noise_report_t *report);
can_proto_result_t can_decode_listen_noise_report(const uint8_t *data, uint8_t dlc,
                                                  can_listen_noise_report_t *report);
// LISTEN_PULSE_TIMING (0x428, DLC 8)
can_proto_result_t can_encode_listen_pulse_timing(uint8_t out[CAN_DLC_LISTEN_PULSE_TIMING],
                                                  const can_listen_pulse_timing_t *timing);
can_proto_result_t can_decode_listen_pulse_timing(const uint8_t *data, uint8_t dlc,
                                                  can_listen_pulse_timing_t *timing);
// LISTEN_TRACE (0x429, DLC 8)
can_proto_result_t can_encode_listen_trace(uint8_t out[CAN_DLC_LISTEN_TRACE],
                                           const can_listen_trace_t *trace);
can_proto_result_t can_decode_listen_trace(const uint8_t *data, uint8_t dlc,
                                           can_listen_trace_t *trace);

bool can_listen_mode_is_valid(can_listen_mode_t mode);
bool can_listen_state_is_valid(can_listen_state_t state);
bool can_listen_channel_is_valid(uint8_t channel);  // < CAN_IPROPI_CHANNEL_COUNT
```

## 1.5 Disposition des octets (contrat exact, little-endian)

Convention identique à l'existant : octets réservés émis à 0, ignorés en
réception ; tout DLC différent → `CAN_PROTO_BAD_DLC` ; canal ≥ 12, mode ou état
inconnu, `min > max` → `CAN_PROTO_BAD_VALUE` ; pointeur NULL → `CAN_PROTO_BAD_ARG`.

| Trame | Octets |
|---|---|
| `0x110` | `[0..1]` uid_tag ; `[2]` `(channel & 0x0F) \| split<<4 \| saturated<<5` ; `[3]` seq ; `[4]` base[7:0] ; `[5]` base[11:8] \| peak[3:0]<<4 ; `[6]` peak[11:4] ; `[7]` 0 |
| `0x404` | `[0]` proto ; `[1]` mode ; `[2..3]` param_crc16 ; `[4..5]` target_uid_tag ; `[6]` channel ; `[7]` 0 |
| `0x405` | `[0]` proto ; `[1]` param_id ; `[2..5]` value u32 ; `[6..7]` 0 |
| `0x406` | `[0..1]` uid_tag ; `[2]` channel ; `[3]` request_id ; `[4..5]` lookback_ms |
| `0x426` | `[0..1]` uid_tag ; `[2]` state ; `[3]` refused_param_id ; `[4..5]` sweep_period_us ; `[6]` overflows ; `[7]` 0 |
| `0x427` | `[0..1]` uid_tag ; `[2]` channel ; `[3]` request_id ; `[4..6]` min/max 12 bits (min[7:0], min[11:8]\|max[3:0]<<4, max[11:4]) ; `[7]` flags bit0 |
| `0x428` | `[0..1]` uid_tag ; `[2]` seq ; `[3..4]` t_peak_100us ; `[5..6]` t_end_peak_100us ; `[7]` 0 |
| `0x429` | `[0..1]` seq ; `[2]` s0[7:0] ; `[3]` s0[11:8]\|s1[3:0]<<4 ; `[4]` s1[11:4] ; `[5]` s2[7:0] ; `[6]` s2[11:8]\|s3[3:0]<<4 ; `[7]` s3[11:4] |

Refus de valeur à l'encodage comme au décodage : `base`, `peak`, `min`, `max`,
échantillons > 4095 ; `target_uid_tag == 0` interdit pour TRACE/STREAM ;
`channel` hors 0..11 pour TRACE/STREAM ; `lookback_ms == 0` interdit.

## 1.6 `listen_params.h` — contrat

```c
// Jeu canonique des 18 paramètres d'écoute (ADR-0032 §5, + trace_timeout_ms).
// Aucune valeur n'est compilée ici : le Clavier détient k_listen_config et
// envoie le jeu complet avant chaque démarrage. Le Moteur ne garde que la RAM.
#define LISTEN_PARAMS_COUNT 18u
#define LISTEN_PARAMS_WIRE_SIZE (LISTEN_PARAMS_COUNT * 4u)  // 72 octets

typedef enum {
    LISTEN_PARAM_INVALID = 0,
    LISTEN_PARAM_SETTLE_US = 1,
    LISTEN_PARAM_SAMPLES_PER_DWELL = 2,
    LISTEN_PARAM_BASELINE_INIT_MS = 3,
    LISTEN_PARAM_BASELINE_TAU_MS = 4,
    LISTEN_PARAM_RISE_THR = 5,
    LISTEN_PARAM_RETURN_THR = 6,
    LISTEN_PARAM_RETURN_HOLD_US = 7,
    LISTEN_PARAM_SPLIT_PCT = 8,
    LISTEN_PARAM_MIN_PULSE_US = 9,
    LISTEN_PARAM_DRIVE_BLANK_MS = 10,
    LISTEN_PARAM_TRACE_PERIOD_US = 11,
    LISTEN_PARAM_TRACE_TRIG = 12,
    LISTEN_PARAM_TRACE_TIMEOUT_MS = 13,
    LISTEN_PARAM_STREAM_DECIM = 14,
    LISTEN_PARAM_VEL_FIXED = 15,
    LISTEN_PARAM_VEL_MIN_DELTA = 16,
    LISTEN_PARAM_VEL_MAX_DELTA = 17,
    LISTEN_PARAM_DETAIL = 18,
} listen_param_id_t;

typedef struct {
    uint32_t settle_us;          // attente mux après commutation (écoute)
    uint32_t samples_per_dwell;  // conversions moyennées par canal (1..64)
    uint32_t baseline_init_ms;   // fenêtre de base initiale
    uint32_t baseline_tau_ms;    // constante de temps du suivi lent
    uint32_t rise_thr;           // seuil de début (points au-dessus de la base)
    uint32_t return_thr;         // seuil de retour (< rise_thr)
    uint32_t return_hold_us;     // maintien du retour (0 = instantané)
    uint32_t split_pct;          // creux de trille (1..99 %)
    uint32_t min_pulse_us;       // impulsion minimale rapportée
    uint32_t drive_blank_ms;     // masquage après pilotage
    uint32_t trace_period_us;    // période de capture (>= 2)
    uint32_t trace_trig;         // déclenchement de trace
    uint32_t trace_timeout_ms;   // sans déclenchement (>= 1, défaut 10000)
    uint32_t stream_decim;       // un échantillon sur N (>= 1)
    uint32_t vel_fixed;          // vélocité fixe (1..127)
    uint32_t vel_min_delta;      // 0/0 = vélocité fixe
    uint32_t vel_max_delta;
    uint32_t detail;             // 1 = émet LISTEN_PULSE_TIMING
} listen_params_t;

typedef enum {
    LISTEN_PARAMS_OK = 0,
    LISTEN_PARAMS_ERR_ARG,
    LISTEN_PARAMS_ERR_FIELD,   // *bad_id désigne le champ fautif
} listen_params_result_t;

size_t listen_params_field_count(void);                       // 18
const char *listen_params_field_name(uint8_t id);              // "settle_us"...
bool listen_params_field_id(const char *name, uint8_t *id);    // aller-retour noms
bool listen_params_get(const listen_params_t *p, uint8_t id, uint32_t *out);
bool listen_params_set(listen_params_t *p, uint8_t id, uint32_t value);
listen_params_result_t listen_params_validate(const listen_params_t *p, uint8_t *bad_id);
// 18 u32 little-endian dans l'ordre des ids 1..18, puis can_crc16_ccitt().
size_t listen_params_serialize(const listen_params_t *p, uint8_t out[LISTEN_PARAMS_WIRE_SIZE]);
uint16_t listen_params_crc16(const listen_params_t *p);
```

`listen_params_validate()` applique **exactement** (rejet en bloc, jamais
d'écrêtage, `bad_id` = premier champ fautif) :

| Champ | Règle |
|---|---|
| `samples_per_dwell` | 1..64 |
| `split_pct` | 1..99 |
| `trace_period_us` | ≥ 2 |
| `trace_timeout_ms` | ≥ 1 |
| `stream_decim` | ≥ 1 |
| `return_thr` / `rise_thr` | `return_thr < rise_thr` |
| `vel_fixed` | 1..127 |
| `vel_min_delta` / `vel_max_delta` | (0,0) ou (min > 0 et max > 0 et min < max) |
| `detail` | 0 ou 1 |

Les autres champs (dont `settle_us`) sont structurellement libres : ce ne sont
pas des limites. `listen_params_get/set` ne valident que l'id (1..18) ; c'est
`validate` qui juge le jeu complet, côté Clavier avant envoi et côté Moteur à
`LISTEN_CONTROL`.

## 1.7 Tests hôte du Lot 1

`tests/host/test_listen_protocol.c` (modèle : `test_phase3_helpers.c`), compilé
avec `can_protocol.c` + `listen_params.c`, `-std=c11 -Wall -Wextra -Werror -pedantic` :

1. **Aller-retour** des 8 trames, valeurs limites : canal 0/11, `base/peak/min/max`
   0 et 4095, seq 0/255, uid_tag 0/0xFFFF, flags seuls, échantillons 0/4095.
2. **DLC** : pour chaque trame, `dlc-1` et `dlc+1` → `CAN_PROTO_BAD_DLC` ; NULL → `BAD_ARG`.
3. **Valeurs** : canal 12, mode 9, état 9, base 4096, `min > max` → `BAD_VALUE`.
4. **Réservés** : octet réservé non nul décodé sans erreur ; encodage émet 0.
5. **Packing 12 bits** : un vecteur par trame qui vérifie les octets exacts du
   tableau §1.5 (pas seulement l'aller-retour).
6. **`listen_params`** : 18 noms uniques et aller-retour nom↔id ; ids 1..18 sans trou ;
   `set/get` de chaque champ ; 9 cas `validate` (un par règle, plus un jeu valide) ;
   `serialize` = 72 octets, little-endian, et `crc16` change si un seul champ change ;
   **un vecteur CRC doré** (valeur figée dans le test, calculée une fois avec le jeu
   de `k_listen_config`, garantit la stabilité du format).

Inscription dans `run_host_tests.ps1` (après le bloc `test_combo`, `:100`) :

```powershell
# Protocole d'écoute ADR-0032 + paramètres canoniques.
$exe8 = Join-Path $out "test_listen_protocol.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") `
    (Join-Path $PSScriptRoot "test_listen_protocol.c") `
    (Join-Path $root "shared\protocol\can_protocol.c") `
    (Join-Path $root "shared\protocol\listen_params.c") `
    -o $exe8
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (listen protocol) en echec" }
& $exe8
if ($LASTEXITCODE -ne 0) { throw "Tests hote (listen protocol) en echec" }
```

## 1.8 Commandes Lot 1

```powershell
powershell -File tests\host\run_host_tests.ps1
cmake --build "build/moteur"
cmake --build "build/clavier"
```

---

# Lot 2 — Moteur : acquisition Core 1, détecteur, anneaux, CAN

## 2.1 Fichiers

| Fichier | Action | Ancrage / raison |
|---|---|---|
| `firmware/moteur/src/pulse_detector.h/.c` | **créer** | module pur, zéro SDK, testable PC |
| `firmware/moteur/src/listen_noise_ring.h/.c` | **créer** | anneau bruit 1 ms × 256, pur et testable |
| `firmware/moteur/src/ipropi_mux.h/.c` | **créer** | extraire `g_channel_to_mux_input`, `mux_select_channel`, `mux_enable/disable` de `ipropi_selftest.c:65-99` pour ne pas dupliquer la carte du mux |
| `firmware/moteur/src/ipropi_selftest.c` | modifier | utiliser `ipropi_mux` (`:65-99`, `:193-210`) ; **aucune** valeur d'écoute |
| `firmware/moteur/src/listen_core1.h/.c` | **créer** | boucle Core 1, anneaux SPSC, trace, stream, parcage |
| `firmware/moteur/src/can_link.h` | modifier | stats + API écoute (`:11-61`) |
| `firmware/moteur/src/can_link.c` | modifier | RX `:580-653`, init/poll `:655-752`, TX `:111-123` |
| `firmware/moteur/src/main.c` | modifier | init `:63-116`, boucle `:116-271` (masque piloté `:182-184`, poll `:251`) |
| `firmware/moteur/CMakeLists.txt` | modifier | sources `:41-53`, options `:38-39`, liens `:93` |
| `tests/host/test_pulse_detector.c` | **créer** | cas IMP-002 |
| `tests/host/test_listen_noise_ring.c` | **créer** | tranches, wrap, troncature |
| `tests/host/run_host_tests.ps1` | modifier | deux blocs de plus |

## 2.2 `pulse_detector.h` — contrat et état O(1)

```c
// Détecteur d'impulsion IPROPI, une instance par canal. Aucune allocation,
// aucune dépendance SDK : `now_us` est fourni par l'appelant.
typedef enum {
    PULSE_STATE_INIT = 0,      // fenêtre baseline_init_ms en cours
    PULSE_STATE_IDLE = 1,      // calme, base suivie
    PULSE_STATE_RISE = 2,      // au-dessus de base + rise_thr
    PULSE_STATE_RETURN_HOLD = 3,  // retour sous base + return_thr, maintien en cours
} pulse_state_t;

typedef struct {
    uint16_t base;      // ligne de base au début de l'impulsion
    uint16_t peak;      // maximum
    uint16_t delta;     // peak - base, saturant à 0xFFFF
    uint32_t t_peak_us; // t_peak - t0
    uint32_t t_end_us;  // t_end - t0 (t_end - t_peak = t_end_us - t_peak_us)
    bool split;         // fin par creux de trille
} pulse_event_t;

typedef struct {
    listen_params_t cfg;       // copie à l'entrée en écoute, jamais relue
    pulse_state_t state;
    uint32_t baseline_q16;     // base en Q16.16
    uint32_t init_sum;
    uint32_t init_count;
    uint32_t init_end_us;
    uint16_t last_sample;
    uint16_t base_at_start;
    uint16_t peak;
    uint16_t trough;
    uint32_t last_calm_us;     // dernier échantillon <= base + return_thr
    uint32_t t0_us;
    uint32_t t_peak_us;
    uint32_t t_trough_us;
    uint32_t return_candidate_us;
    uint32_t rejected;         // impulsions < min_pulse_us
} pulse_detector_t;

void pulse_detector_init(pulse_detector_t *pd, const listen_params_t *params, uint32_t now_us);
void pulse_detector_reset(pulse_detector_t *pd, uint32_t now_us);  // nouvelle écoute
// Retourne true si `out` reçoit une impulsion complète et non rejetée.
bool pulse_detector_feed(pulse_detector_t *pd, uint16_t sample, uint32_t now_us,
                         pulse_event_t *out);
uint16_t pulse_detector_baseline(const pulse_detector_t *pd);
```

Algorithme exact (à commenter dans le `.c`) :

1. `INIT` : accumuler les échantillons jusqu'à `now_us >= init_end_us` ; puis
   `baseline_q16 = (init_sum / init_count) << 16`, `state = IDLE`. Aucune
   impulsion n'est rapportée pendant l'init. `baseline_init_ms = 0` → base = premier
   échantillon.
2. `IDLE` : si `sample <= base + return_thr`, mémoriser `last_calm_us = now_us` ;
   **suivi lent uniquement en IDLE** :
   `baseline_q16 += ((sample << 16) - baseline_q16) * dt_us / (baseline_tau_ms * 1000)`.
   Si `sample > base + rise_thr` : `base_at_start = base`, `t0_us = last_calm_us`,
   `peak = trough = sample`, `t_peak_us = now_us`, `state = RISE`.
3. `RISE` : mettre à jour `peak`/`t_peak_us`. Si après la crête
   `sample < base + split_pct * (peak - base) / 100`, armer le creux
   (`trough = sample`, `t_trough_us = now_us`) ; une remontée ultérieure de plus
   de `rise_thr` au-dessus du creux termine l'impulsion **au creux** (`t_end =
   t_trough_us`, `split = true`) et ouvre immédiatement la suivante (`t0 = t_trough_us`,
   `base_at_start = base`, `peak = trough = sample`, `t_peak = now_us`).
   Si `sample <= base + return_thr` : `return_candidate_us = now_us` ;
   `return_hold_us == 0` → fin immédiate à `now_us`, sinon `state = RETURN_HOLD`.
4. `RETURN_HOLD` : si `sample > base + return_thr`, revenir à `RISE` (même
   impulsion, crête mise à jour). Sinon si `now_us - return_candidate_us >=
   return_hold_us`, fin **à `return_candidate_us`**.
5. Fin : `duration = t_end_us - t0_us` ; si `duration < min_pulse_us`, incrémenter
   `rejected` et ne rien rapporter ; sinon remplir `base/peak/delta/t_peak_us/
   t_end_us/split`, puis `state = IDLE` (la base ne saute pas : l'EMA reprend au
   prochain échantillon calme).

Seuils recalculés à chaque échantillon depuis `base` (Q16.16 → u16 arrondi), pas
figés à l'entrée : une dérive lente reste sans fausse impulsion.

## 2.3 `listen_noise_ring.h` — anneau bruit

```c
#define LISTEN_NOISE_SLICES 256u
// Une tranche de 1 ms par canal : min/max empaquetés dans UN u32
// (min << 16 | max), accès atomique 32 bits entre cœurs, pas de lecture déchirée.
typedef struct {
    uint32_t slice[LISTEN_NOISE_SLICES];  // 0xFFFFFFFF = tranche vide
    uint32_t last_ms;
} listen_noise_ring_t;                    // 1024 o par canal -> 12 288 o pour 12

void listen_noise_ring_reset(listen_noise_ring_t *ring, uint32_t now_ms);
void listen_noise_ring_record(listen_noise_ring_t *ring, uint32_t now_ms, uint16_t sample);
// `lookback_ms` > 256 -> *truncated = true et fenêtre limitée à 256 ms.
bool listen_noise_ring_query(const listen_noise_ring_t *ring, uint32_t now_ms,
                             uint16_t lookback_ms, uint16_t *min, uint16_t *max,
                             bool *truncated, uint16_t *slices_valid);
```

`record` : si `now_ms != last_ms`, avancer d'une tranche, l'initialiser à
`0xFFFF/0x0000` (min=0xFFFF, max=0) puis écrire. Si plusieurs ms ont sauté (trace,
parcage), les tranches intermédiaires restent `0xFFFFFFFF` et ne comptent pas
dans `slices_valid`. `query` balaie les `lookback_ms` dernières tranches non vides
et renvoie `base` par `pulse_detector_baseline()` côté appelant.

## 2.4 `listen_core1.h` — API entre cœurs

```c
#define LISTEN_PULSE_RING_SIZE 64u    // puissance de 2
#define LISTEN_STATUS_RING_SIZE 8u
#define LISTEN_STREAM_RING_SIZE 256u  // échantillons
#define LISTEN_TRACE_PRE_TRIGGER 1024u // 25 % de 4096

typedef struct {  // publié par Core 1, consommé par Core 0
    uint8_t channel;
    uint8_t seq;
    uint16_t base;
    uint16_t peak;
    uint32_t t_peak_us;
    uint32_t t_end_us;
    bool split;
} listen_pulse_evt_t;

typedef struct {
    can_listen_state_t state;
    uint8_t refused_param_id;
    uint16_t sweep_period_us;
    uint8_t overflows;
} listen_status_evt_t;

void listen_core1_init(const uint8_t uid[CAN_UID_SIZE], uint16_t uid_tag,
                       solenoid_driver_t *drivers, uint8_t driver_count);
void listen_core1_launch(void);                 // multicore_launch_core1()
bool listen_core1_active(void);                 // mode != OFF
void listen_core1_apply_params(const listen_params_t *params, uint16_t crc); // OFF seulement
bool listen_core1_set_mode(can_listen_mode_t mode, uint8_t channel, uint16_t target_uid_tag);
void listen_core1_set_drive_mask(uint16_t non_idle_mask);  // Core 0, chaque boucle
bool listen_core1_wait_parked(uint32_t timeout_us);        // après OFF
bool listen_core1_take_pulse(listen_pulse_evt_t *out);
bool listen_core1_take_status(listen_status_evt_t *out);
bool listen_core1_take_stream(uint16_t *out);              // échantillon décimé
bool listen_core1_take_noise(uint8_t channel, uint16_t lookback_ms,
                             uint16_t *min, uint16_t *max, bool *truncated);
uint32_t listen_core1_sweep_period_us(void);
uint8_t listen_core1_overflow_count(void);
// Trace : buffer prêt -> Core 0 lit 4096 échantillons puis libère.
bool listen_core1_trace_ready(void);
uint16_t listen_core1_trace_sample(uint32_t index);
void listen_core1_trace_release(void);
```

Partage entre cœurs, règles à commenter dans le `.c` :

* anneaux SPSC : indices `volatile uint32_t head/tail`, producteur écrit la case
  **puis** `__dmb()` **puis** `head` ; consommateur lit `head`, `__dmb()`, la case,
  puis avance `tail` (même schéma que `can_link.c:81-98,704-724`) ;
* configuration (`listen_params_t`, CRC) écrite par Core 0 **uniquement mode OFF**
  et lue par Core 1 après le drapeau de mode ; `__dmb()` avant le changement de mode ;
* `g_mode` et `g_drive_mask` : `volatile`, un seul écrivain (Core 0) ;
* débordement d'anneau = `++overflows` saturant 255 + drapeau STATUS, jamais
  silencieux ; l'impulsion perdue n'est pas inventée.

## 2.5 Boucle Core 1 (séquence)

```
listen_core1_main():
  init anneaux/détecteurs, parked = true, mode = OFF
  boucle infinie :
    if mode == OFF :
        mux_disable(); parked = true; publier STATUS IDLE (1 fois/s max) ; __wfe()/continue
    parked = false
    if !solenoid_global_sleep_actif() : publier STATUS UNAVAILABLE_SLEEP ; attendre
    if un canal en FAULT : publier STATUS FAULT (une fois par transition)
    mux_enable()
    if mode == TRACE : capture_trace() ; puis attendre trace_release ; mode = LISTEN
    else :
        balayage des 12 canaux dans l'ordre 0..11 :
          mux_select(channel) ; busy_wait(settle_us)
          somme = samples_per_dwell conversions ADC (2 µs chacune)
          échantillon = (somme + N/2) / N
          si canal masqué (drive_blank_ms après sortie du masque) :
              ++samples_masked ; pas de détecteur, pas de bruit
          sinon :
              noise_ring[channel].record(now_ms, sample)
              if mode == STREAM && channel == cible && (++n % stream_decim == 0)
                  pousser dans l'anneau stream
              if pulse_detector_feed(&pd[channel], sample, now_us, &evt)
                  pousser dans l'anneau pulse (seq++)
        mesurer la période du balayage (now_us fin - début), publier STATUS 1/s
```

* `mux_enable()` = `gpio_put(MOTEUR_MUX_EN_GPIO, false)` ; `mux_disable()` = `true`
  (même convention que `ipropi_selftest.c:89-91`).
* `trace` : mux fixe sur le canal cible, période `max(trace_period_us, 2)` ;
  tampon de 4096 ; pré-déclenchement 1024 ; déclenchement au premier échantillon
  `> base + trace_trig` ; sans déclenchement à `trace_timeout_ms` → STATUS
  `TRACE_TIMEOUT` (publier une fois) et retour au balayage sans envoyer de trace.
  Aucune écriture de log ni de CAN sur Core 1.
* `stream` : un échantillon sur `stream_decim` du canal cible, pendant le balayage
  normal ; si l'anneau est plein, jeter l'échantillon et compter le débordement.
* Canal piloté : `drive_blank_ms` court à partir du moment où le canal **sort** du
  masque `non_idle_mask` ; échantillons invalides pendant ce délai, comptés
  `samples_masked` (compteur STAT UT non exposé en trame, seulement journalisé
  côté Moteur si `MOTEUR_DIAG_TRACE` — le Moteur n'a pas de console).

## 2.6 `can_link.c` / `.h` (Moteur)

Ajouts d'API :

```c
void can_link_listen_set_drive_mask(uint16_t non_idle_mask);  // relayé Core 1
bool can_link_listen_is_active(void);                          // pour refuser 0x401
```

RX — dans `handle_frame()` (`can_link.c:590-652`) :

| ID | Action |
|---|---|
| `0x405` `LISTEN_PARAM` | si mode != OFF → STATUS `IDLE`? **non** : refus par `refused_param_id` et aucune application. Si id inconnu → STATUS `refused_param_id` + `PARAM_MISMATCH`. Sinon `listen_params_set()` en RAM d'attente |
| `0x404` `LISTEN_CONTROL` | OFF : `listen_core1_set_mode(OFF)` puis `wait_parked(10 ms)` ; LISTEN/TRACE/STREAM : si un auto-test est actif → STATUS `BUSY_SELFTEST` ; sinon valider le jeu reçu (`validate` + CRC == `param_crc16`) sinon STATUS `PARAM_MISMATCH` ; puis `apply_params` + `set_mode` |
| `0x406` `LISTEN_NOISE_QUERY` | seulement si écoute active et `target_uid_tag == board_tag` ; répondre `0x427` avec la fenêtre demandée |
| `0x401` `IPROPI_SELFTEST_REQUEST` | si `listen_core1_active()` → **refuser** `REFUSED_BUSY` (aucune suspension, §11) ; sinon chemin existant `:607-625` |
| `0x110`, `0x426`–`0x429` | ajouter à la liste « IDs ignorés » `:626-636` (le Moteur ne les reçoit pas) |

TX — fonctions internes + pompe (appelées depuis `can_link_poll`, `:704-752`) :

```c
static void listen_tx_poll(uint32_t now_us);   // vide les anneaux vers le CAN
```

* `0x110` : pour chaque `listen_pulse_evt_t` dépopé, encoder et envoyer ; si
  `params.detail != 0`, envoyer aussitôt `0x428` avec le **même seq** (durées µs
  → `(us + 99) / 100`, saturation `0xFFFF` et bit `saturated` dans `0x110`).
* `0x426` : sur changement d'état, refus, ou 1×/s ; `sweep_period_us` saturant
  `0xFFFF`, `overflows` saturant 255.
* `0x429` : une trame toutes les **250 µs** (1024 trames ≈ 256 ms, ADR « ≈0,3 s ») ;
  en TRACE, 1024 trames puis `trace_release()` ; en STREAM, dès 4 échantillons
  disponibles, en respectant le même espacement. Si `can2040_transmit` échoue :
  `++stats.tx_fail`, compter le débordement, ne pas réémettre en boucle.
* `uid_tag` est ajouté par Core 0 dans `0x110/0x426/0x427/0x428` ; `0x429` n'en a pas.

Init (`can_link_init`, `:655-702`) : après `ipropi_selftest_init`, mémoriser
`board_tag`, puis `listen_core1_init(...)` ; **ne pas** lancer le Core 1 ici
(voir `main.c`). `listen_core1_launch()` a lieu après `can_link_init`, l'ADC et le
mux étant déjà initialisés par `ipropi_selftest_init` (`:193-210`).

## 2.7 `main.c` (Moteur)

* après `can_link_init(...)` (`:90`) : `listen_core1_launch();`
* dans la boucle, avant `can_link_poll(now_us)` (`:251`) : calculer le masque des
  canaux non IDLE (même test que `all_channels_idle`, `:54-61`, mais par canal) et
  appeler `can_link_listen_set_drive_mask(mask)` ;
* le bouton Moteur reste inchangé ; un appui long pendant l'écoute produit un
  `0x401` refusé côté Clavier et, côté Moteur, `REFUSED_BUSY` si la trame arrive.
* `printf` Moteur : le Moteur n'a **pas** de console (`pico_enable_stdio_uart(moteur 0)`,
  `CMakeLists.txt:95`) ; ne pas ajouter de journal Core 1.

## 2.8 CMake Moteur

```cmake
# sources (:41-53)
    src/ipropi_mux.c
    src/listen_core1.c
    src/listen_noise_ring.c
    src/pulse_detector.c
    ${CMAKE_CURRENT_LIST_DIR}/../../shared/protocol/listen_params.c

# liens (:93) : ajouter pico_multicore
target_link_libraries(moteur PRIVATE pico_stdlib hardware_gpio hardware_pwm hardware_irq
    hardware_sync hardware_clocks hardware_adc hardware_flash pico_flash pico_unique_id
    pico_multicore can2040)
```

Aucun nouveau `#define` de valeur d'écoute dans le firmware Moteur (invariant 4 et
ADR §5) : seules les tailles de tampons sont compilées.

## 2.9 Tests hôte du Lot 2

`test_pulse_detector.c` (paramètres de `k_listen_config` en dur dans le test,
`settle_us` sans effet) :

| Cas | Attendu |
|---|---|
| appui isolé | 1 événement, `base`/`peak`/`delta` exacts, `t_peak`/`t_end` cohérents |
| appui + relâchement (2 impulsions séparées) | 2 événements, pas de fusion |
| trille, creux bref à 25 % de (crête−base) | 2 événements, 1er `split=true`, `t_end` = creux |
| bruit ±3 autour de la base (±1 sur un échantillon) | 0 événement, base stable |
| impulsion < `min_pulse_us` | 0 événement, `rejected == 1` |
| dérive de base lente (+20 points/s) | 0 événement, base suit à ±2 |
| `return_hold_us = 0` | fin immédiate au premier retour |
| `baseline_init_ms` | aucune détection pendant l'init, base = moyenne |
| saturation de durée | `t_end_us` correct (le test vérifie la valeur µs, pas la trame) |

`test_listen_noise_ring.c` : tranche vide, une ms = min/max corrects, wrap 256,
saut de ms, `lookback_ms = 1/256`, `257` → `truncated`, `slices_valid` exact.

Inscription dans `run_host_tests.ps1` : deux blocs `gcc` (sources du module + test),
sans SDK, `-I firmware\moteur\src -I shared\protocol`.

## 2.10 Commandes Lot 2

```powershell
powershell -File tests\host\run_host_tests.ps1
cmake -S "firmware/moteur" -B "build/moteur" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
cmake --build "build/moteur"
```

Si `build/moteur` existe déjà, le reconfigurer (nouvelles sources) ; en cas de
cache incohérent, supprimer `build/moteur` et rejouer les deux commandes
(`tests/build_clavier_windows.md:181-196`).

---

# Lot 3 — Clavier : mode REC, console `e`, Serial Studio

## 3.1 Fichiers

| Fichier | Action | Ancrage / raison |
|---|---|---|
| `firmware/clavier/src/listen_config.h` | **créer** | `k_listen_config`, modèle `solenoid_profile_config.h:29-82` |
| `firmware/clavier/src/rec_mode.h/.c` | **créer** | logique pure : états, refus, alternance, fusion, voisins, vélocité, noms de notes, résumé de trace |
| `firmware/clavier/src/keycode.h` | modifier | kinds `:7-30`, macros `:108-132` |
| `firmware/clavier/src/keycode.c` | modifier | `keycode_press` `:136-186`, `keycode_name` `:361-441` |
| `firmware/clavier/src/main.c` | modifier | état `:21-45`, maintenance `:159-241`, note `:470-491`, boucle `:685-719` |
| `firmware/clavier/src/can_link.h/.c` | modifier | API écoute ; RX `:2512-2620`, poll `:2772`, TX `:3025-3076`, getters `:3078-3084` |
| `firmware/clavier/src/bringup_console.c` | modifier | aide `:25-29`, dispatch `:218-293` |
| `firmware/clavier/src/usb_log.h/.c` | modifier | port 2 (ring, task) |
| `firmware/clavier/src/tusb_config.h` | modifier | `CFG_TUD_CDC 2` conditionnel `:13-19` |
| `firmware/clavier/src/usb_descriptors.c` | modifier | interfaces/endpoints/chaînes `:3-39,50-56` |
| `firmware/clavier/CMakeLists.txt` | modifier | option `:17-22`, sources `:38-57`, defs `:83-98` |
| `tools/serial_studio/clavicore_rec.json` | **créer** | projet Quick Plot (dossier `tools/` à créer) |
| `tests/host/test_rec_mode.c` | **créer** | alternance, fusion, refus, voisins, vélocité, noms |
| `tests/host/test_console_tuning.c` | modifier | doublures des nouvelles API `e` |
| `tests/host/run_host_tests.ps1` | modifier | bloc `test_rec_mode.exe` |

## 3.2 `listen_config.h`

```c
// SEUL FICHIER À MODIFIER POUR RÉGLER L'ÉCOUTE (ADR-0032 §5).
// Ces valeurs sont diffusées au Clavier avant chaque démarrage en écoute ;
// aucune n'est une limite électrique et le Moteur n'en compile aucune.
// Procédure de réglage : e show / e set / e diff, puis recopier ici.
#ifndef CLAVIER_LISTEN_CONFIG_H
#define CLAVIER_LISTEN_CONFIG_H

#include "listen_params.h"

static const listen_params_t k_listen_config = {
    .settle_us = 15u,              // défaut ADR
    .samples_per_dwell = 2u,
    .baseline_init_ms = 200u,
    .baseline_tau_ms = 500u,
    .rise_thr = 5u,
    .return_thr = 3u,
    .return_hold_us = 1000u,
    .split_pct = 30u,
    .min_pulse_us = 500u,
    .drive_blank_ms = 50u,
    .trace_period_us = 20u,
    .trace_trig = 5u,
    .trace_timeout_ms = 10000u,    // §11
    .stream_decim = 1u,
    .vel_fixed = 100u,
    .vel_min_delta = 0u,
    .vel_max_delta = 0u,
    .detail = 1u,
};

#endif
```

Au boot : `listen_params_validate(&k_listen_config, &id)` ; si refus, journal
`[REC] config invalide champ=<nom>` (niveau ALWAYS) et REC reste indisponible.

## 3.3 `rec_mode.h` — structures

```c
typedef enum { REC_MODE_OFF = 0, REC_MODE_SOLE = 1, REC_MODE_MUTE = 2 } rec_mode_t;
typedef enum {
    REC_OK = 0,
    REC_NO_TABLE,        // table de commissioning absente/invalide
    REC_SELFTEST,        // auto-test en cours
    REC_PROFILE_SYNC,    // synchronisation de profil en cours
    REC_MAINTENANCE,     // maintenance active
} rec_refusal_t;

#define REC_OFF_LIST_MAX 128u
typedef struct { uint8_t count; uint8_t note[REC_OFF_LIST_MAX]; } rec_off_list_t;

typedef struct {
    bool emit_on;        // Note On à émettre
    bool emit_off;       // Note Off à émettre
    uint8_t midi_note;
    uint8_t velocity;
} rec_action_t;

typedef struct {
    uint8_t matrix_ref[128];    // positions matrice tenant la note (compteur)
    uint8_t captured_ref[128];  // 0/1 : note captée active
    uint8_t key_down[COMMISSIONING_KEY_COUNT + 1u]; // alternance par touche 1..49
    bool noise_enabled;         // e bruit
    // Résumé de trace, alimenté par les 1024 trames LISTEN_TRACE :
    uint16_t trace_min, trace_max, trace_first, trace_last;
    uint32_t trace_count, trace_peak_index;
} rec_state_t;

// Entrée/sortie : remplissent offs avec les Note Off à émettre (bornes 128).
rec_refusal_t rec_mode_enter(rec_state_t *st, rec_mode_t mode,
                             bool table_valid, bool selftest_active,
                             bool profile_sync_active, bool maintenance,
                             rec_off_list_t *offs);
void rec_mode_exit(rec_state_t *st, rec_off_list_t *offs);
void rec_mode_raz(rec_state_t *st);   // e raz : alternance seule, aucun Note Off

// Événement matrice : met à jour matrix_ref, retourne l'action MIDI à émettre.
// En REC, l'appelant n'envoie JAMAIS de 0x100 ; REC_MUTE supprime l'émission.
rec_action_t rec_mode_matrix_event(rec_state_t *st, rec_mode_t mode,
                                   uint8_t midi_note, bool pressed);
// Impulsion captée : bascule key_down[k], applique la fusion par compteur.
rec_action_t rec_mode_captured_event(rec_state_t *st, rec_mode_t mode,
                                     uint8_t key, uint16_t delta);

bool rec_mode_lookup_uid_channel(const commissioning_runtime_t *runtime,
                                 uint16_t uid_tag, uint8_t channel, uint8_t *key);
bool rec_mode_lookup_neighbor(const commissioning_runtime_t *runtime, uint8_t key,
                              int8_t delta, commissioning_note_target_t *out);
uint8_t rec_mode_velocity(const listen_params_t *params, uint16_t delta);
bool rec_mode_parse_note(const char *text, uint8_t *midi_note);  // "MI3" ou "52"
```

Sémantique à commenter et tester :

* **fusion** : `total = matrix_ref[n] + captured_ref[n]` ; `rec_action` n'émet
  `emit_on` qu'au passage 0 → 1 et `emit_off` qu'au passage 1 → 0. Les événements
  sont traités séparément par source (`matrix_ref` par note, `captured_ref` 0/1) :
  une note tenue par la matrice puis captée ne produit pas de doublon.
* **REC_SOLE** : matrice = MIDI conservé (USB via `midi_note_on`), solénoïdes
  muets (aucun `can_link_note_on`) ; captées = MIDI.
* **REC_MUTE** : matrice muette (aucune émission), captées = MIDI.
* **entrée** : `matrix_ref[]` remis à 0 **après** avoir rempli `offs` (notes
  matrice actives) si `REC_MUTE` ; `captured_ref[]` à 0 ; `key_down[]` à 0
  (toutes touches relevées, §8) ; les `offs` sont émis par l'appelant.
* **sortie** : `offs` = toutes les notes `captured_ref > 0`, puis `captured_ref`
  remis à 0 ; `matrix_ref` n'est pas réémis (une note physique encore tenue ne
  rejoue pas).
* **vélocité** : `vel_fixed` si `vel_min_delta == 0` ; sinon
  `v = 1 + (delta - vel_min_delta) * 126 / (vel_max_delta - vel_min_delta)`,
  bornée 1..127 (borne MIDI). `delta` ≤ min → 1, ≥ max → 127.
* **voisins** : `rec_mode_lookup_neighbor` cherche `key ± 1` (1..49) dans
  `runtime->notes[]` (index `key - COMMISSIONING_KEY_MIN`, `commissioning_table.c:219`) ;
  renvoie false si non attribuée → `[BRUIT] k±1 : non attribuée`.
* **note littérale** : `commissioning_key_to_midi_note(key)` (key + 35), sans
  transposition, armure ni altération ; même canal MIDI que la matrice (canal 0).
* **résumé trace** : `base` = minimum des 1024 échantillons de pré-déclenchement,
  `pic` = maximum global, `delta = pic - base`, `bruit_cc = max - min` du
  pré-déclenchement, `t_pic = index_du_max * trace_period_us / 1000`.

## 3.4 `can_link` (Clavier)

Nouvelles fonctions publiques (`can_link.h`) :

```c
bool can_link_profile_sync_active(void);
// Envoie le jeu (18 x 0x405) puis LISTEN_CONTROL avec le CRC du jeu.
bool can_link_listen_send_params(const listen_params_t *params);
bool can_link_listen_start(const listen_params_t *params, can_listen_mode_t mode,
                           uint16_t target_uid_tag, uint8_t channel);
bool can_link_listen_stop(void);
bool can_link_listen_trace(const listen_params_t *params, uint16_t uid_tag, uint8_t channel);
bool can_link_listen_stream(const listen_params_t *params, uint16_t uid_tag, uint8_t channel);
bool can_link_listen_stream_off(void);
bool can_link_listen_noise_query(uint16_t uid_tag, uint8_t channel, uint8_t request_id,
                                 uint16_t lookback_ms);

typedef enum {
    CAN_LINK_LISTEN_PULSE = 0,
    CAN_LINK_LISTEN_STATUS,
    CAN_LINK_LISTEN_NOISE,
    CAN_LINK_LISTEN_TRACE_FRAME,
} can_link_listen_event_kind_t;

typedef struct {
    can_link_listen_event_kind_t kind;
    union {
        can_listen_pulse_t pulse;
        can_listen_status_t status;
        can_listen_noise_report_t noise;
        can_listen_trace_t trace;
    } u;
    uint16_t source_uid_tag;  // attribué à la réception (0x429 : cible courante)
} can_link_listen_event_t;

bool can_link_listen_take_event(can_link_listen_event_t *out);
```

RX — `handle_frame` (`can_link.c:2526`) : ajouter `0x110`, `0x426`, `0x427`,
`0x428`, `0x429` et les pousser dans une file bornée (64 événements, débordement
compté dans `stats.events_dropped`) ; `0x429` reçoit `source_uid_tag` = dernière
cible TRACE/STREAM. Les IDs Clavier TX (`0x404`–`0x406`) restent dans la liste
« ignorés » `:2604-2610`.

TX : `send_frame` (`:111`) est réutilisé ; `can_link_listen_send_params` envoie
les 18 `0x405` à 1 ms d'intervalle (flash Moteur absent, simple marge), puis
`can_link_listen_start` envoie `0x404`. `can_link_listen_stop` envoie `OFF` et
attend le STATUS `IDLE` du Moteur (timeout 20 ms, sinon journal `[LISTEN] arret
sans reponse`, jamais bloquant). `can_link_profile_sync_active()` expose le
`profile_sync_active` statique (`:140`).

## 3.5 `main.c` — intégration REC

1. `clavier_state_t` (`:21-45`) : ajouter `rec_state_t rec; rec_mode_t rec_mode;
   uint8_t rec_params_valid;` et `listen_params_t listen_params;` initialisé avec
   `k_listen_config` au boot (`:685-697`), puis validé.
2. Keycodes : dans `dispatch_keycode`, après `KEYCODE_MAINTENANCE_TOGGLE`
   (`:506-512`), ajouter `KEYCODE_REC_SOLE` / `KEYCODE_REC_MUTE` à l'appui :
   * hors REC : `rec_mode_enter(...)` avec les 4 refus ; succès → journal
     `[REC] ON SOLE|MUTE`, envoi des `offs`, `can_link_listen_start(...,
     CAN_LISTEN_MODE_LISTEN, 0, 0)`, `rec_mode = SOLE|MUTE` ;
   * en REC : `rec_mode_exit`, envoi des `offs`, `can_link_listen_stop()`,
     `[REC] OFF` (l'une ou l'autre touche sort, §7).
   Si l'entrée est refusée : `[REC] refus <raison>` et aucun envoi.
3. Maintenance : dans `handle_maintenance_toggle` (`:159`), refuser en REC
   (`[REC] refus MAINTENANCE`) ; `KEYCODE_GET_UID`, transpositions et
   `UNTRANSPOSE` restent actifs en REC (§7).
4. Notes matrice (`:470-491`) : remplacer les appels directs par
   `rec_action_t a = rec_mode_matrix_event(...)` ; si `a.emit_on/off` :
   `midi_note_on/off` toujours, `can_link_note_on/off` **seulement hors REC**
   (aucune trame `0x100` en REC). `state.last_note_event_ms` mis à jour comme avant.
5. Boucle (`:699-719`) : avant `can_link_poll()`, drainer
   `can_link_listen_take_event()` (borne 16 événements/tour) vers :
   * `LISTEN_PULSE` : lookup (uid, canal) → touche ; si non attribuée,
     `[REC] canal non attribue` ; sinon `rec_mode_captured_event()` →
     `midi_note_on/off` (jamais CAN) ; journal `[LISTEN] pos= ch= touche= min=
     max= delta= ...` (POSITION via `runtime.notes[key-1].position`) ;
   * `LISTEN_PULSE_TIMING` : si `rec.noise_enabled`, calculer
     `lookback = (t_peak + t_end_peak) * 100 µs + 2 ms` (borné 65535), puis deux
     `can_link_listen_noise_query()` pour les touches k−1/k+1 attribuées ;
   * `LISTEN_STATUS` : `[LISTEN] pos=<uid> etat=<nom> periode=<us>us
     debordements=<n>` au plus 1×/s par carte, immédiat sur changement ;
   * `LISTEN_NOISE_REPORT` : `[BRUIT] k-1|k+1 pos= ch= touche= min= max= base=`
     et `[BRUIT] ... TRONQUE` si drapeau ;
   * `LISTEN_TRACE` : accumuler le résumé (3.3) ; si option ON, écrire les 4
     points bruts sur le port 2.
6. `e trace` : sur réception des 1024 trames, journal `[TRACE] pos= ch= touche=
   base= pic= delta= bruit_cc= t_pic= ms n=4096` (PuTTY, jamais les valeurs),
   puis retour à LISTEN si REC actif, sinon `can_link_listen_stop()`.
7. Sortie du mode : si le port 2 est ouvert, écrire `/*-1,0*/\n` avant une trace.

## 3.6 Keycodes

`keycode.h` : ajouter **en fin** d'énumération (aucune valeur existante ne bouge) :

```c
    // ADR-0032 : bascules du mode REC, ajoutées EN FIN (positions libres,
    // invariant 6 : aucun keymap n'est modifié par cette ADR).
    KEYCODE_REC_SOLE = 16,
    KEYCODE_REC_MUTE = 17,
```

et les macros, après `T_TEMP_OCT_R_DOWN` (`:132`) :

```c
#define REC_SOLE 0x1000u
#define REC_MUTE 0x1100u
```

`keycode.c` : `keycode_press` = aucun effet (comme GET_UID/MAINT) ; `keycode_name`
retourne `"REC_SOLE"` / `"REC_MUTE"`. **Ne placer ni `REC_SOLE` ni `REC_MUTE`
dans `keymap.c`** (invariant 6) ; `docs/specs/keycodes.md` documente seulement
les symboles (doc-aval).

## 3.7 Console `e` (`bringup_console.c`)

Ajouter `case 'e'` dans `execute_line` (`:232-292`) et une famille
`listen_command(text + 1)` ; aide mise à jour dans `print_help` (`:25-29`).

| Entrée | Comportement |
|---|---|
| `e ?`, `e` | aide `[LISTEN] e show\|get\|set\|diff\|reset\|rec\|bruit\|trace\|stream\|raz` |
| `e show` | 18 lignes `[LISTEN] <champ> = <valeur>`, plus `actif=`, `crc=0x%04X`, `detail=` |
| `e get <champ>` | une ligne ; champ inconnu → `[LISTEN] champ inconnu '<x>'` |
| `e set <champ> <v> [...]` | copie, applique toutes les affectations, `validate` ; refus → **rien n'est envoyé**, `[LISTEN] reglage refuse: <champ> (<raison>)` ; succès → `can_link_listen_send_params()`, puis si REC actif : `stop` + `start` (application immédiate) |
| `e diff` | lignes `.champ = valeur,` des champs différents de `k_listen_config` |
| `e reset` | `k_listen_config` puis comme `set` |
| `e rec` | équivalent de `REC_SOLE` (entrée) |
| `e bruit` | bascule `noise_enabled` ; refus si `detail == 0` : `[LISTEN] bruit: necessite detail=1` ; succès `[LISTEN] bruit=on\|off` |
| `e trace <touche>` | résout touche (nom ou MIDI 36..84) ; table invalide → `[REC] refus REC_NO_TABLE` ; envoie TRACE (uid, canal) ; une seule carte |
| `e stream <touche>\|off` | si `CLAVIER_SERIAL_STUDIO=OFF` → `[LISTEN] stream indisponible (CLAVIER_SERIAL_STUDIO=OFF)` ; sinon STREAM / STREAM_OFF |
| `e raz` | `rec_mode_raz()` + `[LISTEN] raz (touches relevees)` |

`e set detail 0` alors que `bruit` est actif : désactiver `bruit` et le dire.
Aucune commande `e` n'écrit en flash ; perte au reboot Clavier, comme `p`.

## 3.8 Option `CLAVIER_SERIAL_STUDIO`

`CMakeLists.txt` (après `:22`) :

```cmake
option(CLAVIER_SERIAL_STUDIO "Second port CDC : flux Serial Studio Quick Plot (ADR-0032)" OFF)
```

et dans le bloc de définitions (`:83-98`) :

```cmake
if(CLAVIER_SERIAL_STUDIO)
    target_compile_definitions(clavier PRIVATE CLAVIER_SERIAL_STUDIO=1)
else()
    target_compile_definitions(clavier PRIVATE CLAVIER_SERIAL_STUDIO=0)
endif()
```

`tusb_config.h` :

```c
#if CLAVIER_SERIAL_STUDIO
#define CFG_TUD_CDC 2   // port 1 = journal/console, port 2 = Serial Studio
#else
#define CFG_TUD_CDC 1
#endif
```

`usb_descriptors.c` : `ITF_NUM_CDC2`, `ITF_NUM_CDC2_DATA`, `EPNUM_CDC2_NOTIF
0x84`, `EPNUM_CDC2_OUT 0x05`, `EPNUM_CDC2_IN 0x85`,
`TUD_CDC_DESCRIPTOR(ITF_NUM_CDC2, 5, ...)`, longueur conditionnelle, chaîne 5
`"Clavicore REC"` (index des chaînes `:50-56`). `CFG_TUD_CDC=1` ⇒ descripteur
inchangé, aucun code mort lié.

`usb_log.h/.c` :

```c
#if CLAVIER_SERIAL_STUDIO
void serial_studio_init(void);
void serial_studio_write(const char *text, size_t length);
void serial_studio_printf(const char *format, ...);  // usage interne Clavier
void serial_studio_task(void);                        // flush, jamais bloquant
#else
static inline void serial_studio_init(void) {}
static inline void serial_studio_write(const char *t, size_t n) { (void)t; (void)n; }
static inline void serial_studio_printf(const char *f, ...) { (void)f; }
static inline void serial_studio_task(void) {}
#endif
```

Anneau 4096 o, `tud_cdc_n_connected(1)` / `tud_cdc_n_write_available(1)` /
`tud_cdc_n_write(1, ...)` / `tud_cdc_n_write_flush(1)` ; débordement compté,
jamais bloquant. `serial_studio_task()` appelé dans la boucle `main.c` à côté de
`usb_log_task()` (`:715`). Le port 1 (journal, console) est **inchangé**.

Formats Quick Plot (écrits sur le port 2 uniquement) :

* flux : `/*brut,base,seuil_haut,seuil_retour,evt*/\n` par échantillon ;
  `base` = dernière base connue de ce canal (celle du dernier `LISTEN_PULSE`, 0
  avant la première) ; `seuil_haut = base + rise_thr` ; `seuil_retour = base +
  return_thr` ; `evt` = delta à la fin d'une impulsion, 0 sinon ;
* trace : `/*-1,0*/\n` puis 4096 lignes `/*index,brut*/\n`.

`tools/serial_studio/clavicore_rec.json` : projet Serial Studio (mode Quick
Plot, délimiteurs `/*` `*/`), deux groupes nommés « REC — flux » (5 jeux :
`brut`, `base`, `seuil_haut`, `seuil_retour`, `evt`) et « REC — trace »
(2 jeux : `index`, `brut`), source série sur le **port COM n°2**, 115200 bauds
(le CDC ignore le débit), sans JavaScript. Le fichier est fourni à titre
d'exemple : toute évolution du format doit rester dans ce document.

## 3.9 Journal (récapitulatif)

* `[LISTEN] pos=2 ch=7 touche=MI3 min=14 max=27 delta=13 m>M=3.4ms M>m=9.1ms
  m>M>m=12.5ms [trille] -> ON|OFF`
* `[BRUIT] k-1 pos=2 ch=8 touche=RE#3 min=13 max=16 base=14` (idem k+1)
  `[BRUIT] ... TRONQUE` si fenêtre > 256 ms ; `[BRUIT] k+1 : non attribuée`
* `[LISTEN] pos=N etat=... periode=...us debordements=...`
* `[TRACE] pos= ch= touche= base= pic= delta= bruit_cc= t_pic= ms n=4096`
* `[REC] ON SOLE|MUTE`, `[REC] OFF`, `[REC] refus <raison>`,
  `[REC] canal non attribue`
* `[LISTEN] config invalide champ=...`, `[LISTEN] arret sans reponse`

## 3.10 Tests hôte du Lot 3

`test_rec_mode.c` (compile `rec_mode.c` + `commissioning_table.c` +
`can_protocol.c` + `listen_params.c`, sans SDK) :

1. alternance : 2 impulsions → ON puis OFF ; 4 impulsions → ON/OFF/ON/OFF.
2. `e raz` au milieu : prochaine impulsion = ON.
3. fusion : matrice ON → captée ON (aucun 2e ON) → matrice OFF (aucun OFF) →
   captée OFF (OFF unique) ; ordre inverse.
4. `REC_MUTE` : entrée remplit `offs` des notes matrice actives, `matrix_ref`
   remis à 0, matrice sans émission ; `REC_SOLE` conserve l'émission matrice.
5. refus : table invalide, auto-test, sync profil, maintenance (un cas chacun).
6. voisins : runtime de deux cartes, k−1 et k+1 sur la bonne carte/canal ; bord
   k=1 et k=49 ; voisin non attribué → false.
7. vélocité : 0/0 → `vel_fixed` ; min/max → 1, 127, milieu, bornes basses/hautes.
8. `rec_mode_parse_note` : `MI3`→52, `DO2`→36, `SI8`→119, `RE#3`→51, `52`→52,
   `128`/`XYZ` → false.
9. résumé trace : 1024 échantillons de test → base/pic/delta/bruit/t_pic exacts.

`test_console_tuning.c` : ajouter les doublures `can_link_listen_*`,
`can_link_profile_sync_active`, `rec_mode_*`, `k_listen_config`, puis tester
`e show`, `e get`, `e set` (refus atomique : aucune valeur changée), `e diff`,
`e bruit` refusé avec `detail=0`, `e stream` refusé avec l'option OFF.

Inscription `run_host_tests.ps1` : bloc `test_rec_mode.exe` (même modèle que
`test_combo`, `:90-101`), avec les `-I` `shared\protocol` + `firmware\clavier\src`.

## 3.11 Commandes Lot 3

```powershell
powershell -File tests\host\run_host_tests.ps1
# Clavier production (option OFF)
cmake -S "firmware/clavier" -B "build/clavier" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2 -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
cmake --build "build/clavier"
# Variante Serial Studio (répertoire séparé, option ON)
cmake -S "firmware/clavier" -B "build/clavier_studio" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2 -DCLAVIER_SERIAL_STUDIO=ON -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
cmake --build "build/clavier_studio"
# Moteur (déjà reconfiguré au Lot 2)
cmake --build "build/moteur"
```

Artefacts : `build/clavier/clavier.uf2`, `build/clavier_studio/clavier.uf2`,
`build/moteur/moteur.elf` (SWD, pas d'UF2 : `tests/build_clavier_windows.md:143-199`).

---

## 4. Validation

* **N1 (chaîne)** : `powershell -File tests\host\run_host_tests.ps1` → 0 échec,
  puis builds Clavier (`pico2`, option OFF **et** ON) et Moteur
  (`moteur_rp2354b`) → **0 erreur / 0 warning**. Le Validateur fournit le nombre
  de vérifications avant/après (référence : 1312 au 2026-11-04,
  `ARCHITECTURE_CURRENT.md:198-202`).
* **N2 (banc, hors chaîne)** : partage Core 0/Core 1 et seuils → campagne de
  `tests/lecture_test_roadmap.md` (M1/M2/M3/G1), rédigée par doc-aval.
* Aucun test ne fige une position de keymap (invariant 6) ; les tests de fusion
  et d'alternance utilisent des touches 1..49 arbitraires.

## 5. Documentation laissée à doc-aval

`docs/specs/CAN.md` (8 trames), `docs/specs/keycodes.md` (`REC_SOLE`,
`REC_MUTE`), `docs/specs/commandes-putty.md` (famille `e`),
`docs/specs/Detail_architecture.md` §mode REC, `ARCHITECTURE_CURRENT.md`, et
`tests/lecture_test_roadmap.md`. Aucun de ces fichiers n'est modifié par les lots.

## 6. Questions ouvertes (non bloquantes, défauts retenus ci-dessus)

1. `stream` n'a pas de champ `base` par échantillon : le tracé utilise la
   dernière base de `LISTEN_PULSE` (0 avant la première). Si le banc exige une
   base continue, il faudra une trame supplémentaire (ADR).
2. `LISTEN_NOISE_QUERY.lookback_ms` est un u16 : une impulsion > 63,5 s est
   écrêtée à 65535 ms ; aucune impulsion de cette durée n'est attendue.
3. `samples_masked` et `pulses_rejected` ne sont pas exposés par une trame (pas
   de champ dans `LISTEN_STATUS`) : visibles seulement par le journal Clavier si
   une trame dédiée est ajoutée plus tard ; non requis par l'ADR.
4. `MOTEUR_DIAG_TRACE=OFF` : les compteurs de débordement restent dans
   `LISTEN_STATUS` (aucune option ne les supprime).
