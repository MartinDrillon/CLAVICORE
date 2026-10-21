# CAN — Phase 3a validée, Phase 3b spécifiée

Ce document synthétise les décisions actées dans
[ADR-0002](../adr/adr-0002-integration-can2040.md),
ADR-0005,
[ADR-0004](../adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) et
[ADR-0009](../adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md).

**État réel** :
- **Phase 3a validée sur bus réel** : premier UID relevé de bout en bout
  `A3FA4C99CA526F4B` (addenda ADR-0003).
- **Phase 3b / Livraison 2** : protocole, table flash, résolution
  note→(UID, canal) et `SOLENOID_COMMAND` implémentés ; les validations
  strictement matérielles restantes sont listées en fin de document.
- **Bring-up / ADR-0009 (spécifié, non encore implémenté)** : table persistée à
  chaque auto-test demandé avec état `FINAL`/`PROVISIONAL`, trame de diagnostic
  `IPROPI_OHM` (`0x601`), PROBE **5 ms** avec fenêtre d'échantillonnage en fin
  d'impulsion, lignes `[MAP]`. Les sections concernées ci-dessous décrivent la
  cible ; le tableau de correspondance des fichiers à revoir est dans
  [`impact-adr-0009.md`](impact-adr-0009.md).

## GPIO CAN par carte

| Carte | GPIO RX | GPIO TX | Plage RP2350 |
|---|---|---|---|
| Clavier | GPIO0 | GPIO1 | 0-31 |
| Moteur | GPIO46 | GPIO47 | 16-47 |

Sur RP2350, `gpio_rx` et `gpio_tx` doivent être tous les deux dans la plage
GPIO0-31 ou tous les deux dans la plage GPIO16-47, sans mélange des deux
plages.

## Terminaison bus

`R24` (valeur `R120`, 120 Ω) est présente mais marquée `dnp` (Do Not
Populate) sur les deux cartes (`Solenoide_drive` et `Clavier_drive_1`). Un
bus CAN fonctionnel exige une terminaison 120 Ω à chaque extrémité physique
du bus : `R24` ne doit être soudée que sur les deux cartes situées aux
extrémités du chaînage physique, jamais sur toutes les cartes.

## Contrat logiciel

- Vendoring de `can2040` en sous-module git, épinglé au tag `v1.7.0`, sous
  `firmware/common/can2040/`.
- Bloc PIO `pio_num = 0` sur les deux cartes.
- IRQ `PIO0_IRQ_0` en gestionnaire exclusif, priorité applicative la plus
  haute disponible.
- Callback `can2040_rx_cb` limité à une copie du message vers un tampon
  circulaire simple producteur-unique/consommateur-unique ; aucun parsing,
  dispatch ou appel à `can2040_transmit()` depuis ce callback.
- Débit CAN cible : 500 kbit/s, défini comme constante nommée.
- File d'émission `can2040_transmit()` limitée à 4 messages maximum
  (`tx_queue[4]`, taille fixe).

## Checklist

- [x] Ajouter le sous-module `can2040` (`firmware/common/can2040/`, tag
      `v1.7.0`, déclaré dans `.gitmodules`)
- [x] Définir `PICO_RP2350` pour les cibles `clavier` et `moteur`
- [x] Écrire `shared/protocol/can_protocol.h` et `.c`
- [x] Valider le bring-up CAN Phase 3a sur bus réel
- [ ] Décider l'ordre physique du chaînage CAN pour placer les 2 résistances
      de terminaison
- [ ] Valider le budget de latence IRQ face au code applicatif existant
      (priorité `PIO0_IRQ_0` vs IRQ USB TinyUSB)

## Bring-up UID (Phase 3a)

- Identifiants CAN (11 bits) :
  - `0x400` `UID_REQUEST` (Clavier → diffusion)
  - `0x410` `BOARD_ANNOUNCE_UID` (trame A, UID 8 octets)
  - `0x411` `BOARD_ANNOUNCE_INFO` (trame B : `uid_tag`, versions,
    `reply_to`)
- Affichage : le Clavier relaie l'UID dans le journal texte USB CDC,
  lisible avec PuTTY sur le port COM du périphérique composite.
- Procédure complète, décodage et erreurs STATUS :
  [`tests/can_phase3_uid.md`](../../tests/can_phase3_uid.md).

## Phase 3b — Auto-test IPROPI et commissioning

### Rappel matériel Moteur

- Mux **74HC4067 = IC17**
- `/MUX_EN` = GPIO42 (`~E`, actif bas)
- Sélection `S0..S3` = GPIO44, GPIO43, GPIO38, GPIO36 (`S0` = bit de poids
  faible)
- Sortie `Z` = `/MUX OUT` = GPIO45 = **ADC5**
- Bouton de test = GPIO34

> Attention : les nets `/SENSx` ne sont **pas** des lignes IPROPI ; ce sont
> les entrées `PH/IN2` des DRV8874. Les mesures IPROPI utiles à l'auto-test
> passent par `/ADC0..11` puis par le mux.

### Mapping canal PWM → entrée du mux

Le bitmap d'occupation est indexé par **canal PWM** (`PWMn`), pas par entrée
du mux.

| Canal | Entrée mux | Canal | Entrée mux |
|---|---|---|---|
| 0 | 9 | 6 | 1 |
| 1 | 10 | 7 | 2 |
| 2 | 11 | 8 | 5 |
| 3 | 12 | 9 | 3 |
| 4 | 13 | 10 | 4 |
| 5 | 14 | 11 | 6 |

Entrées non connectées : `0`, `7`, `8`, `15`.

### Trames CAN Phase 3b

#### `IPROPI_SELFTEST_REQUEST` — ID `0x401`, DLC 2

Émetteur : **Clavier** vers diffusion.

| Octet | Nom | Valeurs / remarques |
|---|---|---|
| 0 | `proto_version` | version du protocole |
| 1 | `request_id` | bits `0..6` = `1..127`, bit `7` = mode calibration |

Règles :
- émise seulement par le Clavier ;
- le Clavier la refuse pendant le jeu et émet alors `STATUS 0x21`
  `SELFTEST_REFUSED_PLAYING` ;
- toutes les cartes Moteur testent et répondent.
- si `bit7 = 1`, chaque carte émet aussi `IPROPI_RAW` (`0x600`) puis
  `IPROPI_OHM` (`0x601`) après la trame C ;
- `reply_to` dans les réponses reprend seulement le `request_id` logique (`1..127`).

#### `IPROPI_SELFTEST_TRIGGER` — ID `0x402`, DLC 2

Émetteur : **Moteur** vers le Clavier, quand le bouton local est maintenu
**3 secondes**.

| Octet | Nom | Valeurs / remarques |
|---|---|---|
| 0 | `proto_version` | version du protocole |
| 1 | `reserved` | **doit être émis à `0`** |

Rôle : demander au Clavier d'orchestrer le test. La carte Moteur ne lance pas
elle-même la diffusion `0x401`.

#### `BOARD_ANNOUNCE_STATUS` — ID `0x412`, DLC 8

Émetteur : **Moteur** vers le Clavier, trame C envoyée en complément des
trames A (`0x410`) et B (`0x411`) lors d'un auto-test demandé.

| Octets | Nom | Valeurs / remarques |
|---|---|---|
| 0..1 | `uid_tag` | CRC16 LE |
| 2..3 | `occupation_bits` | bitmap 12 bits LE, bits `12..15 = 0` |
| 4 | `reply_to` | reprend le `request_id` reçu (`1..127`) |
| 5 | `test_status` | `0` OK, `1` refusé occupé, `2` abandonné, `3` non calibré |
| 6..7 | `fault_bits` | bitmap 12 bits LE, bits `12..15 = 0` |

Règles :
- délai de réponse dérivé de l'UID, identique au mécanisme d'ADR-0003 ;
- le Clavier attend `A+B+C` de toutes les cartes listées dans
  `uid_position.h`, avec timeout constant ;
- un `FAULT` pendant le test force le canal concerné à « non occupé » et pose
  son bit dans `fault_bits`.
- la trame C ne change pas en mode calibration.

#### `IPROPI_RAW` — ID `0x600`, DLC 8

Émetteur : **Moteur** vers le Clavier, **uniquement** quand
`IPROPI_SELFTEST_REQUEST` a été reçu avec `bit7 = 1`.

| Octets | Nom | Valeurs / remarques |
|---|---|---|
| 0..1 | `uid_tag` | CRC16 LE |
| 2 | `reply_to` | `request_id` logique `1..127` |
| 3 | `channel` | `0..11` |
| 4..5 | `idle_avg` | moyenne ADC 12 bits LE, canal mux stabilisé **avant** l'impulsion |
| 6..7 | `probe_avg` | moyenne ADC 12 bits LE des **N derniers échantillons** de l'impulsion (fenêtre en fin d'impulsion, ADR-0009 §5b) |

Règles :
- 12 trames, une par canal `0..11`, après la trame C ;
- même délai initial UID-dérivé que les trames A/B/C, puis émission espacée ;
- `idle_avg` et `probe_avg` sont des valeurs brutes de diagnostic ; la décision
  d'occupation officielle est prise côté Moteur sur
  `delta = max(probe_avg - idle_avg, 0)` ;
- le seuil courant est **figé à 400 ADC** (`delta >= 400` => canal occupé) ;
- `probe_avg` est prélevé en **fin** d'impulsion PROBE (durée 5 ms, duty 20 %) :
  la même valeur sert au critère d'occupation et aux estimations d'`IPROPI_OHM` ;
- le statut protocolaire `UNCALIBRATED` reste réservé pour compatibilité, mais
  n'est pas attendu avec le firmware actuel.

#### `IPROPI_OHM` — ID `0x601`, DLC 8

Émetteur : **Moteur** vers le Clavier, **uniquement** quand
`IPROPI_SELFTEST_REQUEST` a été reçu avec `bit7 = 1`, **après** les trames
`IPROPI_RAW`.

| Octets | Nom | Valeurs / remarques |
|---|---|---|
| 0..1 | `uid_tag` | CRC16 LE |
| 2 | `reply_to` | `request_id` logique `1..127` |
| 3 | `channel` | `0..11` |
| 4..5 | `ohm_milliohm` | résistance estimée, mΩ LE ; `0xFFFF` = invalide |
| 6..7 | `i100_mA` | courant estimé à 100 % de duty, mA LE ; `0xFFFF` = invalide |

Règles :
- 12 trames, une par canal `0..11`, après les 12 trames `IPROPI_RAW`, même
  espacement et même délai initial UID-dérivé ;
- calcul côté **Moteur** (constantes de `solenoid_profile.h`), jamais côté
  Clavier : le Clavier ne duplique aucune constante électrique ;
- entrée du calcul : `delta` (voir `IPROPI_RAW`) et le **duty PROBE réellement
  appliqué** après écrêtage par le pilote ;
- `ohm_milliohm` invalide si `delta < IPROPI_SELFTEST_THRESHOLD_DELTA_ADC`,
  `delta == 0`, ou résultat > 65 534 mΩ ; `i100_mA` invalide si
  `ohm_milliohm` est invalide ; les deux sont clampés à 65 534 sinon ;
- trame **purement informative** : sa perte ou son invalidité ne modifient ni
  `occupation_bits`, ni `fault_bits`, ni l'écriture de la table ;

Formules (constantes de `firmware/moteur/src/solenoid_profile.h`) :

```text
K (V/A)   = SOLENOID_IPROPI_UA_PER_A (450) x SOLENOID_IPROPI_RESISTOR_OHM (2400) / 1e6
R (mOhm)  = (duty_permil x VM_mV x 4095 x K) / (1000 x delta x VREF_mV) - Rds_mOhm
I100 (mA) = VM_mV x 1000 / (R_mOhm + Rds_mOhm)
```

Avec les valeurs nominales (`VM = 24.000 V`, `VREF = 3.300 V`, `K = 1.08 V/A`,
`Rds = 240 mΩ`, duty `200 ‰`) :

```text
R (mOhm) = 32164 x duty_permil / delta - 240
I100 (mA) = 24 000 000 / (R_mOhm + 240)
```

Repères : `delta = 1030` → `6.00 Ω` / `3.85 A` ; `delta = 800` → `7.80 Ω` /
`2.99 A` ; `delta = 400` (seuil) → `15.84 Ω` / `1.49 A`.

#### `SOLENOID_COMMAND` — ID `0x100`, DLC 5

Émetteur : **Clavier** vers une carte Moteur ciblée.

| Octets | Nom | Valeurs / remarques |
|---|---|---|
| 0..1 | `uid_tag` | CRC16 LE de la carte cible |
| 2 | `channel` | `0..11` |
| 3 | `command` | `0 = off`, `1 = note on`, `2 = push on maintenance` |
| 4 | `velocity` | `0..127` |

Règles :
- le Moteur ignore et compte toute trame avec `uid_tag` différent, canal hors
  plage ou canal non occupé selon son dernier bitmap local ;
- au boot, avant tout auto-test réussi, le bitmap local Moteur vaut
  « **tous les canaux gérables autorisés** » ; le filtrage fonctionnel repose
  donc d'abord sur la **table persistante du Clavier** ;
- aucune commande n'est appliquée pendant un auto-test en cours ;
- `command = 2` applique le profil **MAINTENANCE** côté driver Moteur
  (PULL seul, duty 50 %, coupure locale à 10 s, repos maintenance minimal) ;
- pas d'acquittement applicatif en Phase 3b/Livraison 2 ;
- la coupure de sécurité côté driver reste la dernière barrière.

### Format flash de commissioning (Clavier)

* **1 secteur 4 Kio en fin de flash** :
  `offset = PICO_FLASH_SIZE_BYTES - 4096`
* lecture directe **XIP**
* écriture via **`flash_safe_execute`**
* format **version 2** (ADR-0009) ; une table **version 1** reste lisible et est
  interprétée `state = FINAL`, `flags = 0` (une v1 n'a pu être écrite qu'avec
  toutes les cartes présentes)

En-tête (16 octets) :

| Octet | Champ | Type | Remarque |
|---|---|---|---|
| 0..3 | `magic` | `u32` | `'CLVC'` |
| 4..5 | `format_version` | `u16` | `2` |
| 6..7 | `count` | `u16` | nombre d'enregistrements |
| 8..11 | `crc32` | `u32` | CRC32 calculé sur les enregistrements **seulement** |
| 12 | `state` | `u8` | `0 = FINAL`, `1 = PROVISIONAL` |
| 13 | `flags` | `u8` | motifs (tableau ci-dessous) |
| 14..15 | `reserved` | `u16` | `0` |

Les enregistrements commencent à l'octet **16** ; l'enregistrement lui-même est
**inchangé**.

Motifs (`flags`) :

| Bit | Nom | Sens |
|---|---|---|
| `0x01` | `MISSING_BOARD` | une position listée dans `uid_position.h` n'a pas répondu |
| `0x02` | `UNKNOWN_BOARD` | une carte répondante n'est pas dans `uid_position.h` (exclue de la table) |
| `0x04` | `BOARD_NOT_OK` | une carte répondante a `test_status != OK` (entrée conservée, `bitmap = 0`) |
| `0x08` | `FAULT_DURING_TEST` | au moins un `fault_bits != 0` |
| `0x10` | `NOT_49_KEYS` | `total_occupied != 49` |
| `0x20` | `DIAG_INCOMPLETE` | mode diagnostic demandé et trames `IPROPI_RAW` incomplètes |
| `0x40`, `0x80` | réservés | émis à `0` |

Règle unique : `state = FINAL` **si et seulement si** `flags == 0`. Une table
`PROVISIONAL` reste **pleinement jouable** pour les canaux qu'elle contient :
l'état est informatif, jamais un verrou.

Enregistrement par carte (inchangé) :

| Champ | Type | Remarque |
|---|---|---|
| `uid_position_entry_t` | préfixe | UID 64 bits + position, **inchangé** |
| `bitmap_occupation` | `u16` | bits `0..11`, trous autorisés |
| `reserved` | `u16` | `0` |

Règle de contenu (ADR-0009 §1 et §2) : la table est le **cliché du dernier
auto-test explicitement demandé**. Elle n'est jamais écrite au boot, et il
n'existe aucune commande d'édition ou d'écriture manuelle d'une entrée.

### Lignes `[STATUS]` (codes `0x20+` d'ADR-0004, `0x31` d'ADR-0009)

Format texte CDC :
- par défaut : `[STATUS] code=NOM pos=N`
- avec bitmap : `[STATUS] code=NOM pos=N bitmap=0xXYZ`
- exception : `[STATUS] code=SELFTEST_STARTED`

| Code | Nom |
|---|---|
| `20` | `SELFTEST_STARTED` |
| `21` | `SELFTEST_REFUSED_PLAYING` |
| `22` | `BOARD_MISSING` |
| `23` | `TABLE_WRITTEN` |
| `24` | `TABLE_NOT_WRITTEN_INCOMPLETE` |
| `25` | `TABLE_DIFFERS` |
| `26` | `TABLE_PARTIAL` |
| `27` | `TABLE_ERROR_FINAL` |
| `28` | `FLASH_TABLE_INVALID` |
| `29` | `UID_NOT_IN_TABLE` |
| `2A` | `CHANNEL_DIVERGENCE` |
| `2B` | `DUPLICATE_TAG` |
| `2C` | `SELFTEST_UNCALIBRATED` |
| `2D` | `BOARD_REFUSED_BUSY` |
| `2E` | `FAULT_DURING_TEST` |
| `2F` | `MAINTENANCE_BUSY` |
| `30` | `MAINTENANCE_NO_TABLE` |
| `31` | `TABLE_PROVISIONAL` |

### Lignes `[TABLE]`

Formats (existants, complétés par `state`/`flags`) :
- `[TABLE] loaded state=FINAL|PROVISIONAL flags=0xNN cards=<0..5> keys=<0..49>`
- `[TABLE] invalid ...`
- `[TABLE] pos=<0..4> uid=<16 hex> bitmap=0xXYZ keys=<a>-<b>`
- `[TABLE] diff pos=<0..4> old=0xXYZ new=0xXYZ`

Règles :
- la ligne `loaded` est émise au boot après chargement, **et** après chaque
  écriture ;
- `state=PROVISIONAL` est toujours accompagné de
  `[STATUS] code=TABLE_PROVISIONAL` ;
- `keys=a-b` donne la plage de touches de la carte ; le détail exact canal →
  touche est donné par les lignes `[MAP]` ;
- `diff` est émise position par position avant écriture, sans jamais corriger
  silencieusement (invariant 1) ;
- `cards` = nombre d'enregistrements écrits, `keys` = total occupé.

### Lignes `[MAP]`

Format :
`[MAP] pos=<0..4> ch=<0..11> key=<1..49> midi=<36..84> note=<DO2..DO6>(<MIDI>)`

Règles :
- une ligne par canal **assigné**, émise après chaque écriture de table et à
  chaque boot ;
- nommage de note conforme à ADR-0008 (`DO2(36)` … `DO6(84)`) ;
- sortie contrôlée par la constante `CLAVIER_MAP_LOG` : `1` pendant le bring-up,
  `0` en configuration finale.

### Lignes `[MODE]` et `[PUSH]`

Formats :
- `[MODE] maintenance=ON|OFF`
- `[PUSH] midi=<36..84> key=<1..49> pos=<0..4> ch=<0..11> start|stop|timeout`

Règles :
- aucun MIDI USB n'est envoyé pendant le mode maintenance ;
- `timeout` correspond à la coupure automatique du profil MAINTENANCE, bornée
  à 10 s (`SOLENOID_MAINT_MAX_MS`, ADR-0006) ; le HOLD du jeu normal est borné
  séparément à 20 s (`SOLENOID_HOLD_MAX_MS`, ADR-0007) ;
- un nouveau cycle de poussée n'est autorisé qu'après relâchement de la
  touche.

### Ligne `[SELFTEST]`

Format :
`[SELFTEST] req=<1..127> pos=<0..127> status=<OK|REFUSED_BUSY|ABORTED|UNCALIBRATED> bitmap=0xXYZ faults=0xXYZ tag=XXXX`

- `bitmap` et `faults` sont sur 12 bits ;
- `tag` est le `uid_tag` CRC16 sur 4 hex majuscules ;
- `pos=127` si l'UID n'est pas connu dans `uid_position.h`.

### Lignes `[SELFTEST_RAW]`

Formats :
- `[SELFTEST_RAW] req=<1..127> pos=<0..127> ch=<0..11> idle=<0..4095> probe=<0..4095>`
- `[SELFTEST_RAW] req=<1..127> pos=<0..127> ch=<0..11> delta=<signé> ohm_est=<Ω|--> i100=<A|-->`

Règles :
- émises quand le mode diagnostic est actif : option CMake
  `-DCLAVIER_SELFTEST_CALIBRATION=ON` (défaut **`ON`** pendant le bring-up,
  ADR-0009 §7) ou bascule `c` de la console CDC (livraison B) ;
- `delta = probe - idle` en décimal signé ;
- la décision d'occupation, elle, utilise `max(delta, 0)` avant comparaison au
  seuil 400 ;
- `ohm_est` : résistance estimée en ohms sur **2 décimales**, ou `--` si
  invalide ; `i100` : courant estimé à 100 % de duty en ampères sur **2
  décimales**, ou `--` si invalide (ADR-0009 §5c, §5d) ;
- `pos=127` si le `uid_tag` reçu n'a pas pu être rattaché à une position de
  `uid_position.h`.

### Ligne `[SELFTEST_OHM]` (synthèse par carte)

Format :
`[SELFTEST_OHM] pos=<0..4> n=<1..12> ohm_min=<Ω> ohm_max=<Ω> spread=<%> i100_min=<A> i100_max=<A>[ warn=i100>3.0A]`

Règles :
- calculée par le **Clavier** à partir des trames `0x601` des canaux **occupés**
  de la carte, une fois les 12 trames reçues ;
- `spread = (ohm_max - ohm_min) / moyenne`, en pourcent ;
- `warn=i100>3.0A` n'apparaît que si au moins un canal occupé dépasse la
  fenêtre 2–3 A en PULL 100 % ;
- ligne de confort de bring-up : elle sert à repérer une bobine hors gabarit.

## Règle d'assignation des touches

### Convention musicale figée

Notation scientifique, C4 = MIDI 60 (ADR-0008 ; §6 d'ADR-0004 remplacé par
ADR-0010 pour le sens de l'assignation).

- **touche 1 = DO2 = MIDI 36**
- **touche 49 = DO6 = MIDI 84**
- formule : `touche = note_midi - 35`
- hors plage MIDI `36..84` : note ignorée

### Règle ascendante (ADR-0010)

- L'ancrage se fait sur la touche **1** (`DO2`, MIDI 36) : la carte de
  **position 0** (nœud CAN 2), canal occupé le plus bas, joue **DO2**.
- Les cartes de `uid_position.h` sont parcourues de la **position la plus basse
  vers la plus haute** (`0 → 4`).
- Le bloc courant commence à la touche courante (initialement **1**) et
  **monte**.
- La taille d'un bloc = nombre de canaux occupés de la carte.
- Dans un bloc, les **canaux occupés triés par index croissant** reçoivent les
  **touches croissantes** du bloc.
- Les canaux non occupés sont sautés (trous autorisés).
- Les touches non attribuées (total occupé < 49) sont celles du **haut** (aigu).

### Exemple

Carte unique en position 0, avec canaux `0`, `1` et `2` occupés :

| Canal | Touche | Note MIDI | Note |
|---|---|---|---|
| 0 | 1 | 36 | DO2 |
| 1 | 2 | 37 | DO#2 / RÉb2 |
| 2 | 3 | 38 | RÉ2 |

Une carte placée en position 1 recevrait le bloc immédiatement plus aigu,
commençant à la touche 4.

Exemple à 2 cartes :

* position 0, `3` canaux occupés → touches `1,2,3`
* position 1, `2` canaux occupés → touches `4,5`
* dans chaque bloc, les canaux occupés les plus bas reçoivent les touches les
  plus graves du bloc.

### Contiguïté des positions et motif `ANCHOR_SHIFT`

L'ancrage étant grave, une position absente décale vers le haut **toutes** les
cartes plus aiguës. Pour que ce décalage ne soit jamais tacite :

- la table n'est `FINAL` que si les positions présentes sont **exactement
  `0..K−1`** (aucun trou avant la position présente la plus haute, position 0
  déclarée) ;
- sinon l'assignation est **quand même construite** (remplissage compact
  croissant des positions présentes) et la table est `PROVISIONAL` avec le motif
  **`0x40 ANCHOR_SHIFT`** ;
- aucune correction, aucun réordonnancement automatique (invariant 1) : c'est un
  signalement, au même titre que `MISSING_BOARD`.

## Contraintes de commissioning

- **Pas d'auto-test au boot** : au démarrage, le Moteur annonce seulement A et
  B ; la trame C n'est émise qu'en réponse à `0x401`.
- **Table flash côté Clavier = cliché du dernier auto-test demandé** (ADR-0009) :
  écriture uniquement si **au moins une** carte répondante a fourni un jeu
  complet (A+B+C), si aucun `uid_tag` n'est dupliqué, et si le contenu diffère
  de la table en mémoire. **Aucune** écriture au boot, **aucune** commande
  d'édition ou d'écriture manuelle d'une entrée.
- **Ce qui ne bloque plus l'écriture, mais marque la table** (`PROVISIONAL` +
  motif) : position de `uid_position.h` sans réponse (`MISSING_BOARD`) ; carte
  répondante absente de `uid_position.h`, exclue (`UNKNOWN_BOARD`) ; carte
  répondante avec `test_status != OK`, entrée conservée avec `bitmap = 0`
  (`BOARD_NOT_OK`) ; `fault_bits != 0` (`FAULT_DURING_TEST`) ; total occupé
  différent de 49 (`NOT_49_KEYS`, déjà signalé `TABLE_PARTIAL`) ; trames
  `IPROPI_RAW` incomplètes en mode diagnostic (`DIAG_INCOMPLETE`) ; positions
  présentes non contiguës depuis 0, position 0 absente (`ANCHOR_SHIFT`, motif
  `0x40`, ADR-0010).
- **Ce qui bloque toujours** : aucune carte complète, `uid_tag` ou position
  dupliqué, auto-test demandé pendant le jeu.
- **Pas de remapping silencieux** : toute divergence UID/bitmap/table est
  signalée explicitement (lignes `[TABLE]`, `[STATUS]`, `[MAP]`) avant et après
  écriture.
- **Table provisoire jouable** : une table `PROVISIONAL` reste utilisable pour
  les canaux qu'elle contient ; les canaux d'une carte présente sur le bus mais
  absente de la table restent refusés et signalés (`UID_NOT_IN_TABLE`), sans
  remappage.

## Remarques

- **Livraisons 1 et 2 implémentées** dans le dépôt : auto-test IPROPI, table
  flash, assignation persistante et `SOLENOID_COMMAND`.
- **Éléments ADR-0009 non encore implémentés** (au 2026-10-20) : format de table
  v2 (état/motifs), trame `IPROPI_OHM` `0x601`, PROBE 5 ms avec fenêtre
  d'échantillonnage en fin d'impulsion, lignes `[MAP]` et `[SELFTEST_OHM]`,
  console CDC de bring-up. Les sections correspondantes de ce document les
  décrivent comme **cible** ; le suivi fichier par fichier est dans
  [`impact-adr-0009.md`](impact-adr-0009.md).
- **Règle d'assignation ascendante (ADR-0010, 2026-10-20)** : décidée et
  documentée ici comme **cible**. Tant que l'implémentation n'est pas faite, le
  comportement réel de `commissioning_build_runtime` reste **descendant** (blocs
  ancrés sur la touche 49) ; suivi fichier par fichier dans
  [`impact-adr-0010.md`](impact-adr-0010.md).

## Tests manuels

- Phase 3a (UID) : [`tests/can_phase3_uid.md`](../../tests/can_phase3_uid.md)
- Phase 3b / Livraison 1 :
  [`tests/ipropi_selftest_phase3b.md`](../../tests/ipropi_selftest_phase3b.md)
- Phase 3b / Livraison 2 :
  [`tests/phase3b_bout_en_bout.md`](../../tests/phase3b_bout_en_bout.md)
- Bring-up commissioning provisoire (ADR-0009) :
  [`tests/bringup_commissioning_provisoire.md`](../../tests/bringup_commissioning_provisoire.md)

## Points non démontrés sans matériel

* latence appui touche → CAN → PWM sous charge réelle ;
* effet exact de l'écriture flash pendant activité CAN réelle (la sécurité
  logicielle de `flash_safe_execute` a été vérifiée dans le SDK 2.3.1, mais
  pas encore chronométrée au banc) ;
* comportement électrique d'un canal physiquement vide commandé **avant le
  premier auto-test** si la table Clavier était erronée.
