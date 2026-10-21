---
title: "ADR-0004: Auto-test IPROPI, table d'assignation flash et SOLENOID_COMMAND"
status: "Accepted"
date: "2026-10-18"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol", "hardware"]
supersedes: ""
superseded_by: ""
---

### Status
Dérogation à un invariant — validation humaine requise : invariant 1 de `AGENTS.md` reformulé (plus d'auto-test au boot), validé par l'utilisateur le 2026-10-18.

**Accepted**

Divergences documentaires à corriger par le Documentaliste :
* `docs/specs/Roadmap.md` écrit « 49 = DO1 » (ancien sens). Sens définitif : **touche 1 = DO1 (MIDI 36), touche 49 = DO5 (MIDI 84)**.
* La case Roadmap « Valider PIO/IRQ can2040 » est obsolète : la Phase 3a est validée sur bus réel (addenda ADR-0003, UID `A3FA4C99CA526F4B` relevé). `CAN.md` dit encore « non testée sur matériel » : à corriger.
* Les nets `/SENSx` (GPIO0, 2, 6, 8…) du Moteur **ne sont pas des IPROPI** : ce sont les entrées PH/IN2 (direction) des DRV8874. Le nom est trompeur.

### Context

L'invariant 1 impose de signaler explicitement toute divergence entre un résultat d'auto-test IPROPI demandé, ou entre la liste d'UID présents au boot et la table de commissioning côté Clavier, sans jamais corriger silencieusement. Aujourd'hui la table n'existe pas, aucun message ne pilote un solénoïde (`SOLENOID_COMMAND` 0x100, `BOARD_ASSIGN` 0x200 et trame C 0x412 sont réservés), et le Moteur est anonyme (invariant 2).

Faits vérifiés (netlist `Solenoide_drive.xml`, `pinmap.generated_drive.h`) :
* Mux **74HC4067 = IC17**. `/MUX_EN` = GPIO42 (`~E`, **actif BAS**). Sélection S0..S3 = GPIO44, 43, 38, 36 (S0 = LSB). Sortie Z = `/MUX OUT` = GPIO45 = **ADC5** sur RP2354B.
* Les IPROPI conditionnés (nets `/ADC0../ADC11`, résistance vers GND) arrivent sur le mux. Mapping canal → entrée mux (0..15) : 0→9, 1→10, 2→11, 3→12, 4→13, 5→14, 6→1, 7→2, 8→5, 9→3, 10→4, 11→6. Entrées 0, 7, 8, 15 non connectées. Canal n = `PWMn`.
* Bouton de test : `PIN_BOUTON_TEST` = GPIO34.
* `solenoid_profile.h` : PULL 15 ms / 100 %, HOLD 25 %, plafonds driver `SOLENOID_DUTY_MAX_PERCENT` 100 et `SOLENOID_PULL_MAX_MS` 50 (invariant 4).
* `uid_position_format.h` : `uid_position_entry_t {uid[8], position}`, préfixe imposé des enregistrements Phase 4 (ADR-0003 §7), `UID_POSITION_COUNT` = 5.
* `uid_position.h` ne contient aujourd'hui qu'une carte (position 4, nœud 6).
* Parc : 49 touches = 49 solénoïdes max, 12 canaux par carte, 5 cartes ; 5×12 = 60 > 49, donc des canaux non occupés sont normaux.

### Decision

#### 1. Auto-test IPROPI (carte Moteur)
* Séquentiel, **un canal à la fois**, impulsion **PROBE** : 20 % de duty, sens **PULL uniquement**, durée juste suffisante à la détection. La durée est une constante réglable dans `solenoid_profile.h`.
* PROBE est un **nouveau profil de la couche pilote** (`solenoid_driver.c`). Le plafond (durée, duty) est appliqué par le driver : le code d'auto-test ne peut pas piloter un canal hors bornes (invariant 4). L'impulsion doit rester sous le seuil d'attraction mécanique (à vérifier au banc).
* Refus si un canal n'est pas IDLE : `test_status` = 1 (refusé, occupé).
* Mesure : crête ou moyenne de l'ADC sur `MUX_OUT` après un temps de stabilisation du mux, comparée à un seuil. Le seuil est une constante **provisoire à mesurer**. Tant qu'il n'est pas caractérisé, le test est signalé « non calibré » (`test_status` = 3).
* Canal « occupé » = courant > seuil **ET** pas de FAULT actif. Un FAULT pendant le test rend le canal non occupé et positionne son bit dans `fault_bits` (invariant 1 : jamais de défaut masqué).
* Résultat : bitmap 12 bits, bit n = canal n. La table canal → entrée mux est une constante du firmware Moteur.

#### 2. Déclenchement
* Le bouton de test de la carte Moteur, **maintenu 3 s**, émet vers le Clavier une trame `IPROPI_SELFTEST_TRIGGER` : ID **0x402**, DLC 2 (`proto_version`, octet réservé 0).
* Le Clavier est **seule autorité** : il émet `IPROPI_SELFTEST_REQUEST` en diffusion : ID **0x401**, DLC 2 (`proto_version`, `request_id` 1..127).
* **Toutes** les cartes (y compris celle du bouton) testent et répondent.
* Le Clavier **refuse** le test pendant le jeu (solénoïde actif ou note récente, délai constant) : STATUS SysEx `SELFTEST_REFUSED_PLAYING`, sans émettre de REQUEST.
* Justification de 0x402 : plage découverte 0x4xx, juste après `UID_REQUEST` (0x400) et `SELFTEST_REQUEST` (0x401). Priorité inférieure aux IDs de jeu 0x100 et de sécurité, donc ne gêne pas le temps réel. Aucun collision avec 0x410..0x412.
* **Variante assumée** de la demande initiale (« la carte émet une requête de diffusion ») : l'orchestration par le Clavier lui permet de savoir qu'un test est en cours et de le refuser pendant le jeu.
* Aucune annonce non sollicitée hors boot.

#### 3. Trame C `BOARD_ANNOUNCE_STATUS` (ID 0x412, DLC 8)
| Octets | Contenu |
|---|---|
| [0..1] | `uid_tag` LE |
| [2..3] | bitmap d'occupation 12 bits LE (bits 12-15 = 0) |
| [4] | `reply_to` (`request_id` 1..127) |
| [5] | `test_status` : 0 OK, 1 refusé occupé, 2 abandonné, 3 non calibré |
| [6..7] | `fault_bits` 12 bits LE (bits 12-15 = 0) |

* Réponse avec le même délai UID-dérivé qu'ADR-0003 (trames A, B, C). Le Clavier attend A+B+C de **toutes** les cartes de `uid_position.h`, avec un timeout constant.
* DLC strict, octets réservés émis à 0 (ADR-0003 §1).

#### 4. Pas d'auto-test au boot
Le Moteur annonce seulement A et B au boot (ADR-0003). L'auto-test et la trame C ne sont émis qu'en réponse à `IPROPI_SELFTEST_REQUEST`. Au boot, le Clavier charge la table flash, envoie `UID_REQUEST` et compare les UID reçus à la table (carte manquante/inconnue => signalé, canaux de la carte refusés) ; il ne compare pas de bitmap. Le bitmap stocké en flash est la dernière référence connue ; il n'est vérifié que lors d'un auto-test demandé, où toute différence avec la table précédente est signalée avant écriture.

#### 5. Table d'assignation (commissioning) en flash, côté Clavier
* Stockage : dernier(s) secteur(s) flash dédiés. En-tête `magic` + `format_version` + `count` + CRC32. Écriture via `flash_safe_execute` (IRQ CAN/USB actives).
* Enregistrement par carte : préfixe `uid_position_entry_t` (inchangé, ADR-0003 §7) + `bitmap_occupation` u16 + champs réservés. **Les touches ne sont pas stockées** : elles sont dérivées de façon déterministe (point 6).
* Écriture **automatique** après auto-test (bouton) réussi, mais **seulement si toutes les cartes listées dans `uid_position.h` ont répondu** avec `test_status` = 0. Sinon : table non écrite, STATUS, l'ancienne table reste.
* Si l'ancienne table existait et diffère : la nouvelle est écrite quand même, mais la différence est signalée explicitement (SysEx STATUS + log CDC). Pas de correction silencieuse.
* Convention de position figée : **position = nœud CAN − 2 (0..4)** ; position 0 = touches les plus GRAVES ; la position la plus haute = touches les plus AIGUËS.
* Le Clavier refuse une table contenant deux `uid_tag` identiques (collision CRC16).

#### 6. Règle d'assignation (descendante, ancrée sur la touche 49)

> **Remplacée par ADR-0010 (2026-10-20, `Accepted`).** La règle d'assignation
> est désormais **ascendante** : ancrage sur la touche 1 = DO2 (position 0,
> canal occupé le plus bas), positions parcourues de 0 à 4. Cette section et ses
> exemples sont conservés **pour l'historique** et ne doivent pas être
> implémentés. Références à jour :
> [ADR-0010](./adr-0010-ancrage-grave-mapping-commissioning.md),
> `docs/specs/CAN.md`, `docs/specs/impact-adr-0010.md`.
* Clavecin 49 touches : touche 1 = DO1 (MIDI 36) … touche 49 = DO5 (MIDI 84). `touche = note_midi − 35` ; hors 36..84 ignoré. (Nommage Yamaha C3 = 60.)
* Les cartes de `uid_position.h` sont parcourues de la position la plus haute vers la plus basse. Chacune reçoit un bloc contigu de touches **en descendant** depuis la touche courante (début = 49), de taille = nombre de canaux occupés. Dans un bloc, les canaux occupés triés par index croissant reçoivent les touches croissantes (canal plus bas = touche plus grave du bloc). Canaux non occupés sautés (trous autorisés).
* Exemple : carte unique en position 4, canaux 0, 1, 2 occupés → canal 0 = touche 47 (MIDI 82, LA#4/SIb4), canal 1 = touche 48 (MIDI 83, SI4), canal 2 = touche 49 (MIDI 84, DO5). Une carte en position 3 aurait son bloc finissant à la touche 46.
* Total ≠ 49 : en bring-up, table valide signalée `TABLE_PARTIAL` (touches sans solénoïde silencieuses, comptées). En configuration finale (5 cartes), total ≠ 49 est une **erreur signalée**.

#### 7. Boot du Clavier
Charge et valide la table flash (CRC/format). Invalide → **aucun solénoïde activé** + signalement. Envoie `UID_REQUEST` et compare les UID reçus à la table : carte manquante / UID inconnu / divergence → signalée, canaux de la carte refusés, **jamais remappés**.

#### 8. `SOLENOID_COMMAND` (livraison 2)
* ID **0x100**, DLC 5 : [0..1] `uid_tag` cible LE ; [2] canal 0..11 ; [3] commande (0 = note off, 1 = note on) ; [4] vélocité 0..127.
* Le Moteur **ignore et compte** toute trame dont `uid_tag` ≠ le sien, tout canal hors 0..11, tout canal non occupé selon le dernier bitmap local. Il applique la machine pull/hold/release du driver, sans contournement (invariant 4).
* Le Clavier résout note MIDI → position → (UID, canal) via la table.
* **Pas d'acquittement applicatif** (reporté, avec les sessions `BOARD_ASSIGN`). Conséquence : une trame perdue = note manquée, visible uniquement par compteur ; un note off perdu est couvert par `SOLENOID_HOLD_MAX_MS` (coupure du driver).

#### 9. Nouveaux STATUS SysEx (type 0x02, plage 0x20+)
> **Superseded par ADR-0005 (2026-10-18)** pour le canal d'affichage diagnostic. Les codes restent la référence sémantique ; seul le transport texte CDC remplace ce format historique.
`20` SELFTEST_STARTED, `21` SELFTEST_REFUSED_PLAYING, `22` BOARD_MISSING, `23` TABLE_WRITTEN, `24` TABLE_NOT_WRITTEN_INCOMPLETE, `25` TABLE_DIFFERS, `26` TABLE_PARTIAL, `27` TABLE_ERROR_FINAL, `28` FLASH_TABLE_INVALID, `29` UID_NOT_IN_TABLE, `2A` CHANNEL_DIVERGENCE, `2B` DUPLICATE_TAG, `2C` SELFTEST_UNCALIBRATED, `2D` BOARD_REFUSED_BUSY, `2E` FAULT_DURING_TEST, `2F` MAINTENANCE_BUSY, `30` MAINTENANCE_NO_TABLE. Détails (bitmaps en nibbles 7 bits) : à préciser par le Documentaliste.

#### 10. Livraisons
1. **Livraison 1** : Moteur (profil PROBE, lecture mux, bitmap, trame C, REQUEST/TRIGGER) + Clavier (réception et affichage SysEx du bitmap). Testable seule.
2. **Livraison 2** : table flash, assignation, `SOLENOID_COMMAND`, test bout en bout.

### Consequences

#### Positive
* **POS-001**: Divergences détectées et signalées, jamais corrigées silencieusement (invariant 1).
* **POS-002**: Moteur anonyme (invariant 2) ; seule vérité côté Clavier (invariant 3).
* **POS-003**: Le test ne peut pas dépasser les bornes du driver (invariant 4).
* **POS-004** (remplacé par ADR-0010) : Ancrage descendant : l'absence d'une carte basse ne décale aucune touche des cartes hautes. La règle ascendante donne la propriété symétrique — l'absence d'une carte **aiguë** ne décale aucune carte grave.
* **POS-005**: Livraison 1 testable sans flash ni commande solénoïde.

#### Negative
* **NEG-001**: Complexité accrue : machine d'états Clavier (REQUEST, A+B+C, timeout).
* **NEG-002**: Détection IPROPI à 20 % : seuil non caractérisé, test « non calibré » en attendant.
* **NEG-003**: Pas d'ack applicatif : notes manquées par perte de trame.
* **NEG-004**: Écriture flash avec IRQ actives : risque de latence / blocage (à vérifier).
* **NEG-005**: Un canal défaillant ou non câblé change l'assignation des touches de toutes les cartes plus graves au prochain test, **signalé mais effectif** (conséquence assumée de la règle compacte). Avec la règle ascendante (ADR-0010), le décalage joue dans l'autre sens — cartes plus **aiguës** — et reste signalé, avec en plus le motif `ANCHOR_SHIFT` quand des positions manquent.
* **NEG-006**: Collision de `uid_tag` (CRC16) possible : table refusée.

### Alternatives Considered

#### Mesure parallèle de tous les canaux, sans mux
* **ALT-001**: **Description**: tous les canaux impulsés simultanément.
* **ALT-002**: **Rejection Reason**: appel de courant 24 V cumulé, un seul ADC, pas de canal indépendant ; le séquentiel est plus sûr.

#### Table recalculée à chaque boot
* **ALT-003**: **Description**: aucune persistance, assignation refaite à chaque démarrage.
* **ALT-004**: **Rejection Reason**: viole l'invariant 3 (table persistante) et le remappage serait implicite.

#### Stockage des touches dans la table
* **ALT-005**: **Description**: table UID → canal → touche écrite en flash.
* **ALT-006**: **Rejection Reason**: redondant avec la règle déterministe, risque d'incohérence bitmap/touches.

#### Ancrage ascendant
* **ALT-007**: **Description**: première carte (position 0) ancrée sur la touche 1.
* **ALT-008**: **Rejection Reason**: remappe silencieusement les touches si une carte manque (invariant 1).
  * **Décision inversée par ADR-0010 (2026-10-20)** : l'ancrage grave est retenu, le décalage restant **signalé** (`MISSING_BOARD`, `ANCHOR_SHIFT`, lignes `[MAP]`) et non silencieux.

#### Auto-test avec mise à jour de la table à chaque boot
* **ALT-009**: **Description**: la table se réécrit à chaque démarrage.
* **ALT-010**: **Rejection Reason**: refusé par l'utilisateur ; réécriture automatique de la table au démarrage interdite.

#### Requête de diffusion émise directement par la carte Moteur
* **ALT-011**: **Description**: le bouton fait émettre la REQUEST par la carte.
* **ALT-012**: **Rejection Reason**: le Clavier ne saurait pas qu'un test est en cours et ne pourrait pas le refuser pendant le jeu.

### Implementation Notes
* **IMP-001**: ADC5 = GPIO45 sur RP2354B ; configurer GPIO45 en entrée analogique, pas de pull. Prévoir une commutation du canal d'ADC cohérente.
* **IMP-002**: `MUX_EN` est actif BAS : activer le mux (0) avant mesure, le désactiver (1) après. Mettre S0..S3 avant d'activer.
* **IMP-003**: Temps de stabilisation du mux avant lecture ADC : constante à régler (à mesurer, 74HC4067 + RC du conditionnement).
* **IMP-004**: Seuil de détection à mesurer sur banc ; constante unique ; « non calibré » tant qu'elle est par défaut.
* **IMP-005**: Alimentation 24 V : le test à 20 % n'est significatif que si 24 V est établie au moment de l'auto-test demandé.
* **IMP-006**: `flash_safe_execute` côté Clavier : vérifier que la désactivation de XIP n'empêche pas l'exécution de l'IRQ PIO0 can2040 (code en flash) ; sinon placer le gestionnaire en RAM (`__not_in_flash_func`). Non prouvé.
* **IMP-007**: Ne pas modifier `uid_position_entry_t` ; nouveaux types dans `shared/protocol/`. Nouveaux IDs 0x401, 0x402, 0x412 dans `can_protocol.h`.
* **IMP-008**: Note confirmée par l'utilisateur : `N_DO1` dans `firmware/clavier/src/keymap.c` vaut bien MIDI 36.
* **IMP-009**: Constante de timeout d'attente A+B+C, délai « jeu récent », durée PROBE et seuil : toutes nommées.

**Tâches ordonnées — Codeur**
1. `shared/protocol` : IDs 0x401/0x402/0x412, encodeurs/décodeurs trames TRIGGER, REQUEST, C ; tests hôte.
2. Moteur : profil PROBE dans `solenoid_profile.h` et `solenoid_driver.c` (écrêtage par le driver).
3. Moteur : lecture mux/ADC, table de mapping, bitmap, `fault_bits`, auto-test, trame C, bouton 3 s.
4. Clavier : émission REQUEST, réception A+B+C, timeout, SysEx STATUS 0x20+, refus pendant le jeu.
5. (Livraison 2) Table flash, assignation, comparaison des UID au boot, `SOLENOID_COMMAND` des deux côtés.

**Tâches ordonnées — Documentaliste**
1. Corriger la Roadmap : sens 49 = DO5, case « Valider PIO/IRQ » obsolète.
2. Mettre à jour `CAN.md` (IDs, état Phase 3a validée), la spec des trames (0x401, 0x402, 0x412, 0x100).
3. Procédure de test dans `tests/` (auto-test, boot, table).

**Modules impactés** : `shared/protocol/` (can_protocol, nouveau format de table), `firmware/clavier/` (can_link, midi/sysex, main, table flash, keymap), `firmware/moteur/` (solenoid_driver, solenoid_profile, can_link, mux/ADC, bouton), `docs/specs/`, `tests/`.

### Questions ouvertes
Aucune à ce stade.

### References
* **REF-001**: [./adr-0001-hal-solenoide-drv8874-phen.md](./adr-0001-hal-solenoide-drv8874-phen.md)
* **REF-002**: [./adr-0002-integration-can2040.md](./adr-0002-integration-can2040.md)
* **REF-003**: [./adr-0003-bringup-can-phase3-lecture-uid-sysex.md](./adr-0003-bringup-can-phase3-lecture-uid-sysex.md)
* **REF-004**: `Netlist/pinmap.generated_drive.h`, `Netlist/Solenoide_drive.xml` (mux IC17)
* **REF-005**: Datasheet 74HC4067 et DRV8874 (IPROPI) ; documentation RP2350 (ADC).

### Addendum 2026-10-18
> **Superseded par ADR-0005 (2026-10-18)** pour le format de sortie diagnostic. Cet addendum reste conservé pour l'historique.

Précision d'interface validée par l'Architecte pour la **Livraison 1** :

* Le STATUS SysEx `0x20+` garde le format
  `F0 7D 43 01 02 <code> <détail> F7`.
* `<détail>` contient **0 ou plusieurs octets 7 bits**, propres à chaque code.
* Règle par défaut : **1 octet `position`** (`0..4`, ou `0x7F` si inconnue).
* Exceptions :
  * `0x2A` `CHANNEL_DIVERGENCE` = `position` + bitmap sur **3 nibbles**
    (`b11..b8`, `b7..b4`, `b3..b0`) ;
  * `0x2D` `BOARD_REFUSED_BUSY` = `position` + bitmap sur **3 nibbles** ;
  * `0x2E` `FAULT_DURING_TEST` = `position` + bitmap sur **3 nibbles**.
* Nouveau SysEx de production `type 0x03` `SELFTEST_REPORT` :
  `F0 7D 43 01 03 request_id position test_status bitmap[3 nibbles] faults[3 nibbles] uid_tag[4 nibbles] F7`
  (nibbles poids fort d'abord).
* Aucun octet des trames CAN décidées plus haut ne change.

### Addendum 2026-10-18 (3) — Trame de diagnostic IPROPI_RAW

Précision d'interface validée par l'Architecte pour la calibration du seuil
IPROPI, **sans changer** la trame C `BOARD_ANNOUNCE_STATUS` :

* Nouveau message CAN `IPROPI_RAW` dans la plage diagnostic :
  **ID `0x600`**, **DLC 8**, sens **Moteur → Clavier**.
* Format :
  * `[0..1]` `uid_tag` LE ;
  * `[2]` `reply_to` = `request_id` logique (`1..127`) ;
  * `[3]` `channel` (`0..11`) ;
  * `[4..5]` `idle_avg` u16 LE = moyenne ADC 12 bits **sans impulsion**,
    mux stabilisé sur ce canal ;
  * `[6..7]` `probe_avg` u16 LE = moyenne ADC 12 bits **pendant** l'impulsion
    PROBE.
* Émission : **12 trames**, une par canal, avec le **même délai initial
  UID-dérivé** que A/B/C, puis **espacées après la trame C**.
* Condition d'émission : **uniquement** si l'auto-test a été demandé avec le
  **flag calibration**.
* Activation du mode calibration :
  * `IPROPI_SELFTEST_REQUEST` **garde DLC 2** :
    `[proto_version, request_id]` ;
  * le flag est porté par `request_id | 0x80` ;
  * le `request_id` logique reste `1..127` sur **7 bits** et `reply_to`
    réutilise seulement cette valeur logique.
* Côté Clavier, le mode calibration est activé par une **constante de
  compilation** `CLAVIER_SELFTEST_CALIBRATION` (option CMake, défaut `OFF`).
  Hors build dédié, le comportement ne change pas.
* Moyennage :
  * `probe_avg` = moyenne sur `IPROPI_SELFTEST_RAW_SAMPLES` échantillons lus
    pendant PROBE à partir de `IPROPI_SELFTEST_ADC_SAMPLE_DELAY_US` ;
  * `idle_avg` = moyenne relevée juste avant l'impulsion, mux stabilisé.
* Garde d'exécution : la fenêtre d'échantillonnage pendant PROBE doit rester
  **strictement inférieure** à `SOLENOID_PROBE_DURATION_MS`, par assertion
  statique ou garde explicite dans le firmware.
* Le mode calibration **ne change ni le seuil, ni la logique d'occupation**.
  Ce point historique a été **supersédé par l'addendum (4)** : le firmware
  courant applique désormais un seuil delta explicite et n'émet plus
  `UNCALIBRATED`.
* Journal CDC côté Clavier :
  * `[SELFTEST_RAW] req=R pos=N ch=C idle=I probe=P`
  * `[SELFTEST_RAW] req=R pos=N ch=C delta=D` avec `D = probe - idle` signé
* Note de format : `SELFTEST_STARTED` est un **code sans carte** ; la ligne
  CDC devient `[STATUS] code=SELFTEST_STARTED` (plus de `pos=127`).

### Addendum 2026-10-18 (4) — Seuil unique figé et observation matérielle

Décision humaine du 2026-10-18 pour la Livraison 1 :

* le firmware Moteur applique un **seuil unique de compilation**
  `IPROPI_SELFTEST_THRESHOLD_DELTA_ADC = 400`, identique sur toutes les cartes ;
* le critère d'occupation devient `delta = max(probe_avg - idle_avg, 0)`,
  puis `occupé <=> delta >= 400` ;
* `idle_avg` reste toujours mesuré avant l'impulsion et `probe_avg` reste
  toujours moyenné sur `IPROPI_SELFTEST_RAW_SAMPLES` échantillons, y compris
  hors mode calibration ;
* le statut protocolaire `UNCALIBRATED` reste réservé pour compatibilité,
  mais **n'est plus émis** avec ce réglage figé ;
* le profil **PROBE 3 ms / 20 %** de `solenoid_profile.h` est
  **validé empiriquement 2026-10-18**.

Observation de banc à ne pas masquer par le firmware :

* les mesures de la **carte 1** (canaux 6, 7, 9, 10, 11 avec `idle_avg`
  compris entre **136 et 552**) indiquent un **défaut matériel suspect à
  l'entrée du mux** ;
* ce comportement doit rester **signalé** par les mesures et le bitmap, sans
  compensation logicielle ni remappage implicite.

### Addendum 2026-10-18 (5) — Livraison 2 : détails d'interface

Précisions minimales figées pour la **Livraison 2**.

#### 1. `SOLENOID_COMMAND` (`0x100`)

* **ID `0x100`**, **DLC 5**, sens **Clavier → toutes les cartes Moteur**.
* Format :
  * `[0..1]` `uid_tag` LE ;
  * `[2]` `channel` (`0..11`) ;
  * `[3]` `command` (`0 = off`, `1 = on`) ;
  * `[4]` `velocity` (`0..127`).
* Décodage à **DLC strict**.
* Le Moteur **ignore et compte** :
  * `uid_tag` différent ;
  * DLC faux ;
  * canal `> 11` ;
  * canal non occupé selon son **dernier bitmap local** ;
  * toute commande reçue pendant un auto-test en cours.
* **Choix minimal pour l'absence de bitmap local au boot** : le Moteur démarre
  avec un bitmap local « **tous les canaux gérables sont autorisés** »
  (typiquement `0xFFF` pour 12 drivers câblés), puis remplace ce bitmap par le
  résultat du dernier auto-test **réussi**. Risque assumé : avant le premier
  auto-test, une commande vers un canal physiquement vide n'est filtrée que par
  la **table persistante du Clavier** et par la **couche pilote** ; l'invariant
  4 reste inchangé, car les bornes électriques restent appliquées uniquement
  dans `solenoid_driver`.
* **Pas d'acquittement applicatif** : une perte de trame = note manquée,
  comptée seulement.

#### 2. Format flash de la table côté Clavier

* Stockage dans **1 secteur 4 Kio** situé **en fin de flash** :
  `offset = PICO_FLASH_SIZE_BYTES - 4096`.
* Lecture **directe XIP** ; écriture via `flash_safe_execute`.
* En-tête :
  * `magic` = `'CLVC'` (`u32`) ;
  * `format_version` (`u16`) ;
  * `count` (`u16`) ;
  * `crc32` (`u32`) calculé **uniquement sur les enregistrements**.
* Enregistrement par carte :
  * préfixe `uid_position_entry_t` **inchangé** ;
  * `bitmap_occupation` (`u16`) ;
  * `reserved` (`u16`).

#### 3. Point IMP-006 (`flash_safe_execute` / IRQ `PIO0_IRQ_0`)

Vérification effectuée dans le **SDK 2.3.1** et dans le build courant :

* `flash_safe_execute()` (lib `pico_flash`) entre en zone sûre en appelant
  `save_and_disable_interrupts()` sur le cœur courant ; dans la configuration
  actuelle du Clavier (super-boucle **mono-cœur**, pas de `pico_multicore`,
  pas de cœur 1 lancé), cela suffit à empêcher l'exécution de `PIO0_IRQ_0`
  pendant l'effacement / l'écriture.
* Le gestionnaire `can2040_pio_irq_handler()` reste **en flash**
  (`.text.can2040_pio_irq_handler` dans les fichiers `.map/.dis`) ; **ce n'est
  pas un problème ici**, précisément parce que les IRQ sont désactivées pendant
  `flash_safe_execute`.
* La callback de mutation flash utilisée par le Clavier doit rester en
  **RAM** (`__not_in_flash_func`) ; c'est la solution minimale retenue.
* Si une évolution future active le **cœur 1**, alors cette conclusion ne
  tient plus automatiquement : il faudra participer explicitement au protocole
  `flash_safe_execute_core_init()` (ou équivalent) avant de conserver cette
  écriture in situ.

#### 4. Journal CDC

* Réutilisation des codes `STATUS` de la section 9 :
  * `[STATUS] code=NOM`
  * `[STATUS] code=NOM pos=N`
  * `[STATUS] code=NOM pos=N bitmap=0xXYZ`
* Lignes de table :
  * `[TABLE] loaded ...`
  * `[TABLE] invalid ...`
  * `[TABLE] pos=N uid=XXXXXXXXXXXXXXXX bitmap=0xXYZ keys=a-b`
* Lignes de résolution de note :
  * `[NOTE] midi=M key=K pos=P ch=C`
  * `[NOTE_DROPPED] midi=M reason=...`
* Les lignes `[NOTE]` sont **désactivées par défaut** et ne sont émises que si
  `CLAVIER_NOTE_LOG != 0`. Les rejets restent comptés et journalisés.
* Le mode maintenance (ADR-0006) ajoute aussi :
  * `[MODE] maintenance=ON|OFF`
  * `[PUSH] midi=M key=K pos=P ch=C start|stop|timeout`

#### 5. Règle d'assignation figée pour le calcul pur

> **Remplacée par ADR-0010 (2026-10-20, `Accepted`).** Le parcours est désormais
> **ascendant** depuis la touche 1 (`DO2`), positions `0 → 4`. Les exemples
> ci-dessous (ancrés sur la touche 49) sont **historiques** ; les vecteurs à
> utiliser sont dans `docs/specs/impact-adr-0010.md`.

* `touche = note_midi - 35`.
* Seules les notes MIDI `36..84` sont routables ; hors plage : ignorées.
* Parcours des cartes par **position décroissante** (`4 → 0`).
* Chaque carte reçoit un **bloc contigu descendant** depuis la touche courante,
  initialement **49**.
* Dans un bloc, les **canaux occupés triés par index croissant** reçoivent les
  **touches croissantes** du bloc.
* Les trous de bitmap sont autorisés.
* `total_occupied != 49` reste admis en bring-up et doit être signalé par
  `TABLE_PARTIAL`.
* Deux `uid_tag` identiques rendent la table **invalide** (`DUPLICATE_TAG`).

Exemples normatifs :

* position 4, bitmap `0x00B` (`0,1,3`) :
  * canal 0 → touche 47 ;
  * canal 1 → touche 48 ;
  * canal 3 → touche 49.
* position 4, bitmap `0x841` (`0,6,11`) :
  * canal 0 → touche 47 ;
  * canal 6 → touche 48 ;
  * canal 11 → touche 49.
* positions 4 puis 3, avec `3` canaux puis `2` canaux occupés :
  * la carte de position 4 reçoit `47,48,49` ;
  * la carte de position 3 reçoit `45,46`, donc ses canaux occupés les plus
    bas sont affectés dans cet ordre.