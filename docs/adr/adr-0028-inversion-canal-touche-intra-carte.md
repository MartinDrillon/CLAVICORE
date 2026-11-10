---
title: "ADR-0028: Inversion intra-carte du sens canal → touche — canal occupé le plus haut = note la plus grave du bloc"
status: "Accepted"
date: "2026-10-28"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

**Amende partiellement ADR-0010** : le point « les canaux occupés triés par
index croissant reçoivent les touches croissantes du bloc » de sa §1 (décision)
et ses exemples de référence §5. **Reste en vigueur** : ancrage grave, parcours
des positions `0 → 4`, blocs contigus croissants, touches non attribuées côté
aigu, contiguïté des positions et motif `0x40 ANCHOR_SHIFT`, format flash v2,
aucune trame CAN nouvelle. Aucune autre ADR n'est touchée.

Aucune dérogation à un invariant d'`AGENTS.md` :

* **invariant 1** : la règle reste déterministe et appliquée au *cliché du
  dernier auto-test demandé* ; les signalements existants (`MISSING_BOARD`,
  `ANCHOR_SHIFT`, `NOT_49_KEYS`/`TABLE_PARTIAL`, lignes `[MAP]`, `[CARD]` à
  chaque boot) sont inchangés dans leur déclenchement comme dans leur texte.
  Aucune correction silencieuse, aucune réattribution automatique ;
* **invariant 2** : les cartes Moteur restent anonymes ; `firmware/moteur/` et
  `shared/protocol/can_protocol.[ch]` ne portent aucune hypothèse d'ordre entre
  canal et touche (le Moteur indexe `driver[channel]`, `SOLENOID_COMMAND` ne
  transporte que `channel`). Aucune trame n'est modifiée ;
* **invariant 3** : la table persistante côté Clavier reste la seule source de
  vérité ; aucune touche n'y est stockée, **format flash v2 inchangé**, aucune
  migration ;
* **invariant 5** : la règle d'assignation est une interface de
  `shared/protocol` (`commissioning_build_runtime`) ; c'est l'objet de la
  présente ADR, écrite **avant** l'implémentation.

Aucun doute de sécurité électrique : la décision ne touche ni courant, ni durée,
ni budget thermique, ni profil d'activation (ADR-0014).

### Context

**Demande humaine (2026-10-28).** « Inverser l'ordre des notes MIDI par rapport
à la position des channels PWM des cartes moteurs : la channel 11 doit jouer la
note la plus grave gérée par la carte, la channel 0 la plus aiguë. C'est
simplement à cause de la disposition physique des cartes dans le clavecin. »

**Ce que fait le code aujourd'hui.** `commissioning_build_runtime`
(`shared/protocol/commissioning_table.c:159-231`) parcourt les positions `0 → 4`
(ADR-0010) et donne à chaque carte un bloc contigu croissant
(`block_start = current_low`, `block_end = block_start + N − 1`). Dans ce bloc,
la boucle `for (channel = 0 … 11)` (`:204-224`) attribue au canal occupé
d'index **croissant** la touche **croissante** suivante (`key = block_start +
channel_index`, `:211`). Conséquence : canal 0 = note la plus grave du bloc.

**Pourquoi c'est faux sur l'instrument.** Les cartes Moteur sont montées dans le
clavecin de sorte que, **dans le bloc de touches d'une carte**, le solénoïde du
canal PWM 11 soit physiquement du côté grave et le canal 0 du côté aigu. Le
câblage physique a donc l'ordre canal ↔ touche **inversé** à l'intérieur de
chaque carte. La disposition est un fait de montage, pas une préférence : la
règle actuelle joue chaque bloc à l'envers, sans qu'aucun signalement ne le
révèle (le mapping est « correct » au sens de la règle, donc muet).

**Ce qui ne doit pas bouger.** L'ordre **inter-cartes** est celui d'ADR-0010 et
reste juste : position 0 = carte physiquement grave, positions parcourues
`0 → 4`, blocs contigus croissants, excédent de canaux et touches non attribuées
côté aigu, `ANCHOR_SHIFT` inchangé. Seule la règle **intra-carte** change.

### Decision

#### 1. Balayage décroissant des canaux dans le bloc (option A, retenue)

Dans un bloc de taille `N = popcount(occupation_bitmap)` commençant à
`block_start` :

* les **canaux occupés** sont parcourus par **index décroissant** (`11 → 0`) ;
* le *k*-ième canal occupé rencontré reçoit la *k*-ième **touche croissante** du
  bloc : `key = block_start + k` ;
* donc, **tous canaux occupés** : canal 11 → `block_start` (note la plus grave
  du bloc), canal 0 → `block_start + 11` (la plus aiguë) ;
* les canaux non occupés sont sautés : le bloc reste **compact**, de taille
  `N`. Conséquence assumée et documentée : la propriété « canal 11 = plus grave »
  n'est littéralement vraie que si le canal 11 est **occupé** ; sinon c'est le
  canal occupé le plus haut qui prend la première touche.

Rien d'autre ne change : `position_first_key` / `position_last_key` gardent leur
sens (bornes du bloc), le garde `COMMISSIONING_TOO_MANY_KEYS` est conservé, et
aucune touche ne peut sortir de `1..49`.

#### 2. Justification du choix A contre la formule fixe (option B)

L'option B (`key = block_start + (11 − channel)`) rend la propriété
« canal 11 = plus grave » vraie par construction, mais **décale le bloc à la
taille du bitmap** : avec `bitmap = 0x00B` (canaux 0, 1, 3), elle attribuerait
les touches `block_start+8`, `+10`, `+11` et laisserait 8 touches **muettes**
au milieu du bloc, sans aucun motif d'erreur existant pour les signaler. C'est
un nouveau mode d'échec silencieux (invariant 1) et une régression par rapport à
la règle compacte actuelle, pour un gain nul sur l'instrument réel (les cartes
pleines sont le cas normal ; les trous sont l'exception d'auto-test).
L'option A conserve la compacité, ne crée aucun mode d'échec et reste lisible
dans `[MAP]`.

#### 3. Ce qui reste explicitement inchangé

* Parcours des positions `0 → 4`, blocs contigus croissants, ancre
  `COMMISSIONING_KEY_MIN`, touches non attribuées côté **aigu**.
* `ANCHOR_SHIFT` (`0x40`) et tous les autres motifs : mêmes conditions, même
  texte, `FINAL` ⇔ `flags == 0`.
* `commissioning_entry_t`, `commissioning_note_target_t`,
  `commissioning_runtime_t`, `commissioning_lookup_midi_note`, encodeurs et
  décodeurs flash. **Format v2 inchangé, aucune migration.**
* Trames CAN, `firmware/moteur/`, `shared/protocol/can_protocol.[ch]`,
  `firmware/clavier/src/keymap.c`, `uid_position.h` (données de seed).
* Limites électriques, profil d'activation, sécurité (ADR-0014) : hors sujet.

#### 4. Exemples de référence (tests hôte)

* Une carte seule, bitmap `0x00B` (canaux 0, 1, 3), bloc `1..3` :
  canal 3 → touche 1 (DO2, 36), canal 1 → touche 2 (37), canal 0 → touche 3
  (38). Touche 4 non trouvée.
* Une carte seule, bitmap `0x841` (canaux 0, 6, 11), bloc `1..3` :
  canal 11 → touche 1 (36), canal 6 → touche 2 (37), canal 0 → touche 3 (38).
* Deux cartes, position 0 = `0x00B` (3 canaux), position 1 = `0x003`
  (canaux 0, 1) : position 0 → touches `1,2,3` avec canal 3 → 1, canal 1 → 2,
  canal 0 → 3 ; position 1 → touches `4,5` avec canal 1 → 4, canal 0 → 5.
* Carte pleine (12 canaux), bloc `1..12` : canal 11 → touche 1 (DO2, 36),
  canal 0 → touche 12 (47).
* Trou de position inchangé (ADR-0010) : position 0 = 1 canal, position 2 =
  1 canal → touches `1,2`, `PROVISIONAL`, `flags` contient `0x40` ; ici
  `channel = 0` dans les deux cas, donc aucun effet de l'inversion.
* Ligne `[CARD]` correspondante (paires alignées par index, canaux **toujours**
  listés par index croissant, touches devenues décroissantes) :
  `[CARD] pos=4 node=6 … bitmap=0x00B channels=0,1,3 keys=3,2,1
  notes=RE2(38),DO#2(37),DO2(36)`.

### Consequences

#### Positive
* **POS-001**: La règle correspond au montage physique : dans chaque bloc, le canal PWM le plus haut est la note la plus grave, le canal 0 la plus aiguë, comme demandé.
* **POS-002**: Le changement est localisé à la boucle interne de `commissioning_build_runtime` et aux helpers d'affichage : aucune trame, aucun format flash, aucun firmware Moteur, aucun keymap.
* **POS-003**: La règle reste compacte (aucune touche muette dans un bloc) et conserve tous les signalements existants ; invariant 1 tenu.
* **POS-004**: L'ordre inter-cartes d'ADR-0010, l'ancrage sur DO2 en position 0 et `ANCHOR_SHIFT` restent valides : aucune régression sur les cas déjà couverts.

#### Negative
* **NEG-001**: Une table déjà persistée change de comportement **sans écriture** : la même table joue d'autres notes après mise à jour du Clavier. Les lignes `[MAP]` / `[CARD]` émises à chaque boot rendent le nouveau mapping lisible ; en cas de doute, régénérer par un auto-test demandé.
* **NEG-002**: La propriété « canal 11 = plus grave » n'est vraie que si le canal 11 est occupé ; avec un bitmap lacunaire, c'est le canal occupé le plus haut qui l'est. C'est le prix de la compacité (choix §2).
* **NEG-003**: Les listes `keys` / `notes` de `[CARD]` deviennent décroissantes alors que `channels` reste croissant : le lecteur ne peut plus supposer « deux listes croissantes » ; les paires restent alignées par index et `[MAP]` donne le détail canal par canal.
* **NEG-004**: Plusieurs vecteurs de test et documents figent l'ancien sens intra-carte (`tests/host/test_phase3_helpers.c`, `tests/phase3b_bout_en_bout.md`, `tests/bringup_commissioning_provisoire.md`, `docs/specs/CAN.md`, `Detail_architecture.md`, `Roadmap.md`, `impact-adr-0010.md`, `ARCHITECTURE_CURRENT.md`) : tous doivent être corrigés dans le même cycle, sinon un test « vert » validera l'ancienne règle.

### Alternatives Considered

#### Formule fixe `key = block_start + (11 − channel)`
* **ALT-001**: **Description**: chaque canal a une place fixes dans un bloc de 12 touches, indépendamment de l'occupation.
* **ALT-002**: **Rejection Reason**: introduit des touches muettes au milieu des blocs sans motif d'erreur disponible (nouveau mode d'échec silencieux, invariant 1) ; casse la règle compacte et la propriété « total occupé = touches attribuées ». Voir §2.

#### Inverser les canaux matériellement (réaffecter le câblage PWM)
* **ALT-003**: **Description**: garder la règle logicielle et recâbler les sorties des drivers pour que le canal 0 soit du côté grave.
* **ALT-004**: **Rejection Reason**: touche au matériel et au schéma pour un problème de table logicielle ; rend le bring-up et les cartes déjà montées non conformes. Hors du périmètre d'un firmware.

#### Inverser l'ordre des positions (cartographie inter-cartes)
* **ALT-005**: **Description**: parcourir les positions `4 → 0` pour obtenir l'effet apparent.
* **ALT-006**: **Rejection Reason**: inverse aussi l'ancrage grave et le sens inter-cartes, que l'humain ne demande pas et qu'ADR-0010 a fixés ; produirait un instrument faux sur deux axes pour corriger un seul.

### Implementation Notes

Relevé factuel des points d'impact (chemins relatifs à la racine) :

* **IMP-001**: `shared/protocol/commissioning_table.c:204-224` — boucle canaux de `commissioning_build_runtime`. Parcourir `channel` de `CAN_IPROPI_CHANNEL_COUNT - 1` à `0` (index décroissant), en conservant `key = block_start + channel_index` et `++channel_index` sur canal occupé. Ne pas toucher `:179` (parcours positions), `:191-203` (bloc, `position_first_key/last_key`, gardes), `:225`, ni `commissioning_lookup_midi_note` (`:233`), ni l'encodage/décodage flash. Mettre à jour le commentaire de règle `:150-158`.
* **IMP-002**: `firmware/clavier/src/uid_position.h:13-18` — commentaire de convention : « les blocs montent dans l'ordre des canaux puis des positions » devient « les blocs montent dans l'ordre des positions ; dans un bloc, les canaux occupés parcourus de 11 vers 0 reçoivent les touches croissantes ». Aucune donnée de seed à changer.
* **IMP-003**: `firmware/clavier/src/can_link.c` — `:297-300` commentaire de `emit_map_line` (« canaux occupés croissants → touches croissantes ») à inverser ; `:329-335` parcours d'émission des lignes `[MAP]` (ordre des lignes seulement, contenu inchangé) ; `:493-513` `build_channel_list` **reste croissant** (format `[CARD]` stable) ; `:526-575` `build_note_key_lists` doit produire des touches **décroissantes** pour des canaux croissants : initialiser `current_key = last_key` et décrémenter après chaque canal occupé, en conservant le garde `:545-548`. Vérifier que les paires `channels`/`keys`/`notes` restent alignées par index et que `:1440`/`:1486` (boucles positions) ne changent pas.
* **IMP-004**: `tests/host/test_phase3_helpers.c` — `test_commissioning_assignment_examples` (`:129-175`) : recalculer les six cas (`0x00B`, `0x841`, deux cartes) selon §4 ; cas « trou de bitmap » (`:152-159`) : canal 11 → touche 1, canal 6 → 2, canal 0 → 3 ; `test_commissioning_totals_and_duplicates` (`:200-232`) et `test_commissioning_five_boards_total_49` (attendu canal 8 → touche 9 etc.) à recalculer ; `test_diag_lines` (`:715`, `:770-771`) et `print_example_case_a` (`:853-873`) : ligne `[CARD]` attendue `channels=0,1,3 keys=3,2,1 notes=RE2(38),DO#2(37),DO2(36)`. Procédure inchangée : `tests/host/run_host_tests.ps1` (mêmes sources, mêmes options).
* **IMP-005**: `tests/phase3b_bout_en_bout.md` (`:47-57`, `:274-310`) et `tests/bringup_commissioning_provisoire.md` (`:11-12`, `:149-154`, `:211-214`) : textes et exemples `[CARD]`/`[MAP]` à recalculer ; le contrôle « canal 0 = DO2 » devient « canal occupé le plus haut = DO2, canal 0 = la plus aiguë du bloc ».
* **IMP-006**: `docs/specs/CAN.md:736-768` (règle et exemples, dont le tableau canal/touche) ; `docs/specs/Detail_architecture.md:21` ; `docs/specs/Roadmap.md:134-140` (exemple « touches 1,2,3 ») ; `docs/specs/impact-adr-0010.md:34` (« Dans un bloc : inchangé ») ; `docs/adr/ARCHITECTURE_CURRENT.md:58-59`. Ces documents sont à corriger par doc-aval/dans le même cycle, pas avant l'implémentation.
* **IMP-007**: Vérifié — aucune hypothèse d'ordre dans `firmware/moteur/` (`can_link.c:126-127`, `:663-665`, `ipropi_selftest.c:219-349` : bits par canal, `driver[channel]`) ni dans `shared/protocol/can_protocol.[ch]` (seul `channel` est transporté). Rien à modifier de ce côté.
* **IMP-008**: Aucun test unitaire nouveau n'est requis ; les vecteurs existants suffisent à couvrir le sens intra-carte (`0x00B` pour un trou médian, `0x841` pour un trou haut, carte pleine, deux cartes).

### References
* **REF-001**: [ADR-0010](./adr-0010-ancrage-grave-mapping-commissioning.md) — §1 (point amendé), §5 (exemples remplacés) ; POS-004/NEG-001 et §3 (`ANCHOR_SHIFT`) restent en vigueur.
* **REF-002**: [ADR-0004](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) — format de table et `SOLENOID_COMMAND` (inchangés).
* **REF-003**: [ADR-0009](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) — états et motifs de signalement (inchangés).
* **REF-004**: [ADR-0008](./adr-0008-convention-nommage-octaves-notation-scientifique.md) — `touche = MIDI − 35`, `DO2(36)` … `DO6(84)`.
* **REF-005**: `shared/protocol/commissioning_table.c`, `firmware/clavier/src/{uid_position.h,can_link.c}`, `tests/host/test_phase3_helpers.c`, `docs/specs/CAN.md`, `docs/specs/impact-adr-0010.md`.
