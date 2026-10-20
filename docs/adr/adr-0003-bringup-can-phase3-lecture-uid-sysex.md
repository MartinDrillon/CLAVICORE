---
title: "ADR-0003: Protocole de bring-up CAN Phase 3 — lecture de l'UID Moteur et affichage SysEx MIDI"
status: "Proposed"
date: "2026-10-17"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol"]
supersedes: ""
superseded_by: ""
---

### Status
**Proposed**

Aucune dérogation à un invariant de `AGENTS.md`. L'invariant 3 (table de commissioning persistante côté Clavier, seule source de vérité) est respecté : `uid_position.h` est une table d'amorçage, jamais une seconde source de vérité (Decision §6).

Divergences documentaires signalées (à corriger par le Documentaliste) :
* `docs/specs/Roadmap.md` place `can_protocol.h` dans `common/` ; `AGENTS.md` impose `shared/protocol/`. **Cette ADR retient `shared/protocol/`.**
* `Roadmap.md` écrit `Firmware/` (majuscule) ; le dépôt utilise `firmware/`.
* Le vendoring de can2040 reste celui d'ADR-0002 (sous-module git, tag `v1.7.0`, `firmware/common/can2040/`). `CAN.md` et ADR-0002 sont cohérents ; seule la Roadmap (`common/can2040/`) diverge. `firmware/common/` n'est pas listé dans les frontières d'`AGENTS.md` (sans conséquence : code tiers).

### Context

La Phase 3 demande un premier échange CAN entre le Clavier et **une** carte Moteur : un keycode spécial fait remonter l'UID de la carte, que l'utilisateur relève à la main pour écrire `uid_position.h`. Les cartes Moteur sont anonymes (invariant 2) : leur seule identité est l'UID 64 bits du RP2350. Avant toute implémentation (invariant 5), il faut figer le format des premières trames, le déclencheur, le canal d'affichage et le statut de `uid_position.h`.

Faits vérifiés (2026-10-17) :

1. **UID, SDK 2.3.1** (`src/rp2_common/pico_unique_id/`) :
   * `pico_get_unique_board_id(pico_unique_board_id_t *)` ; `PICO_UNIQUE_BOARD_ID_SIZE_BYTES == 8` (`unique_id.h` l.38) ; `pico_unique_board_id_t` = `uint8_t id[8]` (l.78-80).
   * `pico_get_unique_board_id_string(char *, uint)` exige `len >= 17` et produit 16 chiffres hexa majuscules, dans l'ordre `id[0]..id[7]` (`unique_id.c` l.58-65).
   * **Sur RP2350** (branche `#else`, `unique_id.c` l.38-49), l'ID vient de l'appel ROM `GET_SYS_INFO` avec `SYS_INFO_CHIP_INFO`, recopié en ordre inversé. Cette branche n'appelle ni `flash_get_unique_id` ni `otp_get_chipid`.
   * La **provenance physique exacte (OTP ou autre) n'est pas démontrable depuis les sources du SDK** (code ROM) : à confirmer dans le datasheet RP2350 (« à vérifier »). Sans conséquence ici : on utilise toujours la sortie du SDK, jamais une reconstruction maison.
   * Le `CMakeLists.txt` de `pico_unique_id` lie `hardware_flash` ; le Moteur doit lier explicitement `pico_unique_id`.
2. **TinyUSB 0.18.0** (`class/midi/midi_device.c`) :
   * `tud_midi_stream_write` découpe un flux d'octets en paquets USB-MIDI de 4 octets, gère `0xF0..0xF7` et conserve son état entre appels.
   * Il n'est **pas tout-ou-rien** : il s'arrête quand le FIFO a moins de 4 octets libres et retourne le nombre d'octets consommés. Un SysEx partiel laisse le flux « au milieu d'un SysEx ».
   * Le FIFO TX est dimensionné en octets : `CFG_TUD_MIDI_TX_BUFSIZE` = 64 dans `firmware/clavier/src/tusb_config.h`, soit 16 paquets. Aucune API publique d'espace libre dans `midi_device.h`.
3. **`midi.c` actuel** : `midi_send_message()` refuse si `!tud_mounted()` et compare le retour de `tud_midi_stream_write(0, …)` à la taille ; pas de file applicative ; `midi_task()` = `tud_task()`. Aucun SysEx n'existe.
4. **Device USB** : `usb_descriptors.c` expose un seul device MIDI (produit « Clavicore Keyboard », endpoints 0x01/0x81, 64 o). Il suffit tel quel : aucune modification de descripteur.
5. **Pas de canal texte** : GPIO0/1 = CAN (pas d'UART libre) ; `pico_enable_stdio_uart(clavier 0)` et aucun stdio USB, donc les `printf` existants ne sortent nulle part.
6. **Keycodes** : `keycode_kind_t` est un enum à valeurs implicites empaquetées en `kind << 8 | value` (`keycode.h`, `keymap.c`). `main.c` traite tout keycode non-note dans `handle_matrix_transition` ; `keycode_press` ignore les kinds inconnus.
7. **Acquis ADR-0002 / CAN.md (non redécidés)** : PIO0 ; Clavier RX=GPIO0/TX=GPIO1 ; Moteur RX=GPIO46/TX=GPIO47 ; callback RX = simple copie dans un anneau SPSC ; `tx_queue[4]` ; 500 kbit/s constante nommée ; aucun test CAN avec un seul nœud ; R24 (120 Ω) `dnp` sur les deux cartes.

### Decision

#### 1. Constantes et conventions (dans `shared/protocol/can_protocol.h`)

* `CAN_BITRATE_HZ = 500000u` (constante nommée unique, à confirmer empiriquement par ce bring-up). `CAN_PROTO_VERSION = 1u`.
* Identifiants **11 bits standard** uniquement. Les trames étendues ou RTR sont ignorées et comptées.
* Entiers multi-octets en **little-endian** (natif RP2350). L'UID est un tableau d'octets, transmis dans l'ordre `id[0]..id[7]` de `pico_get_unique_board_id` : l'ordre sur le fil est identique à l'ordre de `pico_get_unique_board_id_string`.
* DLC **strict** : toute trame de DLC inattendu est rejetée et comptée (compteur de diagnostic), jamais interprétée. Octets réservés : émis à 0, ignorés en réception.
* `shared/protocol` reste du C pur, sans dépendance au SDK (encodage et décodage testables sur PC). La conversion vers `struct can2040_msg` reste dans `can_link.c` de chaque carte.

#### 2. Plan d'identifiants (priorité CAN : le plus petit gagne)

| Plage | Usage | Statut |
|---|---|---|
| `0x000-0x0FF` | Sécurité : `EMERGENCY_STOP` = `0x001`, `FAULT_REPORT` = `0x010` | réservé |
| `0x100-0x1FF` | Jeu temps réel : `SOLENOID_COMMAND` = `0x100` | réservé |
| `0x200-0x3FF` | Sessions : `BOARD_ASSIGN` = `0x200` ; acquittements | réservé |
| `0x400` | `UID_REQUEST` | **défini ici** |
| `0x410` | `BOARD_ANNOUNCE_UID` (trame A) | **défini ici** |
| `0x411` | `BOARD_ANNOUNCE_INFO` (trame B) | **défini ici** |
| `0x412` | `BOARD_ANNOUNCE_STATUS` (trame C : bitmap IPROPI 12 bits, Phase 4) | réservé |
| `0x500-0x5FF` | `CONFIG_READ` = `0x500`, `CONFIG_WRITE` = `0x501` | réservé |
| `0x600-0x6FF` | Diagnostic | réservé |
| `0x7F0-0x7FF` | **Interdits** (7 bits de poids fort tous récessifs, règle CAN) | interdit |

Le jeu et les défauts l'emportent sur la découverte. Les IDs réservés ne doivent être ni émis ni interprétés en Phase 3 (compteur « ID inconnu »).

#### 3. Trames du bring-up

**`UID_REQUEST`** : ID `0x400`, DLC 2, Clavier → diffusion.
`[0]` = `proto_version` ; `[1]` = `request_id` (1..127 ; 0 est réservé aux annonces spontanées ; incrémenté modulo 127).

**`BOARD_ANNOUNCE`** : Moteur → Clavier, émise en 2 trames consécutives (UID 64 bits = 8 octets = une trame pleine).

* **Trame A** : ID `0x410`, DLC 8, `[0..7]` = UID.
* **Trame B** : ID `0x411`, DLC 8 :
  * `[0..1]` : `uid_tag` = CRC-16/CCITT (poly `0x1021`, init `0xFFFF`) des 8 octets d'UID, LE.
  * `[2]` : `proto_version` du Moteur.
  * `[3..5]` : `fw_major`, `fw_minor`, `fw_patch` (0..127 chacun).
  * `[6]` : `reply_to` (écho du `request_id`, 0 = annonce spontanée de boot).
  * `[7]` : réservé (0).

La trame B porte `uid_tag` car, avec plusieurs cartes, une trame B d'une carte peut suivre la trame A d'une autre : l'association se fait par le tag, jamais par l'ordre d'arrivée. Si deux UID reçus partagent le même tag, le Clavier le signale (`AMBIGUOUS_TAG`) au lieu d'associer.

Le bitmap IPROPI (hors périmètre) viendra en Phase 4 dans une **trame C** (`0x412` : `uid_tag` + bitmap 12 bits + octets réservés). Aucun octet des trames A/B ne change : l'évolution du format passe par de nouveaux IDs ou par `CAN_PROTO_VERSION`. Un Moteur répond **toujours** à une `UID_REQUEST`, même de version différente, en indiquant sa propre version : c'est le Clavier qui signale `PROTO_MISMATCH`.

#### 4. Plusieurs cartes, timeouts, erreurs

* Deux cartes émettant la même trame au même instant (même ID, données différentes) provoquent une erreur de bit CAN : l'arbitrage ne départage que des IDs différents. Le format évite donc la synchronisation : tout Moteur répond après un **délai pseudo-aléatoire dérivé de l'UID** = `(uid_tag % ANNOUNCE_JITTER_SLOTS) × ANNOUNCE_SLOT_US`, avec `ANNOUNCE_JITTER_SLOTS = 128` et `ANNOUNCE_SLOT_US = 1000` (0-127 ms ; une paire de trames dure environ 0,5 ms à 500 kbit/s). Le risque de collision de slot entre deux cartes est de quelques % ; il est résolu par le mécanisme standard d'erreur et de retransmission CAN, sans blocage logique.
* **Moteur** : timer non bloquant (`time_us_32`), un seul slot de réponse en attente (une nouvelle requête met à jour `reply_to` sans réarmer le délai). Si `can2040_transmit` échoue (file de 4 pleine), la trame est abandonnée et un compteur est incrémenté : pas de perte silencieuse.
* **Clavier** : une seule `UID_REQUEST` en vol. Un nouvel appui pendant `UID_REQUEST_TIMEOUT_MS = 500` est refusé avec un SysEx `STATUS`. Passé ce délai sans réponse : `STATUS NO_RESPONSE`. Pas de retransmission applicative automatique : l'utilisateur ré-appuie. C'est un outil de bring-up avec humain dans la boucle, et la retransmission au niveau CAN (erreurs de bit) est gérée par le contrôleur.
* Réassemblage Clavier : table de 8 entrées maximum (5 cartes + marge), clé = `uid_tag`. Un `UID_REPORT` est émis dès que A et B sont reçues. Si B n'arrive pas dans `ANNOUNCE_PAIR_TIMEOUT_MS = 20`, le rapport est émis avec `fw` = `0x7F 0x7F 0x7F` et `STATUS INCOMPLETE`.
* **À vérifier dans can2040 v1.7.0** : le comportement d'une trame non acquittée (aucun autre nœud sur le bus). Je n'ai pas vérifié qu'il existe une annulation de transmission. Hypothèse : le contrôleur réessaie indéfiniment. Conséquence : le Clavier n'enfile jamais plus d'une requête non acquittée, et le Moteur ne doit pas saturer sa file.
* Le champ « horodatage » proposé n'est **pas retenu** : inutile pour un relevé manuel.

#### 5. Déclencheurs

**Clavier** : nouveau `KEYCODE_GET_UID`, ajouté **en fin** de `keycode_kind_t` pour ne pas décaler les valeurs empaquetées existantes. Nom d'affichage `GET_UID`, macro `KEYCODE_STORAGE_BEHAVIOR(KEYCODE_GET_UID)`. Il émet une `UID_REQUEST` à l'**appui** seulement. Il n'y a pas d'état dans `keycode_state_t`, l'effet de bord est fait dans `main.c`. **Le choix de la position physique dans `keymap.c` est laissé à l'utilisateur** (la Roadmap prévoit qu'il modifie la matrice).

**Moteur** (firmware identique sur toutes les cartes) : réponse à `UID_REQUEST` **et** annonce spontanée au boot (`reply_to` = 0), après init CAN et délai UID-dérivé. Justification : la requête seule oblige à déclencher à chaque test ; le boot seul rate une carte alimentée avant le Clavier. Avec les deux, une carte qui démarre pendant que le Clavier tourne apparaît d'elle-même, et un Clavier démarré après peut redemander. Pas d'annonce périodique (hors périmètre ; éventuel heartbeat = ADR ultérieure).

Le Moteur ne traite en Phase 3 **aucune** commande de jeu : il n'existe encore aucun message qui active un solénoïde, donc le chemin CAN ne peut pas contourner la couche pilote (invariant 4). Le traitement CAN est non bloquant et ne doit jamais retarder `solenoid_driver_tick`.

#### 6. Affichage : SysEx MIDI USB

> **Superseded par ADR-0005 (2026-10-18)** pour l'affichage diagnostic. Cette section est conservée pour l'historique de la décision initiale.

Format (octets 7 bits) : `F0 7D 43 01 <type> <données…> F7`.
`7D` = ID non commercial / éducatif. `43` ('C') et `01` (version du format SysEx) filtrent les autres usages de `7D`.

**`type 0x01 UID_REPORT`** (29 octets) :
`F0 7D 43 01 | 01 | origin | request_id | uid[16 nibbles] | fw_maj fw_min fw_patch | proto_ver | position | F7`

* `origin` : 00 = réponse à une requête, 01 = annonce de boot.
* `request_id` : 1..127, ou 0 pour une annonce de boot.
* **UID : 16 octets, un nibble (0x00-0x0F) par octet, nibble de poids fort en premier**, donc dans l'ordre exact de `pico_get_unique_board_id_string`.
* `position` : 0..4 si l'UID figure dans `uid_position.h`, `0x7F` si inconnu (y compris tant que la table est vide en Phase 3).

Exemple avec un UID fictif `E660583883123456`, fw 0.1.0, proto 1, annonce de boot, UID inconnu :
`F0 7D 43 01 01 01 00 0E 06 06 00 05 08 03 08 08 03 01 02 03 04 05 06 00 01 00 01 7F F7`.

Justification du choix en nibbles plutôt qu'en 16 caractères ASCII hexa : dans la vue hexadécimale de MIDI-OX, un nibble apparaît tel quel (`0E 06 06 00…` donne `E660…` en retirant le 0 de tête). Aucune table ASCII ni vue ASCII n'est nécessaire, et c'est moins sujet aux erreurs de transcription.

**`type 0x02 STATUS`** : `F0 7D 43 01 02 <code> <détail> F7`. Codes :
* `01` NO_RESPONSE, `02` PROTO_MISMATCH, `03` INCOMPLETE, `04` CAN_TX_FAIL, `05` RX_OVERRUN, `06` BAD_FRAME, `07` AMBIGUOUS_TAG, `08` BUSY (requête déjà en vol).
* `10` UID_UNKNOWN (UID absent de `uid_position.h`), `11` TABLE_INVALID (doublon d'UID ou entrée invalide dans la table), `12` SEED_DIFFERS (Phase 4, voir §7).

MIDI-OX (indications générales, menus « à vérifier ») :
* Choisir l'entrée MIDI correspondant au périphérique « Clavicore Keyboard » dans les options des périphériques MIDI.
* Ouvrir le moniteur d'entrée et/ou la vue SysEx : les octets s'affichent en hexa.
* Vérifier que les filtres d'entrée ne masquent pas les SysEx et que la taille des tampons SysEx est suffisante (à vérifier).
* Sous Windows, le port peut être verrouillé par un autre logiciel : n'ouvrir qu'une application à la fois.
* Relever l'UID en retirant le 0 de tête de chaque octet ; **lancer la requête deux fois et comparer**.

Émission dans `midi.c` : nouvelle fonction `midi_send_sysex(const uint8_t *data, uint8_t len)` (len ≤ 32).
* Elle retourne un code d'erreur explicite : `!tud_mounted` = `MIDI_NOT_MOUNTED`, trop long, déjà en cours.
* Elle copie le message dans un tampon `pending`. `midi_task()` le pousse via `tud_midi_stream_write` et conserve l'offset en cas d'écriture partielle.
* Tant qu'un SysEx est en cours, `midi_note_on` / `midi_note_off` refusent explicitement (un message de note ne doit pas s'intercaler dans un SysEx en cours) et le refus est compté.
* `CFG_TUD_MIDI_TX_BUFSIZE` passe de 64 à **128** (`tusb_config.h` uniquement, ni descripteur ni taille d'endpoint), décision utilisateur tranchée le 2026-10-17, pour qu'un SysEx de 29 octets (10 paquets = 40 octets) tienne largement. L'écriture partielle devient quasi impossible, mais elle reste traitée.
* Si l'USB n'est pas monté, le rapport est perdu (message `MIDI_NOT_MOUNTED` compté) : l'utilisateur redemande.

Priorité « CAN avant MIDI » (Roadmap Phase 5) : en boucle principale, l'ordre est : traiter l'anneau RX CAN, puis émettre le SysEx, puis `midi_task()`. Le chemin des notes MIDI n'est pas modifié. `PIO0_IRQ_0` conserve la priorité la plus haute (ADR-0002) ; vérifier que l'IRQ USB de TinyUSB n'a pas une priorité supérieure.

#### 7. `uid_position.h` : table d'amorçage, pas source de vérité

* **Format partagé** dans `shared/protocol/uid_position_format.h` : type `clavicore_uid64_t` (`uint8_t id[8]`, même disposition que `pico_unique_board_id_t`) ; `uid_position_entry_t { clavicore_uid64_t uid; uint8_t position; }` ; `UID_POSITION_COUNT = 5` ; sentinelle : `position == 0xFF` = entrée vide ; UID tout à 0x00 ou tout à 0xFF = invalide ; `UID_POSITION_FORMAT_VERSION = 1`.
* **Données** dans `firmware/clavier/src/uid_position.h` (valeurs relevées par l'utilisateur, tableau `uid_position_table[5]`). Elles vivent uniquement côté Clavier. Le Moteur n'inclut pas ce fichier : son CMake ne doit pas avoir ce chemin dans ses includes (invariant 2).
* **Position 0-4** = numéro de position que l'utilisateur fournira plus tard. La convention (par ex. 0 = carte la plus proche du Clavier) n'est **pas décidée** à ce stade ; question tranchée le 2026-10-17 : en Phase 3, `uid_position.h` peut rester vide ou ne contenir que des UID relevés avec un numéro provisoire, et la convention sera figée dans une mise à jour ultérieure.
* **Phase 3 (lecture seule)** : le Clavier ne s'en sert que pour annoter `position` dans le `UID_REPORT` et signaler `UID_UNKNOWN` ou `TABLE_INVALID`. Il ne corrige rien, ne réattribue rien (invariant 1). Une table vide au départ est normale.
* **Phase 4 (migration)** : la table persistante en flash (UID → position → canal → touche) reprend `uid_position_entry_t` comme **préfixe** de ses enregistrements, pour ajouter canal et touche sans casser le format. `uid_position.h` ne sert alors qu'à amorcer la table à la première initialisation (si la flash est vierge) avec un SysEx `TABLE_SEEDED` explicite. Dès que la flash est initialisée, elle fait foi. Si le fichier compilé diffère de la flash, signaler `SEED_DIFFERS` et ne rien écraser. Le format persistant fera l'objet d'une ADR dédiée (invariant 5).

#### 8. Procédure de relevé (côté utilisateur)

**Prérequis** :
* Sous-module can2040 v1.7.0 ajouté (`firmware/common/can2040/`).
* `-DPICO_RP2350` sur les cibles `clavier` et `moteur`.
* R24 soudée sur le Clavier et sur la carte Moteur testée pour ce relevé à 2 nœuds CAN ; la dessouder ensuite si cette carte ne se trouve pas à une extrémité du chaînage final. Le bus final ne doit avoir une terminaison qu'à chaque extrémité (rappel `CAN.md`) ; décision tranchée le 2026-10-17.
* MIDI-OX configuré sur « Clavicore Keyboard ».
* Moteur flashé avec le firmware Phase 2a + CAN (même binaire sur toutes les cartes).

**Étapes** :
1. Brancher une seule carte Moteur au Clavier via CAN, l'alimenter.
2. Observer l'annonce de boot dans MIDI-OX (sinon appuyer sur la touche `GET_UID`).
3. Noter les 16 nibbles ; relancer et comparer.
4. Répéter pour chacune des 5 cartes dans l'ordre physique.
5. Reporter les UID relevés dans `uid_position.h` ; laisser la table vide ou utiliser des numéros de position provisoires tant que l'utilisateur n'a pas figé la convention. Recompiler et reflasher le Clavier ; si la table est vide, vérifier que `position = 0x7F`, sinon vérifier que la valeur renseignée correspond bien à l'entrée de table.

**Validé au passage** : la liaison CAN, la terminaison, le débit de 500 kbit/s (constante), le câblage RX/TX (Clavier GPIO0/1 ; Moteur GPIO46/47).

### Consequences

#### Positive
* **POS-001**: Aucune modification de descripteur USB, aucun UART requis, aucune dépendance logicielle nouvelle côté hôte (MIDI-OX suffit).
* **POS-002**: Les IDs, les plages réservées et le champ de version sont figés avant la Phase 4 ; ajouter le bitmap IPROPI ne casse pas les trames A/B.
* **POS-003**: L'invariant 3 est tenu : `uid_position.h` ne peut pas devenir une source de vérité concurrente, et toute divergence est signalée.
* **POS-004**: L'encodage et le décodage (`shared/protocol`, SysEx) sont testables sur PC, sans carte.

#### Negative
* **NEG-001**: L'annonce se fait en 2 trames reliées par un tag CRC-16 ; une collision de tag est possible (détectée, signalée).
* **NEG-002**: Le délai UID-dérivé (0-127 ms) ajoute de la latence à la réponse ; sans conséquence pour un bring-up manuel.
* **NEG-003**: Le FIFO MIDI passe à 128 o et `midi.c` gagne un état « SysEx en cours » ; les notes sont refusées pendant ce temps (cas quasi impossible, mais à gérer).
* **NEG-004**: Pour le relevé d'UID en bus CAN à 2 nœuds, il faut souder R24 sur le Clavier et sur la carte Moteur testée. Si cette carte finit au milieu du chaînage final, il faudra la dessouder ; le bus final ne garde une terminaison qu'à chaque extrémité.
* **NEG-005**: Le comportement de can2040 sans acquittement n'est pas vérifié (voir §4).

### Alternatives Considered

#### CDC série pour l'affichage
* **ALT-001**: **Description**: ajouter une interface CDC et lire l'UID dans un terminal série.
* **ALT-002**: **Rejection Reason**: nécessite de modifier les descripteurs USB et `tusb_config.h` ; décision utilisateur d'utiliser le device MIDI existant. Pas d'UART libre (GPIO0/1 = CAN) et stdio non activé.

#### UID encodé dans l'ID d'arbitrage CAN
* **ALT-003**: **Description**: faire figurer l'UID dans les bits de l'identifiant 11 bits.
* **ALT-004**: **Rejection Reason**: 11 bits ne portent pas 64 bits ; cela mélangerait identité et priorité.

#### Adresse courte attribuée sans UID
* **ALT-005**: **Description**: le Clavier attribue des adresses courtes dès le départ, sans lire l'UID.
* **ALT-006**: **Rejection Reason**: contredit les invariants 2 et 3 (identité = UID, rôle dans la table côté Clavier) ; l'adresse de session reste une option de Phase 4 après `BOARD_ASSIGN`.

#### Annonce au boot uniquement (sans requête)
* **ALT-007**: **Description**: aucune `UID_REQUEST`, seulement l'annonce spontanée au boot.
* **ALT-008**: **Rejection Reason**: une carte alimentée avant le Clavier, ou une annonce non vue, obligerait à recycler l'alimentation. La requête seule est rejetée pour la raison inverse (§5).

#### UID encodé en 16 caractères ASCII hexa dans le SysEx
* **ALT-009**: **Description**: afficher `E660…` en ASCII dans le SysEx.
* **ALT-010**: **Rejection Reason**: l'affichage dépend de l'existence d'une vue ASCII dans MIDI-OX (non vérifiée) ; la lecture en hexa brut est plus risquée. Le nibble par octet est lisible dans n'importe quelle vue hexadécimale.

### Implementation Notes
* **IMP-001**: `can2040_rx_cb` reste une pure copie dans l'anneau SPSC (ADR-0002) ; le parsing de `shared/protocol` se fait en boucle principale. Le Moteur lie `pico_unique_id`.
* **IMP-002**: Les constantes sont dans `shared/protocol/can_protocol.h`. La version du firmware vient d'un `config.h` du Moteur.
* **IMP-003**: Vérifier dans les sources de can2040 v1.7.0 les noms des drapeaux RTR/EFF (« à vérifier »), le comportement sans ACK, et s'il existe un moyen d'annuler une trame en file.
* **IMP-004**: Aucune édition du schéma KiCad ; vérifier les GPIO CAN (Clavier 0/1, Moteur 46/47) à l'analyseur logique si la liaison ne répond pas.

### Tâches ordonnées (du plus isolé au plus intégré)

**Codeur**
1. `shared/protocol/can_protocol.h/.c` : constantes, plages d'IDs, encodeurs/décodeurs de `UID_REQUEST`, trames A/B, `uid_tag`, délai de jitter ; tests unitaires sur PC (`tests/`).
2. `shared/protocol/uid_position_format.h` et `firmware/clavier/src/uid_position.h` (table vide), fonction de recherche et contrôle de table ; tests sur PC.
3. Encodeur SysEx `UID_REPORT`/`STATUS` ; test avec un vecteur d'octets de référence (voir l'exemple ci-dessus).
4. `midi_send_sysex`, `tusb_config.h` (FIFO 128), état « SysEx en cours » ; test manuel en situation réelle (Clavier + carte Moteur sur CAN) dans MIDI-OX, sans ajouter de mode ou d'option de build « UID factice » (question tranchée le 2026-10-17).
5. `KEYCODE_GET_UID` : enum (en fin), nom, macro, appel dans `main.c`.
6. Sous-module can2040, `can_link` Moteur : anneau RX, lecture de l'UID, annonce de boot et réponse à `UID_REQUEST`, jitter.
7. `can_link` Clavier : émission de `UID_REQUEST`, réassemblage, timeouts, SysEx.
8. Test bout en bout selon la procédure §8.

**Documentaliste**
1. `docs/specs/Roadmap.md` : `can_protocol.h` → `shared/protocol/` ; Phase 3 (message, IDs, annonce de boot) ; corriger `Firmware/` → `firmware/`.
2. `docs/specs/CAN.md` : checklist (R24 + référence à cette ADR, ordre physique du chaînage).
3. Spec de trames : `docs/specs/CAN_protocol.md` (tableaux de §2-3).
4. Procédure de test manuelle dans `tests/` (§8, avec capture MIDI-OX).
5. Note de build du sous-module can2040 (`build_clavier_windows.md`, ADR-0002 IMP-002).

### Modules impactés
`firmware/clavier` (keycode, keymap, midi, main, `tusb_config.h`, `can_link`), `firmware/moteur` (`main.c`, `CMakeLists.txt`, `can_link`), `shared/protocol` (à créer), `firmware/common/can2040` (sous-module, ADR-0002).

### References
* **REF-001**: [ADR-0001](./adr-0001-hal-solenoide-drv8874-phen.md)
* **REF-002**: [ADR-0002](./adr-0002-integration-can2040.md)
* **REF-003**: `docs/specs/CAN.md`, `docs/specs/Roadmap.md` (Phase 3, Commissioning, Protocole CAN à spécifier)
* **REF-004**: SDK 2.3.1 : `src/rp2_common/pico_unique_id/`, `lib/tinyusb/src/class/midi/midi_device.c`

### Addendum 2026-10-17

Décision utilisateur : ajouter côté Clavier un keycode de diagnostic `KEYCODE_TEST_SYSEX`, **ajouté en fin de `keycode_kind_t` après `KEYCODE_GET_UID`**. À l'appui, ce keycode émet **directement en SysEx** un `UID_REPORT` avec un **UID fictif clairement identifiable**, sans utiliser le CAN et sans nécessiter de carte Moteur, afin de valider la chaîne **SysEx / USB-MIDI / MIDI-OX seule**. Contexte : l'appui sur `KEYCODE_GET_UID` ne produisait aucun message, pas même un `STATUS`.

Cet addendum **amende et remplace** la décision du 2026-10-17 excluant une option « UID factice » de build : ici, il ne s'agit pas d'un mode protocolaire mais d'un **outil de diagnostic local au Clavier**, désactivable par l'option CMake `CLAVIER_ENABLE_TEST_KEYCODES` (défaut **ON**).

Contraintes :
* **Aucune modification de `shared/protocol`** ; le format SysEx reste inchangé.
* `KEYCODE_TEST_SYSEX` **n'écrit jamais** dans `uid_position.h` ni dans la table de commissioning.
* `KEYCODE_TEST_SYSEX` **n'utilise jamais le CAN**.
* L'UID fictif **ne doit jamais pouvoir être relevé comme UID réel** ; il est explicitement marqué comme diagnostic, par `origin/request_id = 0x7E` et une version firmware `127.127.127`, ou toute autre sentinelle d'implémentation offrant la même non-ambiguïté.


### Addendum 2026-10-18 — Journal CDC, retrait des diagnostics SysEx 0x7C/0x7D

- **Changement d'interface USB** : le Clavier devient un périphérique composite MIDI + CDC (ACM), PID 0x4010 -> 0x4011, interfaces MIDI inchangées (numéros 0-1), CDC ajouté en 2-3, EP 0x82 (notif), 0x03/0x83 (données). Cela amende les alternatives « CDC série » rejetées : le CDC n'est pas le canal d'affichage de l'UID (toujours SysEx), seulement un journal de diagnostic texte.
- `printf` est redirigé vers un anneau (4 Kio) vidé par `usb_log_task()` dans la boucle principale ; jamais depuis une IRQ, jamais bloquant.
- **Retrait** : SysEx de diagnostic 0x7D (CAN) et 0x7C (broche), keycodes `TEST_SYSEX`, `TEST_CAN_DIAG`, `TEST_CAN_PIN`, option `CLAVIER_ENABLE_TEST_KEYCODES`, `sysex_diag.c/.h`. Les SysEx de production (UID_REPORT, STATUS) sont conservés.
- Aucun changement dans `shared/protocol/`.

### Addendum 2026-10-18 (2) — Validation Phase 3 et nettoyage

- Premier UID relevé de bout en bout (CAN puis SysEx UID_REPORT) : `A3FA4C99CA526F4B`, firmware 0.1.0, proto 1. Le blocage initial était matériel (ferrite à la place d'une résistance, câble CAN en court-circuit), pas logiciel.
- Retrait des diagnostics temporaires : mode répétition (`r`), échantillonnage TX/RX 3 ms, instantanés `can_link_log_state`, lecture CDC entrante. Le journal CDC en sortie est conservé.
- `uid_position.h` : convention provisoire, nœud 1 = Clavier, nœuds 2..6 = Moteurs, position = nœud - 2 (0..4). Cette entrée est une table d'amorçage, pas la source de vérité (invariant 3). Aucun changement dans `shared/protocol/`.
