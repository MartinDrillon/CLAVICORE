---
title: "ADR-0021: Ne jamais détruire une assignation valide sur une mesure non concluante — conservation du bitmap précédent"
status: "Accepted"
date: "2026-10-23"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

Amende ADR-0009 (IMP-003 et NEG-004). Aucune dérogation aux invariants 1 à 6 :
aucune trame de `shared/protocol/` n'est modifiée par les décisions 1 à 4 ;
la décision 5 réutilise le vocabulaire de `CARD_EVENT` (ADR-0018, `Proposed`).
L'invariant 1 est renforcé : un bitmap conservé est **toujours** signalé.

### Context

Observation du 2026-10-23 après 24 h d'immobilisation, cartes sous tension,
journal du Clavier (extraits, préfixe d'horodatage omis) :

```text
[PROFILE] sync ok duree=2 ms acquittements=2/2 tag=0x4A0B4C69     <- p send n°1
[PROFILE] sync ok duree=1 ms acquittements=2/2 tag=0x4A0B4C69     <- p send n°2
[PROFILE] sync ok duree=1 ms acquittements=2/2 tag=0x4A0B4C69     <- p send n°3
... aucune note ne joue ...
[SELFTEST] req=20 pos=0 status=ABORTED bitmap=0x000 faults=0x000 tag=456B
[SELFTEST] req=20 pos=2 status=OK      bitmap=0x000 faults=0x000 tag=9D5C
[TABLE] diff pos=0 old_uid=A3FA4C99CA526F4B new_uid=A3FA4C99CA526F4B old=0x009 new=0x000
[STATUS] code=TABLE_DIFFERS
[STATUS] code=TABLE_WRITTEN
[TABLE] loaded state=PROVISIONAL flags=0x75 cards=2 keys=0
[SUMMARY] cards_ok=1/5 channels=0 keys=0/49 missing_keys=1-49 table=DIFFERS_WRITTEN
... puis midi=36 et midi=37 sont eux aussi NOTE_DROPPED reason=note_unassigned ...
[UID] pos=0 ... reply_to=0  -> CARD_REBOOTED
[SELFTEST] req=21 pos=0 status=OK bitmap=0x009 ; table réécrite 0x009 ; jeu rétabli
```

Trois observations se déduisent du journal, sans hypothèse :

1. **Les cartes n'ont pas gelé.** Pendant que rien ne jouait, trois `p send`
   ont acquitté `2/2` en 1 à 2 ms : réception CAN, validation, flash (aucune
   écriture nécessaire) et émission fonctionnaient. Le bus et les cartes étaient
   sains.
2. **C'est la table qui a été détruite.** La carte de position 0 a répondu
   `ABORTED` à l'auto-test `req=20` ; le Clavier a écrit une entrée à
   **bitmap 0** pour cette carte, ce qui a produit une table `keys=0` —
   `missing_keys=1-49`. Après cette écriture, **même DO2 (36) et DO#2 (37)**
   tombent en `note_unassigned` : le clavier entier est muet.
3. **Ce n'est pas le bouton RUN qui a réparé**, c'est le **second** auto-test
   (`req=21`), revenu `OK` avec `bitmap=0x009`. Le doute de l'opérateur était
   fondé.

Le comportement du Clavier est **conforme à ADR-0009** : `finish_selftest_session`
(`firmware/clavier/src/can_link.c:1658-1663`) force le bitmap à 0 pour toute
carte dont `test_status != OK` :

```c
// Carte répondante en échec : bitmap inconnu, aucun canal activable —
// jamais un bitmap partiel deviné.
new_entries[new_count].occupation_bitmap =
    (selftest_session.status[position].test_status == CAN_SELFTEST_STATUS_OK)
        ? selftest_session.status[position].occupation_bits
        : 0u;
```

et ADR-0009 NEG-004 l'assume : « Une carte absente ou en échec disparaît ou voit
son bitmap forcé à 0 : son retour exige un nouveau test, ce que l'utilisateur
fait déjà. »

Le raisonnement de sécurité est bon : ne jamais activer un canal dont l'état
vient d'être mesuré faux ou non mesuré. Mais la **conséquence** ne l'est pas :
une mesure non concluante **efface une assignation déjà valide**, et transforme
un instrument qui jouait deux touches en instrument qui n'en joue aucune. ADR-0009
NEG-004 supposait « l'utilisateur refera un test » ; dans les faits, il ne peut
pas distinguer un test avorté d'une carte morte, il appuie sur RUN (qui n'y change
rien) et il conclut à un gel des cartes. C'est exactement le symptôme rapporté à
deux reprises les 22 et 23 octobre.

L'avortement lui-même est un garde-fou légitime : la fenêtre d'échantillonnage du
PROBE (5 ms) a été manquée, donc la mesure d'occupation serait fausse ; la carte
signale `ABORTED` plutôt que de mentir (invariant 1, ADR-0009 §5b). Il faut le
conserver. Ce qui doit changer, c'est ce que le Clavier en fait.

### Decision

#### 1. Une mesure non concluante conserve l'assignation précédente

Lorsqu'une carte **présente** répond avec `test_status != OK`
(`ABORTED`, `REFUSED_BUSY`, `UNCALIBRATED`) :

* si la table courante contient déjà une entrée pour **la même position avec le
  même UID**, cette entrée est **reprise telle quelle** (bitmap inchangé) ;
* sinon — pas d'entrée précédente, ou UID différent (carte remplacée) — l'entrée
  est créée avec un **bitmap 0**, comme aujourd'hui.

Le bitmap conservé est une valeur **mesurée antérieurement**, jamais un bitmap
partiel deviné : la règle de sécurité d'ADR-0009 §5b reste intacte.

#### 2. La conservation est toujours signalée

Chaque entrée conservée produit une ligne explicite :

```text
[TABLE] pos=N bitmap conserve=0xNNN (mesure non concluante: ABORTED)
```

et les motifs existants sont posés : `COMMISSIONING_FLAG_BOARD_NOT_OK`, état
`PROVISIONAL`, `[STATUS] code=TABLE_PROVISIONAL`. Aucune conservation silencieuse
(invariant 1).

#### 3. Une carte absente reste absente

`BOARD_MISSING` conserve le comportement d'ADR-0009 §2 : une position sans
réponse ne figure pas dans la table. Une carte physiquement retirée ne doit pas
être re-déclarée présente sur la foi d'une mesure ancienne. Le cas « carte qui
redémarre pendant le test » est donc traité comme une absence, et signalé
(`CARD_REBOOTED`, ADR-0017 décision 4) ; il devra être re-testé.

#### 4. Aucune activation nouvelle n'est créée

On ne fait que **ne pas perdre** ce qui était déjà acquis. Aucun canal n'est
assigné, déplacé ou activé sur la base d'une mesure non concluante ; aucun
remappage (invariant 1), aucune écriture de limite (invariant 4).

#### 5. La cause de l'avortement devient visible

Le Moteur émet un `CARD_EVENT` (ADR-0018, `0x424`) de type `SELFTEST_ABORTED`
avec le **canal** en cours de mesure, et incrémente un compteur dédié
(`selftest_aborted`, exposé par la décision 2 d'ADR-0020). Un avortement répété
sur le même canal doit être diagnosticable sans console.

#### 6. Ce qui ne change pas

* Le garde-fou d'échantillonnage du PROBE et le statut `ABORTED` (ADR-0009 §5b).
* Les refus d'écriture existants : `DUPLICATE_TAG`, position dupliquée, table
  invalidée par `commissioning_build_runtime`.
* Le refus d'auto-test pendant le jeu (ADR-0004 §2).
* Aucune trame existante modifiée ; `0x424` reste celle d'ADR-0018.

### Consequences

#### Positive
* **POS-001**: Un avortement transitoire ne rend plus le clavier muet : les
  touches déjà validées continuent de jouer, et l'opérateur voit pourquoi.
* **POS-002**: Le symptôme « les cartes ont gelé » cesse d'être confondu avec un
  gel réel — le journal distingue désormais table dégradée et cartes muettes
  (`p send` qui acquitte = cartes saines).
* **POS-003**: La mesure non concluante reste sans danger : aucun canal nouveau
  n'est activé, aucun bitmap partiel n'est inventé.
* **POS-004**: Une carte réellement remplacée (UID différent) n'hérite pas d'un
  bitmap périmé.

#### Negative
* **NEG-001**: Une table peut conserver un bitmap **périmé** pour une carte dont
  la mesure échoue de façon persistante : un solénoïde déposé resterait « assigné »
  tant qu'aucun test réussi ne le corrige. Le motif `BOARD_NOT_OK` et la ligne
  `bitmap conserve=` le disent, mais la table ne reflète plus l'état physique.
* **NEG-002**: La logique de construction de table se complexifie (recherche de
  l'entrée précédente par position **et** UID) ; elle doit être couverte par des
  tests hôte.
* **NEG-003**: La décision 5 dépend d'ADR-0018 (`0x424`) et d'ADR-0020
  (`selftest_aborted` dans les compteurs) : deux ADR à accepter avant.
* **NEG-004**: Le cas « carte qui redémarre pendant l'auto-test » reste une
  perte d'assignation (décision 3) : l'instrument perd les touches de cette carte
  jusqu'au test suivant, mais rien n'est écrit de faux.

### Alternatives Considered

#### Conserver le comportement actuel (bitmap 0 sur statut non OK)
* **ALT-001**: **Description**: garder ADR-0009 IMP-003 tel quel.
* **ALT-002**: **Rejection Reason**: c'est la cause directe de l'incident : une
  mesure avortée sur une carte efface une assignation valide et rend le clavier
  muet, sans que l'opérateur puisse le distinguer d'une panne. Coût constaté :
  deux sessions de banc et un doute matériel.

#### Refuser toute écriture si une carte présente n'est pas OK
* **ALT-003**: **Description**: revenir au refus global antérieur à ADR-0009.
* **ALT-004**: **Rejection Reason**: réintroduit le blocage qu'ADR-0009 a
  supprimé — une seule carte en défaut empêcherait de mettre à jour les autres,
  y compris pendant le bring-up.

#### Marquer les canaux conservés comme « non activables » jusqu'au prochain test
* **ALT-005**: **Description**: conserver le bitmap pour la trace mais refuser
  toute activation sur cette carte.
* **ALT-006**: **Rejection Reason**: aboutit au même résultat pratique que le
  bitmap 0 (le clavier devient muet sur ces touches) tout en complexifiant le
  modèle ; et cela punit l'instrument pour une mesure ratée côté banc.

#### Réduire la fenêtre d'échantillonnage ou supprimer l'avortement
* **ALT-007**: **Description**: échantillonner plus tôt, ou accepter la mesure
  même si la fenêtre est manquée.
* **ALT-008**: **Rejection Reason**: on produirait une occupation fausse en
  silence (courant coupé au moment de l'échantillonnage), exactement ce que
  l'invariant 1 interdit. L'avortement doit rester possible ; c'est sa
  conséquence sur la table qui est corrigée.

#### Faire re-tester automatiquement la carte en échec
* **ALT-009**: **Description**: relancer un auto-test ciblé sur la carte avortée,
  N fois, avant d'écrire.
* **ALT-010**: **Rejection Reason**: ne change pas la décision en cas d'échec
  persistant, ajoute de la latence et de la complexité ; une répétition bornée
  pourra être étudiée si les avortements s'avèrent transitoires et fréquents.

### Implementation Notes
* **IMP-001**: `firmware/clavier/src/can_link.c`, `finish_selftest_session`
  (1650-1669) : pour `test_status != OK`, chercher l'entrée correspondante dans
  `commissioning.entries` par `identity.position` **et** `identity.uid`
  (comparaison complète, pas le tag), reprendre `occupation_bitmap` si trouvée,
  sinon 0. Ne pas toucher au calcul `preview`/`flags`.
* **IMP-002**: Émettre la ligne `[TABLE] pos=N bitmap conserve=0xNNN (mesure non
  concluante: <statut>)` via un formateur de `diag_log.c` (testable sur hôte),
  uniquement quand une entrée est effectivement conservée.
* **IMP-003**: Tests hôte : (a) carte non OK + entrée précédente même UID →
  bitmap conservé, flag `BOARD_NOT_OK`, ligne émise ; (b) carte non OK sans
  entrée précédente → bitmap 0 ; (c) carte non OK + UID différent → bitmap 0 ;
  (d) carte absente → absente de la table (comportement inchangé).
* **IMP-004**: Moteur : émettre `SELFTEST_ABORTED` (canal) au moment de
  `finish_now(CAN_SELFTEST_STATUS_ABORTED)` (`ipropi_selftest.c:295-297`), et
  incrémenter `selftest_aborted`. Dépend d'ADR-0018 et d'ADR-0020.
* **IMP-005**: Ne pas modifier `SOLENOID_PROBE_DURATION_MS` ni la marge
  d'échantillonnage : la fenêtre est correcte par construction (ADR-0009 §5b) ;
  seule la conséquence de l'avortement change.
* **IMP-006**: Documentaliste : `docs/specs/CAN.md`/`profil-solenoides.md`
  (aucune trame modifiée, mais la ligne `bitmap conserve=` et les motifs),
  `tests/phase3b_bout_en_bout.md` (cas « auto-test avorté : le clavier continue
  de jouer les touches déjà validées »), et la procédure de diagnostic :
  **`p send` qui acquitte 2/2 alors que rien ne joue ⇒ regarder la table, pas les
  cartes.**

### References
* **REF-001**: [ADR-0009 — Table de commissioning provisoire](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) (§2 présence, IMP-003 bitmap 0 sur statut non OK, NEG-004, §5b fenêtre PROBE)
* **REF-002**: [ADR-0004 — Auto-test IPROPI](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) (§2 refus pendant le jeu, trame `0x401`)
* **REF-003**: [ADR-0010 — Ancrage du mapping](./adr-0010-ancrage-grave-mapping-commissioning.md) (flags `ANCHOR_SHIFT`, positions présentes)
* **REF-004**: [ADR-0017 — Journal de diagnostic auto-suffisant](./adr-0017-journal-diagnostic-auto-suffisant-horodatage-identite-redemarrages.md) (`CARD_REBOOTED`, horodatage)
* **REF-005**: [ADR-0018 — Trame CARD_EVENT (0x424)](./adr-0018-retour-etat-bouton-canaux-moteur-vers-clavier.md) (support de `SELFTEST_ABORTED`)
* **REF-006**: [ADR-0020 — Comptabilité des échecs des cartes](./adr-0020-comptabilite-echecs-cartes-moteur.md) (compteur `selftest_aborted`, lecture à la demande)
* **REF-007**: `firmware/clavier/src/can_link.c` — construction de la table (1650-1669), `finish_selftest_session` (1611+), ligne `[TABLE] diff` (1603-1608)
* **REF-008**: `firmware/moteur/src/ipropi_selftest.c:287-298` — garde-fou de la fenêtre PROBE et `finish_now(ABORTED)`
* **REF-009**: `firmware/moteur/CMakeLists.txt:89` — `pico_enable_stdio_uart(moteur 0)` : les `printf` du Moteur ne bloquent pas la boucle (piste écartée pour expliquer l'avortement)
