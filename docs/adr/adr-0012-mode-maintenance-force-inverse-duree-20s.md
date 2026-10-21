---
title: "ADR-0012: Mode MAINTENANCE — poussée en polarité inverse (REPEL) tenue jusqu'au relâchement, bornée à 20 s"
status: "Accepted"
date: "2026-10-21"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "hardware"]
supersedes: ""
superseded_by: ""
---

### Status
**Dérogation à un invariant — validation humaine requise : validée par
l'utilisateur le 2026-10-21 (invariant 4 de `AGENTS.md`, profil MAINTENANCE).**

Cette ADR **remplace partiellement l'ADR-0006 (§4, direction de la poussée)** et
**modifie la borne de durée** de la dérogation à l'invariant 4 de `AGENTS.md`
(profil MAINTENANCE : coupure autonome en couche pilote). La dérogation reste
confinée dans `solenoid_driver.c`, reste **bornée** et reste **coupée
automatiquement par le driver** : l'invariant 4 n'est pas rouvert, il est
**re-borné** (10 s → 20 s, avec enveloppe énergétique inchangée).

**Accepted**

### Context

#### 1. Symptôme rapporté

Le keycode `KEYCODE_MAINTENANCE_TOGGLE` assigné en position 84
(`firmware/clavier/src/keymap.c:39`) **ne bascule pas** le Clavier en mode
maintenance. Le mode est donc **inatteignable**, et le profil MAINTENANCE de
`solenoid_profile.h:50-54` n'est jamais exercé sur le banc.

#### 2. Cause racine (défaut de conformité, pas un choix)

Le dispatch du Clavier est cassé en `firmware/clavier/src/main.c:235-250` :

```c
if (keycode.kind == KEYCODE_GET_UID) {
    if (pressed) { /* ... can_link_request_uid() ... */ }

    if (keycode.kind == KEYCODE_MAINTENANCE_TOGGLE) {   // main.c:243 — jamais vrai
        if (pressed) {
            handle_maintenance_toggle(state);
        }
        return;
    }
    return;
}
```

Le second `if` est **imbriqué dans le test `GET_UID`** : sa condition est
impossible par construction. L'appui sur la position 84 tombe donc dans le
chemin générique de behavior (`main.c:252-262`) et n'imprime que
`position 84 behavior MAINT pressed` — aucune ligne `[MODE] maintenance=ON`.

La bascule est en revanche **déjà correcte** lorsque le mode est déjà actif
(`main.c:204-207` → `handle_maintenance_transition` → `main.c:143-148` →
`handle_maintenance_toggle`). D'où le symptôme exact : **impossible d'entrer,
sortie fonctionnelle**. C'est le point (a) déjà relevé, hors périmètre, par
l'ADR-0011 (`IMP-005`) ; la présente ADR est le traitement séparé qu'elle
annonçait.

Le mode ne peut pas non plus être atteint par un autre chemin : `maintenance_mode`
n'est pas persistant (`main.c:23`, RAZ au boot `main.c:266`), aucun message CAN
ne le transporte, et aucun autre keycode n'appelle `handle_maintenance_toggle`
(hors `main.c:245`, mort).

#### 3. Le profil MAINTENANCE actuel ne fait pas ce qui est voulu

Le comportement demandé par l'utilisateur est : **une force en sens inverse du
jeu normal, appliquée pendant toute la durée de l'appui**, pour **repousser
l'aimant** jusqu'à une position d'assemblage, avec une durée maximale « assez
longue ».

Or l'ADR-0006 §4 a décidé l'inverse : « la poussée maintenance est **PULL
uniquement** ; pas de phase HOLD ; **pas de phase RELEASE inversée** ». Le
driver s'y conforme littéralement — `solenoid_driver.c:261` force
`SOLENOID_DIR_LEVEL_PULL` avant d'activer la PWM — donc une poussée maintenance,
si elle était atteignable, **tirerait** l'aimant dans le sens du jeu au lieu de
le repousser. Le défaut de dispatch masque aujourd'hui cette divergence : la
corriger seule livrerait un mode qui « marche » mais fait **le geste inverse de
celui attendu**.

#### 4. Contraintes qui survivent

* Matériel : fenêtre de courant 2–3 A, bobine sur mesure ; enveloppe thermique
  déjà validée par l'humain = **100 % de duty pendant 10 s** (ADR-0006 §4).
* La carte Moteur reste **anonyme** (invariant 2) : la direction est une
  propriété **électrique** du profil, pas un rôle musical.
* Aucune notion de mode global dans la carte Moteur (ADR-0006 §5) : la commande
  elle-même porte le profil (`CAN_SOLENOID_COMMAND_PUSH_ON = 2`,
  `shared/protocol/can_protocol.h:95-97`).
* La table de commissioning reste la seule source de vérité (invariant 3) : la
  résolution note MIDI → `(UID, canal)` en maintenance ne change pas.
* Inversion de polarité : `PH/EN` ne doit **jamais** changer d'état si le duty
  n'est pas nul depuis `SOLENOID_DIR_SAFETY_DELAY_US` = 200 µs (ADR-0001).
* Invariant 5 : toute modification d'interface partagée exige une ADR. Le choix
  ci-dessous **n'en modifie aucune**.

### Decision

#### Décision 1 — Le keycode `MAINTENANCE_TOGGLE` doit être atteignable (mise en conformité ADR-0006 §2)

Le bloc `main.c:243-248` est **déplacé hors** du test `KEYCODE_GET_UID`, au même
niveau que les autres behaviors. Sur front d'appui uniquement, il appelle
`handle_maintenance_toggle(state)` puis sort ; le relâchement est sans effet.
Toutes les conditions d'entrée/sortie de l'ADR-0006 §2 restent inchangées et
**ne sont pas redécidées ici** : refus si `can_link_has_valid_table()` est faux
(`DIAG_STATUS_MAINTENANCE_NO_TABLE`), refus si un auto-test est en cours
(`DIAG_STATUS_BUSY`), coupure de tous les solénoïdes actifs à l'entrée comme à
la sortie, journal `[MODE] maintenance=ON/OFF`, aucune auto-sortie.

Ce n'est **pas** un changement de comportement décidé ici : c'est l'application
d'une décision déjà acceptée, défaillante à l'implémentation.

#### Décision 2 — Le profil MAINTENANCE pousse en **polarité inverse** (REPEL)

Le profil MAINTENANCE applique au canal ciblé, pendant **toute la durée de
l'appui** et sans changement de phase :

* `PH/EN` (DIR) au niveau **inverse du jeu normal** — `SOLENOID_DIR_LEVEL_RELEASE`
  (`solenoid_profile.h:59`), exposé par une constante dédiée du profil
  `SOLENOID_MAINT_DIR_LEVEL` ;
* duty constant `SOLENOID_MAINT_DUTY_PERCENT` (50 %), plafonné par
  `SOLENOID_MAINT_DUTY_MAX_PERCENT` (100 %) ;
* **aucune** phase HOLD, **aucune** phase RELEASE : la poussée dure tant que la
  touche est tenue, puis le driver coupe (duty 0) au relâchement ou au timeout.

Le sens n'est **pas** négociable par le Clavier et n'apparaît dans **aucune**
trame CAN : la carte Moteur ne reçoit toujours que `command = PUSH_ON (2)` et
applique *son* profil MAINTENANCE. Le Clavier continue d'ignorer la direction.

#### Décision 3 — Durée maximale portée de 10 s à **20 s**, à **enveloppe énergétique constante**

`SOLENOID_MAINT_MAX_MS` passe de `10000` à `20000`, et
`SOLENOID_MAINT_MIN_REST_MS` reste `2000`.

Justification : à courant quasi continu, la puissance dissipée est
proportionnelle au duty (P ∝ duty). **20 s à 50 %** dissipe donc approximativement
autant que **10 s à 100 %**, c'est-à-dire exactement l'enveloppe déjà validée
thermiquement par l'humain (ADR-0006 §4). La durée double sans augmenter le
budget thermique ; c'est un allongement de **temps**, pas d'**énergie**.

Ce budget devient une **borne de compilation**, pas une intention :

```
SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT = 10000 * 100    // enveloppe validée : 10 s à 100 %
_Static_assert(SOLENOID_MAINT_MAX_MS * SOLENOID_MAINT_DUTY_PERCENT
               <= SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT,
               "profil MAINTENANCE hors enveloppe thermique validée");
```

L'unité est la **(ms × %)** : le produit `durée × duty` est proportionnel à
l'énergie déposée. L'enveloppe validée vaut `10 000 × 100 = 1 000 000`, et le
profil retenu `20 000 × 50 = 1 000 000` la sature exactement — ni plus, ni moins.

Un profil réglé au banc ne peut donc plus sortir de l'enveloppe validée sans
casser la compilation — l'esprit de l'invariant 4 (« un profil mal réglé ne peut
pas dépasser les bornes »), étendu à la **combinaison** durée × duty.

Dépasser 20 s, ou dépasser cette enveloppe, exige une nouvelle validation
thermique humaine et donc une nouvelle ADR.

#### Décision 4 — Toute sortie de MAINTENANCE restaure la polarité de jeu

Sortir de MAINTENANCE (relâchement, timeout, `OFF`) doit ramener `PH/EN` au
niveau PULL **avec duty nul et après le délai de sécurité**, par le même chemin
que la sortie de RELEASE (`DIR_TO_PULL` → `REST`, `solenoid_driver.c:318-329`),
et **jamais** en réécrivant DIR au prochain appui.

Motif : `solenoid_driver.c:189` documente l'invariant « `dir_channel` est au
niveau PULL en IDLE ». La sortie actuelle de MAINTENANCE va directement en
`REST` (`solenoid_driver.c:204-206`) sans restaurer DIR ; en polarité inverse,
cela laisserait DIR au niveau RELEASE jusqu'à l'activation suivante, et le
prochain **jeu normal** activerait la PWM dans le mauvais sens, sans délai de
sécurité. C'est un risque introduit par cette ADR et fermé par cette décision.

Inversement, l'**entrée** en MAINTENANCE peut écrire DIR immédiatement : elle
n'est admise qu'en `IDLE` (`solenoid_driver.c:252`), état atteint uniquement via
`REST` (`solenoid_driver.c:330-333`), donc avec duty nul depuis au moins
`SOLENOID_MIN_REST_MS` (10 ms) ≫ 200 µs. Cette garantie doit être **commentée
comme telle** dans le code ; si elle venait à être contournée (nouveau chemin
vers IDLE), l'inversion passerait par un état transitoire calqué sur
`DIR_TO_RELEASE` (`solenoid_driver.c:311-317`).

#### Décision 5 — Traceabilité et test au banc

* Le journal CDC du Clavier reste la seule trace exploitable (le Moteur n'a pas
  de console) : `[MODE] maintenance=ON/OFF` puis `[PUSH] midi=M key=K pos=P ch=C
  start|stop|timeout` (ADR-0006 §3). La ligne `start` prouve que la commande
  `PUSH_ON` est partie ; la **direction** ne s'observe qu'au banc.
* Le test manuel `tests/phase3b_maintenance.md` doit distinguer explicitement
  les deux critères : (a) l'entrée/sortie de mode et les lignes CDC, (b) le
  **sens** du geste observé sur le canal poussé, comparé au geste du jeu normal.
* Un `[PUSH] timeout` à ~20 s (et non 10 s) est le critère de vérification de la
  Décision 3, solénoïde surveillé thermiquement à la main.

### Consequences

#### Positive
* **POS-001**: Le mode maintenance devient réellement utilisable : la cause
  racine du symptôme (code mort, `main.c:243-248`) est nommée et la mise en
  conformité avec l'ADR-0006 §2 est tracée, plus jamais perdue.
* **POS-002**: Le geste correspond au besoin physique : l'aimant est **repoussé**
  pendant tout l'appui, ce qui permet l'assemblage sans outil ni manipulation
  directe sous les sauteaux.
* **POS-003**: La durée double (10 s → 20 s) **sans** augmentation du budget
  thermique, grâce à l'équivalence 20 s @ 50 % ≈ 10 s @ 100 %.
* **POS-004**: Le budget thermique est vérifié **à la compilation** (duty ×
  durée), donc un réglage de banc ne peut plus dérober l'enveloppe validée.
* **POS-005**: Aucune interface partagée modifiée : ni `shared/protocol/`, ni
  ID/DLC/trame CAN, ni table de commissioning. L'invariant 5 n'est pas sollicité
  et les cartes Moteur restent anonymes.

#### Negative
* **NEG-001**: Le profil MAINTENANCE et le niveau DIR de `PH/EN` dépendent
  désormais de la **convention de câblage des bobines** : si un canal est câblé
  en sens inverse des autres, le geste y sera inversé. Constaté au banc, non
  détectable par le firmware (aucun retour de position).
* **NEG-002**: `command = PUSH_ON (2)` garde sa valeur mais **change de sens
  physique**. Un Moteur neuf avec un Clavier ancien (ou l'inverse) pousserait
  dans la mauvaise direction, silencieusement. Acceptable pendant le bring-up
  seulement parce que Clavier et Moteur sont flashés ensemble ; toute
  divergence de version doit être traitée comme une erreur d'exploitation.
* **NEG-003**: Maintenir 20 s une force inverse contre une butée mécanique
  charge la fixation de l'aimant au-delà de ce que le jeu normal produit. Non
  quantifié par le firmware ; à surveiller au banc.
* **NEG-004**: Le mode reste un outil de commissioning : entrer en maintenance
  en plein jeu coupe tous les solénoïdes (ADR-0006 §2). La nouvelle atteignabilité
  du keycode rend ce geste réellement disponible, donc réellement déclenchable
  par erreur.

### Alternatives Considered

#### Corriger uniquement le dispatch, sans changer le profil
* **ALT-001**: **Description**: Déplacer le bloc `main.c:243-248` et livrer tel
  quel le profil PULL-only de l'ADR-0006.
* **ALT-002**: **Rejection Reason**: Le mode fonctionnerait mais appliquerait
  une force dans le sens du jeu normal, à l'opposé du besoin physique exprimé
  (« repousser l'aimant »). On livrerait un mode qui *semble* marcher et fait le
  mauvais geste, avec un risque de fausse validation au banc.

#### Nouvelle valeur de commande CAN (`PUSH_OUT = 3`)
* **ALT-003**: **Description**: Ajouter `CAN_SOLENOID_COMMAND_PUSH_OUT` pour
  distinguer explicitement la poussée inverse de la poussée historique.
* **ALT-004**: **Rejection Reason**: Aucun bénéfice fonctionnel : la maintenance
  est un outil de commissioning où Clavier et Moteur sont flashés ensemble, et
  une trame supplémentaire élargit une interface partagée (invariant 5) pour un
  mode qui n'a jamais été déployé. La direction reste une propriété du profil,
  côté Moteur.

#### Direction pilotée par le Clavier (champ dans la trame)
* **ALT-005**: **Description**: Transmettre le niveau DIR voulu dans
  `CAN_SOLENOID_COMMAND.velocity` ou un octet libre, pour choisir le sens depuis
  le Clavier.
* **ALT-006**: **Rejection Reason**: Donne au Clavier une connaissance
  électrique du canal qu'il n'a pas (il ne connaît pas le câblage de la carte
  Moteur), déplace une limite matérielle hors de la couche pilote (invariant 4)
  et permet au métier de demander n'importe quelle polarité, y compris une
  inversion en cours de poussée.

#### Conserver 10 s et demander au Moteur de relancer des cycles
* **ALT-007**: **Description**: Garder `SOLENOID_MAINT_MAX_MS = 10000` et
  enchaîner automatiquement des cycles de 10 s séparés par le repos minimal
  tant que la touche est tenue.
* **ALT-008**: **Rejection Reason**: Multiplie les transitoires d'inversion de
  polarité (chaque cycle repasse par DIR), ajoute un état métier côté Moteur
  (donc une notion de mode dans une carte anonyme, ADR-0006 alt. 1) et produit
  des à-coups mécaniques contraires au but d'assemblage. Allonger une poussée
  continue à budget constant est plus simple et plus sûr.

### Implementation Notes
* **IMP-001**: `main.c` : sortir le bloc `KEYCODE_MAINTENANCE_TOGGLE` de
  `if (keycode.kind == KEYCODE_GET_UID)` (l.235-250) ; il devient un `if`
  **frère**, avant le chemin générique de behavior (l.252), avec `return` sur
  appui comme sur relâchement pour ne pas doubler la ligne `behavior`. Ne pas
  toucher `handle_maintenance_transition` (l.138-193) : il est déjà correct.
* **IMP-002**: `solenoid_driver.c` : dans `solenoid_driver_push_start`
  (l.239-266), écrire `SOLENOID_MAINT_DIR_LEVEL` (Décision 2) au lieu de
  `SOLENOID_DIR_LEVEL_PULL` (l.261), et **commenter la preuve de sécurité** de
  l'écriture directe (état forcément `IDLE`, donc duty nul depuis ≥
  `SOLENOID_MIN_REST_MS`, cf. Décision 4).
* **IMP-003**: `solenoid_driver.c` : `solenoid_driver_note_off` (l.204-206) doit
  router une sortie de MAINTENANCE vers `DIR_TO_PULL` (puis `REST`), comme
  `RELEASE` (l.318-329), et non directement vers `begin_rest` ; sinon
  `SOLENOID_STATE_IDLE` n'honore plus « DIR au niveau PULL » (l.189) et le
  prochain Note On du jeu normal part en sens inverse. Même correction pour le
  timeout de `SOLENOID_STATE_MAINTENANCE` (l.290-294).
* **IMP-004**: `solenoid_profile.h` : ajouter `SOLENOID_MAINT_DIR_LEVEL`
  (= `SOLENOID_DIR_LEVEL_RELEASE`), passer `SOLENOID_MAINT_MAX_MS` à `20000u`,
  ajouter `SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT` = `10000u * 100u` et le
  `_Static_assert` de la Décision 3 **dans** `solenoid_driver.c` avec les
  autres (l.11-23), pas dans le profil. Vérifier que
  `SOLENOID_MAINT_MAX_MS * US_PER_MS` ne déborde pas en `uint32_t`
  (20 000 000 µs : conforme).
* **IMP-005**: `shared/protocol/` : **aucune modification**. Ne pas ajouter de
  constante de commande ; ne pas changer l'encodage de
  `can_solenoid_command_t` (`can_protocol.h:159-164`). Si un besoin de protocole
  apparaît en cours d'implémentation, s'arrêter et écrire une ADR (invariant 5).
* **IMP-006**: Tests — hôte : le dispatch de `main.c` n'est pas couvert par
  `tests/host` ; la vérification de la Décision 1 est donc **manuelle**
  (appui position 84 → `[MODE] maintenance=ON`, second appui →
  `[MODE] maintenance=OFF`, aucun `[PUSH]` sans appui de note). Moteur : le
  timeout doit être mesuré à ~20 s. Consigner les deux dans
  `tests/phase3b_maintenance.md`.
* **IMP-007**: Constat hors périmètre, **à ne pas corriger sous couvert de cette
  ADR** : `AGENTS.md:13` et `docs/specs/Detail_architecture.md:5` annoncent
  « 8 switchs de contrôle » alors que `keymap.c:28-39` en assigne 12
  (positions 73 à 84). Divergence documentaire à traiter par le circuit habituel.

### References
* **REF-001**: `AGENTS.md` — invariant 2 (carte anonyme), invariant 3 (table de
  commissioning), invariant 4 (limites dans la couche pilote ; dérogation
  MAINTENANCE re-bornée ici), invariant 5 (interface partagée inchangée).
* **REF-002**: `./adr-0006-mode-maintenance-push-only.md` — §2 (activation),
  §3 (règle fonctionnelle), §5 (transport CAN `PUSH_ON = 2`) ; **§4 remplacé**
  par la Décision 2, **borne de durée** remplacée par la Décision 3.
* **REF-003**: `./adr-0007-hold-limite-20s.md` — autre dérogation bornée à 20 s
  dans la couche pilote ; même horizon temporel.
* **REF-004**: `./adr-0001-hal-solenoide-drv8874-phen.md` — contrat HAL, niveaux
  PH/EN, `SOLENOID_DIR_SAFETY_DELAY_US` (200 µs, duty nul avant inversion).
* **REF-005**: `./adr-0011-untranspose-remise-a-zero-etat-transposition.md` —
  `IMP-005` (a) : signalement du dispatch `MAINTENANCE_TOGGLE` inatteignable,
  traité ici par la Décision 1.
* **REF-006**: `firmware/clavier/src/main.c:119-136,138-193,195-263` —
  `handle_maintenance_toggle`, `handle_maintenance_transition`,
  `handle_matrix_transition` (dispatch).
* **REF-007**: `firmware/clavier/src/keymap.c:39`, `keycode.h:19` —
  position 84, `KEYCODE_MAINTENANCE_TOGGLE`.
* **REF-008**: `firmware/moteur/src/solenoid_driver.c:11-23,180-194,196-208,
  239-266,284-339` — profils et écrêtage, invariant DIR en IDLE, machine
  d'états (PULL/HOLD/RELEASE, `DIR_TO_RELEASE`/`DIR_TO_PULL`), MAINTENANCE.
* **REF-009**: `firmware/moteur/src/solenoid_profile.h:50-54,56-70` — constantes
  MAINTENANCE et plafonds absolus.
* **REF-010**: `shared/protocol/can_protocol.h:95-97,159-164` — valeurs de
  `command` (`OFF/ON/PUSH_ON`) et struct `can_solenoid_command_t` (inchangées).
* **REF-011**: `tests/phase3b_maintenance.md` — campagne manuelle à étendre
  (Décision 5, `IMP-006`).
