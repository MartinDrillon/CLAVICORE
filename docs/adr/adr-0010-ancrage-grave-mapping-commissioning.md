---
title: "ADR-0010: Ancrage du mapping de commissioning sur la note la plus grave — position 0, canal 0 = DO2 (MIDI 36)"
status: "Accepted"
date: "2026-10-20"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

**Remplace partiellement ADR-0004** : sa « décision §6 — Règle d'assignation
(descendante, ancrée sur la touche 49) » et les alternatives ALT-007/ALT-008
(ancrage sur la touche 1, rejeté), ainsi que POS-004 et NEG-005 qui en
découlent. Le reste d'ADR-0004 (auto-test IPROPI, trame `SOLENOID_COMMAND`,
format de table) reste en vigueur.

**Amende partiellement ADR-0009 §3** : nouveau motif `0x40 ANCHOR_SHIFT` dans
le champ `flags` existant (bit jusque-là réservé). Aucune autre règle d'ADR-0009
n'est modifiée.

Aucune dérogation à un invariant d'`AGENTS.md` :

* **invariant 1** : l'ancrage inversé reste une règle déterministe appliquée au
  *cliché du dernier auto-test demandé* ; tout décalage qu'il provoque est
  **signalé** (motifs `MISSING_BOARD` / `ANCHOR_SHIFT`, état `PROVISIONAL`,
  ligne `[MAP]` par canal à chaque boot). Aucune correction silencieuse, aucune
  réattribution automatique ;
* **invariant 2** : les cartes Moteur restent anonymes ; rien ne change côté
  Moteur ;
* **invariant 3** : la table persistée reste la seule source de vérité ; les
  données stockées ne changent pas (aucune touche n'y est écrite) ;
* **invariant 5** : la règle d'assignation est une interface de
  `shared/protocol` (`commissioning_build_runtime`) ; c'est l'objet de la
  présente ADR, écrite avant l'implémentation.

### Context

**Demande humaine (2026-10-20).** « Lier la première channel occupée de la
carte moteur 1 à la note 1 du clavecin, c'est-à-dire DO2 / MIDI 36. Le premier
solénoïde (canaux 0 → 11) de la carte 1 de la chaîne CAN joue DO2 ; on remplit
ensuite les canaux dans l'ordre des canaux puis des cartes. Les choses se
passent aujourd'hui dans le sens inverse, en partant de la touche 49. »

**Ce que fait le code aujourd'hui.** `commissioning_build_runtime`
(`shared/protocol/commissioning_table.c:130-195`) parcourt les positions de
**4 vers 0**, avec `current_high = COMMISSIONING_KEY_MAX` (49) ; chaque carte
reçoit un bloc **contigu descendant** (`block_start = current_high - N + 1`),
puis `current_high = block_start - 1`. Les canaux occupés, triés par index
croissant, reçoivent les touches croissantes du bloc. `key = midi - 35`
(`commissioning_table.c:47-58`), donc touche 1 = MIDI 36 = DO2 et touche 49 =
MIDI 84 = DO6 (ADR-0008).

**Effet constaté avec le seed actuel.** `firmware/clavier/src/uid_position.h`
laisse la **position 0 vide** (`:20`) et seede les positions 1 à 4. Le
remplissage compact partant de la touche 49 place le « manque » à l'extrémité
grave : la carte de position 0 (nœud 2, première de la chaîne) ne joue rien, la
carte de position 1 commence à la touche 2 (DO#2) si toutes les cartes sont
pleines, la touche 1 (DO2) n'est attribuée à personne et la position la plus
haute possède la touche 49. D'où la perception — exacte — d'un ancrage sur la
touche 49.

**Pourquoi ce n'était pas un oubli.** ADR-0004 §5 fige « position = nœud − 2,
position 0 = touches les plus graves », et §6 choisit explicitement l'ancrage
descendant ; ALT-008 rejette l'ancrage sur la touche 1 au motif qu'il
« remappe silencieusement les touches si une carte manque (invariant 1) ». La
présente ADR inverse ce choix : elle doit donc traiter ce risque, pas le nier.
Le remappage n'est pas *silencieux* — il est signalé à l'écriture et à chaque
boot (`[TABLE]`, `[MAP]`, ADR-0009 §4) — mais il est *effectif*, exactement
comme l'était déjà le décalage symétrique accepté en NEG-005 d'ADR-0004.

**Contraintes physiques qui pèsent dans le choix.** Le câblage est figé : le
solénoïde du canal 0 de la carte 1 est monté sur la touche DO2. Le mapping ne
doit donc pas dépendre de la présence des autres cartes, et l'excédent de
canaux (5 × 12 = 60 > 49) doit tomber du côté aigu, pas du côté grave.

**Ce qui doit rester inchangé.** `keymap.c` est indépendant : ses positions
1..84 sont des positions de matrice, sans lien avec les positions de carte ni
avec les touches de commissioning. Le seul lien est la note MIDI
(`N_DO2 = KEYCODE_STORAGE_NOTE(36)`, `keycode.h`). Aucune trame CAN ne change :
`SOLENOID_COMMAND` ne transporte que `channel` (jamais la touche), et le Moteur
indexe `driver[channel]` sans connaître la musique.

### Decision

#### 1. Ancrage grave, remplissage croissant

La règle d'assignation devient **strictement le miroir** de l'ancienne :

* Ancre : `current_low = COMMISSIONING_KEY_MIN` (touche 1 = MIDI 36 = DO2).
* Les cartes sont parcourues par **position croissante** (0 → 4).
* Chaque carte reçoit un bloc contigu de taille = `popcount(occupation_bitmap)` :
  `block_start = current_low`, `block_end = current_low + N - 1`, puis
  `current_low = block_end + 1`.
* Dans un bloc, les **canaux occupés triés par index croissant** reçoivent les
  **touches croissantes** du bloc (canal plus bas = touche plus grave).
  Canaux non occupés sautés (trous autorisés), comme aujourd'hui.
* Les touches non attribuées (total occupé < 49) sont **celles du haut** (aigu),
  et restent signalées `NOT_49_KEYS` / `TABLE_PARTIAL`.

Conséquence directe : **carte de position 0 (nœud 2), canal occupé le plus bas
→ touche 1 → MIDI 36 → DO2**. C'est l'exigence de l'utilisateur, littéralement.

#### 2. Aucun changement de format ni de trame

* `commissioning_entry_t`, `commissioning_note_target_t`,
  `commissioning_runtime_t` : **inchangés**. `position_first_key` /
  `position_last_key` gardent leur sens (bornes du bloc d'une position), ils
  sont simplement calculés en croissant.
* Format flash **version 2 inchangé** : les touches ne sont pas stockées, la
  table persistée reste valide et aucune migration n'est nécessaire. Le mapping
  est recalculé au chargement ; le changement est visible dans les lignes
  `[MAP]`.
* Trames CAN **inchangées** ; `firmware/moteur/` **non touché** ;
  `firmware/clavier/src/keymap.c` **non touché**.

#### 3. Contiguïté depuis la position 0 et nouveau motif `ANCHOR_SHIFT`

Avec un ancrage grave, une position absente (ou non déclarée) **décale vers le
haut** toutes les cartes suivantes : c'est le prix du choix, symétrique de
NEG-005 d'ADR-0004. Pour que ce décalage ne soit jamais tacite :

* Une table n'est `FINAL` que si les positions présentes sont **exactement
  0..K−1** (aucun trou avant la position présente la plus haute). Autrement dit
  la carte de l'extrémité grave doit être déclarée en position 0.
* Sinon l'assignation est **quand même construite** (remplissage compact
  croissant des positions présentes) — un test de banc avec une seule carte
  doit rester possible (ADR-0009 §2) — mais la table est `PROVISIONAL` avec le
  motif **`0x40 ANCHOR_SHIFT`** (bit 6, jusque-là réservé, émis à 0 par les
  versions antérieures ; **pas** de changement de `format_version`).
* `ANCHOR_SHIFT` s'ajoute aux motifs existants (`0x01`…`0x20`), sans les
  modifier, et se signale comme eux : ligne `[TABLE] state=PROVISIONAL
  flags=0x40`.
* Aucun refus de jouer, aucune réattribution, aucun réordonnancement caché
  au boot (invariant 1).

#### 4. Seed : la carte grave doit être en position 0

`firmware/clavier/src/uid_position.h` doit déclarer en **position 0** la carte
physiquement à l'extrémité grave (nœud 2 de la chaîne). Tant que la position 0
est vide (état actuel du fichier), la configuration est `ANCHOR_SHIFT` et
l'utilisateur voit dans `[MAP]` que DO2 est joué par une autre position. C'est
un signalement, pas une panne.

#### 5. Exemples de référence (tests hôte)

* Une carte en position 0, bitmap `0x00B` (canaux 0, 1, 3) : canal 0 → touche 1
  (DO2, 36), canal 1 → touche 2 (37), canal 3 → touche 3 (38).
* Positions 0 puis 1, avec `3` puis `2` canaux occupés : position 0 →
  touches 1, 2, 3 ; position 1 → touches 4, 5.
* Une carte isolée en position 2, bitmap `0x001` : canal 0 → touche 1
  (DO2, 36), état `PROVISIONAL`, `flags` contient `0x40`.

### Consequences

#### Positive
* **POS-001**: La commande physique de l'instrument correspond au câblage : carte 1 canal 0 = DO2, puis remplissage dans l'ordre des canaux et des cartes, comme demandé.
* **POS-002**: L'absence ou le retrait d'une carte **aiguë** ne décale plus aucune carte grave — c'est la carte DO2.. qui est la plus critique pour le jeu, et elle reste stable.
* **POS-003**: Aucun changement de trame CAN, de format flash, de firmware Moteur ni de keymap : le correctif est localisé à une fonction de `shared/protocol` plus les tests et la documentation.
* **POS-004**: Le risque de décalage est explicitement traité : motif `ANCHOR_SHIFT`, état `PROVISIONAL`, lignes `[MAP]` à chaque boot (invariant 1 tenu).

#### Negative
* **NEG-001**: Symétrie retournée : si la carte grave est absente, une carte non déclarée ou un trou de position, **toutes** les cartes plus aiguës sont décalées. Signalé (`ANCHOR_SHIFT`), mais effectif — c'est exactement l'objection ALT-008 d'ADR-0004, réduite au statut d'information.
* **NEG-002**: Une table déjà persistée change de comportement **sans aucune écriture** : la même table joue d'autres notes après mise à jour du Clavier. Elle doit être relue (`[MAP]`) et, en cas de doute, régénérée par un auto-test demandé.
* **NEG-003**: Beaucoup de documents et de vecteurs de test figent la règle descendante (ADR-0004 §6 et son addendum (5), `docs/specs/CAN.md`, `docs/specs/Roadmap.md`, `tests/phase3b_bout_en_bout.md`, `tests/host/test_phase3_helpers.c`) ; tous doivent être corrigés dans le même cycle, sinon un test « vert » validera l'ancienne règle.
* **NEG-004**: Avec 49 touches et 5 cartes de 12 canaux, la dernière carte ne peut porter qu'une partie des touches aiguës ; la répartition exacte dépend du nombre de canaux occupés par carte (règle compacte, pas de fenêtre fixe).

### Alternatives Considered

#### Conserver l'ancrage descendant et inverser les numéros de position dans le seed
* **ALT-001**: **Description**: laisser `commissioning_build_runtime` tel quel et déclarer la carte grave en position 4, la carte aiguë en position 0.
* **ALT-002**: **Rejection Reason**: contredit la convention figée « position = nœud − 2, position 0 = graves » (ADR-0004 §5, `uid_position.h`), rend la chaîne physique illisible et inverse le sens des `[MAP]` ; corrige un symptôme, pas la règle.

#### Fenêtres fixes de 12 touches par position
* **ALT-003**: **Description**: position `p` possède toujours les touches `12p+1 .. 12p+12` (dernière carte tronquée à 49) ; un canal non occupé laisse sa touche silencieuse et ne décale personne.
* **ALT-004**: **Rejection Reason**: introduit une dépendance dure au découpage 12 canaux/carte et une règle de troncature asymétrique pour la 5e carte ; ne correspond pas à la demande (« on remplit les canaux dans l'ordre ») et ajoute un mode d'échec (touches muettes) au lieu d'un signalement. Retenable plus tard si les décalages de NEG-001 deviennent gênants.

#### Refuser de construire ou de jouer une table sans position 0
* **ALT-005**: **Description**: `COMMISSIONING_MISSING_ANCHOR` : table refusée tant que la position 0 n'est pas déclarée et répondante.
* **ALT-006**: **Rejection Reason**: bloque le bring-up à une seule carte sur le banc, que ADR-0009 §2 autorise explicitement ; un signalement (`0x40`) suffit et respecte mieux la logique « état informatif, pas verrou ».

#### Stocker les touches dans la table
* **ALT-007**: **Description**: remplacer la règle déterministe par `uid → canal → touche` écrit en flash (déjà écarté par ADR-0004 ALT-005).
* **ALT-008**: **Rejection Reason**: redondance avec `occupation_bitmap`, risque d'incohérence, et aucun besoin nouveau : la règle croissante suffit à exprimer la demande.

#### Ancrer chaque carte à son numéro de nœud (touche = 12 × (nœud − 2) + 1 + canal)
* **ALT-009**: **Description**: lier directement la géométrie du chaînage à la touche, sans passer par la table.
* **ALT-010**: **Rejection Reason**: réintroduit un rôle musical dans le câblage (contraire à l'invariant 2 côté Moteur et à l'invariant 3 : le rôle doit vivre dans la table), et casse le remplacement d'une carte sans renumérotation.

### Implementation Notes
* **IMP-001**: `shared/protocol/commissioning_table.c:130-195` : inverser la boucle (`position` de 0 à `UID_POSITION_COUNT - 1`) et le sens de remplissage (`current_low` initialisé à `COMMISSIONING_KEY_MIN`, `block_start = current_low`, `current_low = block_end + 1`). Conserver le garde `COMMISSIONING_TOO_MANY_KEYS` (`total_occupied > 49` ou `block_end > COMMISSIONING_KEY_MAX`). Ne toucher ni `commissioning_lookup_midi_note`, ni les encodeurs/décodeurs flash.
* **IMP-002**: Mettre à jour les vecteurs de test hôte et la procédure `tests/phase3b_bout_en_bout.md` (§5 de la présente ADR) ; ajouter un cas « trou de position » vérifiant `total_occupied`, `position_first_key/last_key` et l'absence de `key` hors 1..49.
* **IMP-003**: Motif `0x40 ANCHOR_SHIFT` : le définir dans `shared/protocol/commissioning_table.h` à côté des motifs d'ADR-0009 (§3) et le calculer côté Clavier au moment de construire l'état de table, comme les autres motifs (positions présentes ≠ `0..K−1`). Aucune promotion manuelle ; le motif disparaît au premier test conforme.
* **IMP-004**: `firmware/clavier/src/uid_position.h` : corriger le commentaire (`:10-12`, `:24`) pour décrire l'ancrage grave et le remplissage croissant ; l'utilisateur y déclare la carte grave en position 0 (aujourd'hui vide). Ce fichier reste des données de seed, jamais une source de vérité.
* **IMP-005**: Documentaliste : `docs/specs/CAN.md` « Règle descendante » → « Règle ascendante » (texte et exemples), `docs/specs/Roadmap.md` l. 127-129 et 164-165, et marquer ADR-0004 §6 / ALT-007-008 / POS-004 / NEG-005 comme remplacés. Aucun schéma KiCad, aucun GPIO, aucune trame à modifier.

### Modules impactés
`shared/protocol` (`commissioning_table.[ch]`), `firmware/clavier/src/uid_position.h` (commentaire + seed utilisateur), `firmware/clavier` (calcul du motif `ANCHOR_SHIFT`, `[TABLE]`), `tests/`, `docs/specs/`. **Non touchés** : `firmware/moteur/`, `shared/protocol/can_protocol.[ch]`, `firmware/clavier/src/keymap.c`, format flash v2.

### References
* **REF-001**: [ADR-0004](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) — §5 (convention de position), §6 (règle remplacée), POS-004, NEG-005, ALT-007/ALT-008.
* **REF-002**: [ADR-0009](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) — §1-§4 (snapshot, écriture assouplie, états et motifs, signalement).
* **REF-003**: [ADR-0008](./adr-0008-convention-nommage-octaves-notation-scientifique.md) — `DO2(36)` … `DO6(84)`, `key = midi − 35`.
* **REF-004**: `shared/protocol/commissioning_table.[ch]`, `firmware/clavier/src/{uid_position.h,keymap.c,can_link.c}`, `docs/specs/CAN.md` (Règle d'assignation des touches), `docs/specs/Roadmap.md`.
