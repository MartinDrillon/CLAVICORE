# CAN — Phase 3a validée, Phase 3b spécifiée

Ce document synthétise les décisions actées dans
[ADR-0002](../adr/adr-0002-integration-can2040.md),
ADR-0005 et
[ADR-0004](../adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md).

**État réel** :
- **Phase 3a validée sur bus réel** : premier UID relevé de bout en bout
  `A3FA4C99CA526F4B` (addenda ADR-0003).
- **Phase 3b / Livraison 2** : protocole, table flash, résolution
  note→(UID, canal) et `SOLENOID_COMMAND` implémentés ; les validations
  strictement matérielles restantes sont listées en fin de document.

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
- si `bit7 = 1`, chaque carte émet aussi `IPROPI_RAW` (`0x600`) après la trame C ;
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
| 4..5 | `idle_avg` | moyenne ADC 12 bits LE, canal mux stabilisé avant PROBE |
| 6..7 | `probe_avg` | moyenne ADC 12 bits LE pendant PROBE |

Règles :
- 12 trames, une par canal `0..11`, après la trame C ;
- même délai initial UID-dérivé que les trames A/B/C, puis émission espacée ;
- `idle_avg` et `probe_avg` sont des valeurs brutes de diagnostic ; la décision
  d'occupation officielle est prise côté Moteur sur
  `delta = max(probe_avg - idle_avg, 0)` ;
- le seuil courant est **figé à 400 ADC** (`delta >= 400` => canal occupé) ;
- le statut protocolaire `UNCALIBRATED` reste réservé pour compatibilité, mais
  n'est pas attendu avec le firmware actuel.

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

En-tête :

| Champ | Type | Remarque |
|---|---|---|
| `magic` | `u32` | `'CLVC'` |
| `format_version` | `u16` | `1` |
| `count` | `u16` | nombre d'enregistrements |
| `crc32` | `u32` | CRC calculé sur les enregistrements seulement |

Enregistrement par carte :

| Champ | Type | Remarque |
|---|---|---|
| `uid_position_entry_t` | préfixe | UID 64 bits + position, **inchangé** |
| `bitmap_occupation` | `u16` | bits `0..11`, trous autorisés |
| `reserved` | `u16` | `0` |

### Lignes `[STATUS]` (codes `0x20+` d'ADR-0004)

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
- `[SELFTEST_RAW] req=<1..127> pos=<0..127> ch=<0..11> delta=<signé>`

Règles :
- émises uniquement quand le Clavier a été compilé avec
  `-DCLAVIER_SELFTEST_CALIBRATION=ON` et a donc demandé le mode calibration ;
- `delta = probe - idle` en décimal signé ;
- la décision d'occupation, elle, utilise `max(delta, 0)` avant comparaison au
  seuil 400 ;
- `pos=127` si le `uid_tag` reçu n'a pas pu être rattaché à une position de
  `uid_position.h`.

## Règle d'assignation des touches

### Convention musicale figée

- **touche 1 = DO1 = MIDI 36**
- **touche 49 = DO5 = MIDI 84**
- formule : `touche = note_midi - 35`
- hors plage MIDI `36..84` : note ignorée

### Règle descendante

- Les cartes de `uid_position.h` sont parcourues de la **position la plus
  haute vers la plus basse**.
- Le bloc courant commence à la touche **49** et descend.
- La taille d'un bloc = nombre de canaux occupés de la carte.
- Dans un bloc, les **canaux occupés triés par index croissant** reçoivent les
  **touches croissantes** du bloc.
- Les canaux non occupés sont sautés.

### Exemple

Carte unique en position 4, avec canaux `0`, `1` et `2` occupés :

| Canal | Touche | Note MIDI | Note |
|---|---|---|---|
| 0 | 47 | 82 | LA#4 / SIb4 |
| 1 | 48 | 83 | SI4 |
| 2 | 49 | 84 | DO5 |

Une carte placée en position 3 recevrait le bloc immédiatement plus grave, se
terminant à la touche 46.

Exemple à 2 cartes :

* position 4, `3` canaux occupés → touches `47,48,49`
* position 3, `2` canaux occupés → touches `45,46`
* dans chaque bloc, les canaux occupés les plus bas reçoivent les touches les
  plus graves du bloc.

## Contraintes de commissioning

- **Pas d'auto-test au boot** : au démarrage, le Moteur annonce seulement A et
  B ; la trame C n'est émise qu'en réponse à `0x401`.
- **Table flash côté Clavier** : écriture automatique après auto-test réussi,
  seulement si **toutes** les cartes listées dans `uid_position.h` ont répondu
  avec `test_status = 0`.
- **Pas de remapping silencieux** : toute divergence UID/bitmap/table doit être
  signalée explicitement avant toute écriture.
- **Table partielle admise** en bring-up : si le total occupé n'est pas 49, le
  Clavier signale `TABLE_PARTIAL`.

## Remarques

- La **Livraison 1** n'implémente pas encore la table flash, l'assignation
  persistante ni `SOLENOID_COMMAND` côté Moteur.

## Tests manuels

- Phase 3a (UID) : [`tests/can_phase3_uid.md`](../../tests/can_phase3_uid.md)
- Phase 3b / Livraison 1 :
  [`tests/ipropi_selftest_phase3b.md`](../../tests/ipropi_selftest_phase3b.md)
- Phase 3b / Livraison 2 :
  [`tests/phase3b_bout_en_bout.md`](../../tests/phase3b_bout_en_bout.md)

## Points non démontrés sans matériel

* latence appui touche → CAN → PWM sous charge réelle ;
* effet exact de l'écriture flash pendant activité CAN réelle (la sécurité
  logicielle de `flash_safe_execute` a été vérifiée dans le SDK 2.3.1, mais
  pas encore chronométrée au banc) ;
* comportement électrique d'un canal physiquement vide commandé **avant le
  premier auto-test** si la table Clavier était erronée.
