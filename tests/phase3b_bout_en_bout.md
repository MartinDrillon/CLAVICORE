# Phase 3b — Livraison 2 : commissioning persistant et jeu

Procédure manuelle **PuTTY + matériel réel**.

> **Procédure écrite pour ADR-0025, implémentée et validée hôte 2026-10-24.** Sur un firmware antérieur au lot
> ADR-0025, ignorer `l 0|1|2`, utiliser `n` pour les `[NOTE]`, et attendre
> `event=BUTTON_SHORT_PRESS` au lieu de `MANUAL_TEST_STARTED`. Les RAW/PUSH sont
> alors émis sans réglage de niveau.

## Préparation

1. Flasher `build\clavier\clavier.uf2` sur le Clavier.
2. Flasher `build\moteur\moteur.elf` par **SWD** (OpenOCD,
   `tests/moteur_phase2a.md` §E–F) : aucun `.uf2` Moteur n'existe (carte sans USB).
3. Ouvrir **PuTTY** sur le port **CDC** du Clavier.
4. Vérifier que `uid_position.h` contient les UID attendus.
5. Taper `l 1` pour le bring-up (boot/UID/table/`[MAP]`/résumés par carte). Pour
   les RAW et le détail par note, taper `l 2` au moment voulu. À `l 0`, seuls les
   verdicts, alertes et réponses console sont émis.

## Test 1 — Auto-test complet puis écriture de table

1. Mettre le système sous tension.
2. Observer au boot :
   * `[BOOT] table=loaded|absent|invalid cards=N`
   * `[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash`
   * `[TABLE] loaded ...` si une table valide est déjà présente ;
   * sinon `[TABLE] invalid ...` puis `[STATUS] code=FLASH_TABLE_INVALID`.
3. Maintenir le bouton d'une carte Moteur **3 s**.
4. Vérifier :
   * `[TRIGGER] bouton Moteur recu -> auto-test lance (req=R)` ;
   * en cas de refus, garder la ligne `[STATUS] ...` et vérifier en plus
     `[TRIGGER] bouton Moteur recu -> refus: ...`
   * `[STATUS] code=SELFTEST_STARTED`
   * une ligne `[SELFTEST] ...` par carte attendue
   * en fin de session : les lignes `[CARD] ...`, puis éventuellement
     `[CARD_UNKNOWN] ...`, puis `[SUMMARY] ...`
   * si tout est complet et qu'une écriture a eu lieu :
     `[STATUS] code=TABLE_WRITTEN`
   * une ligne `[TABLE] pos=N uid=... bitmap=0x... keys=a-b` par carte.
5. Si le total occupé n'est pas 49, vérifier `[STATUS] code=TABLE_PARTIAL`.

## Test 2 — Résolution de notes

Après une écriture réussie :

1. Jouer les touches des notes graves MIDI 36/37/38 (DO2, DO#2, RE2).
2. Vérifier que les solénoïdes correspondent à la règle d'assignation
   **ascendante** (ADR-0010 : ancrage sur la touche 1 = DO2) :
   * la carte de **position 0**, canal occupé le plus bas, joue **DO2**
     (touche 1, MIDI 36) ; avec 3 canaux occupés elle prend les touches
     **1, 2, 3**, donc MIDI **36, 37, 38** ;
   * si la carte de position 1 a 2 canaux occupés, elle prend les touches
     **4, 5** (MIDI **39, 40**), immédiatement après le bloc de la position 0 ;
   * dans chaque bloc, les canaux occupés croissants reçoivent les touches
     croissantes ;
   * **banc sans position 0** (cas fréquent pendant le bring-up) : le bloc grave
     est attribué à la position présente la plus basse et la table est
     `PROVISIONAL` avec `flags=0x40` (`ANCHOR_SHIFT`) ; les lignes `[MAP]`
     donnent la touche réelle de chaque canal, et c'est **elles** qui font foi
     pour le jeu.
3. Taper `l 2`, puis vérifier les lignes :
   * `[NOTE] midi=M key=K pos=P ch=C`
   * ou, en cas de rejet, `[NOTE_DROPPED] midi=M reason=...` (visible à tous les
     niveaux).
4. Revenir à `l 1` ou `l 0` : les `[NOTE]` disparaissent, `[NOTE_DROPPED]` reste.

## Test 3 — Persistance

1. Couper puis remettre l'alimentation **sans refaire d'auto-test**.
2. Vérifier au boot :
   * `[TABLE] loaded ...`
   * les lignes `[TABLE] pos=...`
3. Rejouer quelques touches : la résolution doit fonctionner immédiatement.

## Test 4 — Carte manquante

1. Éteindre.
2. Débrancher une carte Moteur référencée dans la table.
3. Rallumer.
4. Vérifier :
   * `[STATUS] code=BOARD_MISSING pos=N`
   * les notes de cette carte ne déclenchent pas de solénoïde ;
   * les autres cartes restent jouables si elles sont présentes.

## Test 5 — Mauvais cas

### Table absente / corrompue

Attendu :

* `[TABLE] invalid ...`
* `[STATUS] code=FLASH_TABLE_INVALID`
* aucun solénoïde commandé

### Carte inconnue

Attendu :

* ligne `[UID] ... pos=127 ...`
* `[STATUS] code=UID_NOT_IN_TABLE pos=127`

### Auto-test incomplet

Attendu :

* `[STATUS] code=BOARD_MISSING pos=N` pour chaque carte absente
* `[STATUS] code=TABLE_NOT_WRITTEN_INCOMPLETE`
* `[CARD] ... state=MISSING ... channels=- keys=- notes=-`
* `[SUMMARY] ... table=NOT_WRITTEN(...)`
* l'ancienne table reste en vigueur

## Test 6 — Non-régression : synchronisation du profil déclenchée par une trame CAN

Régression corrigée le **2026-10-21** (incident « auto-test impossible par le
bouton Moteur »). Cause : l'échéance de synchronisation du profil était armée
depuis le drainage de la file RX, donc avec un horodatage **postérieur** à celui
de la passe en cours (`first_ms`), et comparée en `uint32_t` non signé : le délai
paraissait déjà dépassé (`0xFFFFFFFC` ms) et l'échec était prononcé dans la même
passe, avant toute lecture d'acquittement. Seul le chemin « trame `0x402` reçue
pendant la passe » était touché ; le chemin console (`t`) ne l'était pas.

### Procédure

1. Système sous tension, table valide chargée (`[TABLE] loaded ...`), au moins
   une carte présente et en ligne.
2. **Maintenir le bouton d'une carte Moteur 3 s** (déclenchement par `0x402`).
3. Relever la séquence complète.

### Attendu

Chaque ligne du journal est précédée de son horodatage (ADR-0017 décision 1) :
le préfixe `[T+… ms]` est omis ici pour la lisibilité.

```text
[PROFILE] sync ok but=auto-test duree=<ms> tentatives=k/3 acquittements=N/N reconciliees=R tardifs=0 source=bouton tag=0x...
[STATUS] code=SELFTEST_STARTED
[TRIGGER] source=bouton req=R -> auto-test lance
```

* aucun `[STATUS] code=PROFILE_SYNC_FAILED` lorsque toutes les cartes attendues
  sont présentes ;
* la durée mesurée `duree=` est du même ordre que le délai de 1 000 ms si l'échec
  survient, et faible (quelques ms) en cas de succès : c'est elle, et non le
  `delai=` configuré, qui fait foi sur le temps réellement écoulé.

### Contre-épreuve (même résultat par les deux chemins)

Répéter le test avec la commande console `t` puis Entrée : la séquence observée
doit être **identique** à celle du bouton (mêmes `[PROFILE]`, mêmes `[SELFTEST]`),
`source=console` au lieu de `source=bouton`. Une différence entre les deux chemins
signale une régression de l'armement des échéances.

### Signature d'une réapparition de la régression

```text
[STATUS] code=PROFILE_SYNC_FAILED
[PROFILE] sync echec but=auto-test duree=0 ms tentatives=1/3 acquittements=0/N reconciliees=0 source=bouton tag=0x...
[PROFILE] sync carte pos=... tag=0x... motif=ACK_TIMEOUT
```

Le critère décisif est **`duree=0 ms`** : l'échec a été prononcé sans qu'aucun
délai ne coure, alors que le délai configuré (`delai=1000 ms`) n'a pas été
attendu. Un `duree` proche de `delai` désigne au contraire un vrai timeout, donc
une autre cause (`motif=` le dira : `ACK_TIMEOUT` carte qui ne répond pas,
`NO_PROFILE_STATUS` carte qui ne parle pas le protocole de profil).

## Test 7 — Événements de carte, compteurs et mesure avortée (ADR-0018/0020/0021/0024/0025)

Objectif : vérifier au banc que les causes de « rien ne se passe » sont désormais
**distinguables** dans le journal, et qu'un auto-test avorté ne rend plus le
clavier muet. Préfixe `[T+… ms]` omis dans les extraits.

Pré-requis : cartes **et** Clavier reflashés (les trames `0x424`/`0x425` n'existent
pas dans un firmware antérieur — une absence d'événement ne prouve alors rien).

### 7.1 Appui démarrant le test manuel

Appui bref (< 3 s) sur le bouton d'une carte. Un appui long **peut** produire le
même `MANUAL_TEST_STARTED` au démarrage du test, puis les événements de
déclenchement.

```text
[CARD] pos=N event=MANUAL_TEST_STARTED ch=<0|11> state=<...> fault=0x000
```

Le solénoïde du canal de test peut bouger : c'est le test manuel de la carte, il
est désormais **rapporté** (il ne l'était pas).

### 7.2 Appui long avec un canal non IDLE

Laisser un canal actif (note maintenue, ou test manuel en cours), puis maintenir
le bouton 3 s.

```text
[CARD] pos=N event=TRIGGER_PENDING ch=- state=0 fault=0x000
[CARD] pos=N event=TRIGGER_BLOCKED ch=<canal bloquant> state=<...> fault=0x...
[STATUS] code=TRIGGER_BLOCKED pos=N
```

`TRIGGER_PENDING` est `l 1`/`l 2` ; `TRIGGER_BLOCKED` et le `[STATUS]` restent
visibles à `l 0`.

et **aucun** `[PROFILE] sync ok` (le `0x402` n'est pas émis). Une fois le canal
revenu IDLE, un nouvel appui long doit produire `TRIGGER_SENT` puis
`[PROFILE] sync ok but=auto-test … source=bouton`.

### 7.3 Commande reçue et ignorée

Provoquer un refus côté carte (carte coupée **après** le scan de boot et touche
qui lui est assignée, par exemple).

```text
[CARD] pos=N event=COMMAND_IGNORED ch=<canal> raison=NON_OCCUPE|CANAL|AUTOTEST|PILOTE
```

Attendu en moins d'une seconde ; au plus une ligne par seconde et par raison (le
compteur, lui, compte tout).

Un `uid_tag` étranger — routage normal d'une commande diffusée vers les autres
cartes — **n'émet aucun événement**, à aucun niveau. Il continue d'incrémenter
`solenoid_ignored_tag`. Vérifier qu'une note routée sur deux cartes ne produit
aucune ligne `raison=TAG`.

### 7.4 `m stats` (console)

Taper `m stats` au repos.

* **Avant ADR-0024** : **13 lignes**
  `[CARD] pos=N stats <nom>=<valeur>` par carte présente (IDs 0–12), puis
  `[STATS] note_sent=<n> cartes_ok=<m> ecart=<n-m>`.
* **Après ADR-0024, avant ADR-0025** : **18 lignes** par carte (IDs 0–17), dont
  `solenoid_reject_sleep/rest/busy/fault/other`.
* **Après ADR-0025** : **une ligne `[CARD] pos=N stats …` par carte** plus
  `[STATS]`, avec les champs obligatoires, les non-nuls et `ext=n/a` pour une
  carte ancienne. Format : `docs/specs/impact-adr-0025.md` §4.

Contrôles :
- `m stats` pendant un auto-test ou une requête d'UID → **refusé**
  (`[CONSOLE] m stats -> refuse (occupe, voir [STATUS])`), sans trafic ;
- `m stats` sans aucune carte vue en ligne → refusé ;
- une carte muette est nommée **une fois** : `[STATUS] code=NO_RESPONSE pos=N` ;
- une carte ancienne qui répond aux IDs 0–12 mais pas aux 13–17 affiche `n/a`
  pour ces derniers, **sans** `NO_RESPONSE` ;
- jouer quelques notes puis relancer : `ecart` reste stable ; s'il augmente,
  examiner `solenoid_rejected` et les compteurs de cause 13–17 (voir 7.3) ; si
  `can_errors` ou `rx_overrun` augmente, c'est le bus ou la file applicative.

### 7.5 Niveaux du journal

`l` → `[CONSOLE] log niveau=… timestamp=… trace=…` ; `l 2` fait apparaître les
lignes `[NOTE] …`, `[KEY] …` et les RAW. `l 0` les retire, sans retirer les
alertes ni `[NOTE_DROPPED]`. `l t` commute le préfixe temporel. La commande `n`
n'existe plus. Aucun autre effet
sur les commandes CAN.

### 7.6 Auto-test avorté (ADR-0021)

Provoquer un `ABORTED` (maintenir un canal non IDLE pendant le test, ou relancer
le test pendant l'attente).

```text
[SELFTEST] req=R pos=N status=ABORTED ...
[CARD] pos=N event=SELFTEST_ABORTED ch=<canal>
[TABLE] pos=N bitmap conserve=0xNNN (mesure non concluante: ABORTED)
```

Et surtout : **les touches déjà validées continuent de jouer** (`[SUMMARY]`
conserve `keys` pour cette carte). Une carte **remplacée** (UID différent) donne
`bitmap=0x000` **sans** ligne `conserve` ; une carte **absente** reste absente.

## Addendum — exemples de session PuTTY

Les exemples ci-dessous **s'ajoutent** aux lignes existantes `[STATUS]`,
`[SELFTEST]` et `[TABLE]`. Ils suivent la règle d'assignation **ascendante**
(ADR-0010 : ancrage sur la touche 1 = DO2) et les cartes y sont volontairement
déclarées aux positions hautes (4, 3) sans position 0 : c'est le cas d'un banc
partiel, d'où le motif `ANCHOR_SHIFT` (`flags=0x40`). Les lignes `[CARD]` sont
émises par position croissante.

### (a) Une carte, canaux 0,1,3 occupés

```text
[BOOT] table=absent cards=0
[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash
[TRIGGER] bouton Moteur recu -> auto-test lance (req=42)
[STATUS] code=SELFTEST_STARTED
[SELFTEST] req=42 pos=4 status=OK bitmap=0x00B faults=0x000 tag=29B1
[STATUS] code=TABLE_PARTIAL
[STATUS] code=TABLE_WRITTEN
[TABLE] state=PROVISIONAL flags=0x40 cards=1 keys=3
[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00B channels=0,1,3 keys=1,2,3 notes=DO2(36),DO#2(37),RE2(38)
[SUMMARY] cards_ok=1/1 channels=3 keys=3/49 missing_keys=4-49 table=WRITTEN calib=ON
```

La carte est seule et déclarée en position 4 : le bloc grave lui revient
(touches 1..3) et l'absence des positions 0..3 est signalée par `flags=0x40`
(`ANCHOR_SHIFT`). Le motif disparaît dès que les positions présentes forment
`0..K−1`.

### (b) Ajout d'une 2e carte

Exemple : la nouvelle carte arrive en position 3 avec canaux `0,1`.

```text
[BOOT] table=loaded cards=1
[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash
[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00B channels=0,1,3 keys=1,2,3 notes=DO2(36),DO#2(37),RE2(38)
[TRIGGER] bouton Moteur recu -> auto-test lance (req=43)
[STATUS] code=SELFTEST_STARTED
[SELFTEST] req=43 pos=4 status=OK bitmap=0x00B faults=0x000 tag=29B1
[SELFTEST] req=43 pos=3 status=OK bitmap=0x003 faults=0x000 tag=4C2A
[STATUS] code=TABLE_PARTIAL
[STATUS] code=TABLE_DIFFERS
[DIFF] pos=3 ancien=0x000 nouveau=0x003
[STATUS] code=TABLE_WRITTEN
[TABLE] state=PROVISIONAL flags=0x40 cards=2 keys=5
[CARD] pos=3 node=5 uid=AA10203040506070 state=OK bitmap=0x003 channels=0,1 keys=1,2 notes=DO2(36),DO#2(37)
[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00B channels=0,1,3 keys=3,4,5 notes=RE2(38),RE#2(39),MI2(40)
[SUMMARY] cards_ok=2/2 channels=5 keys=5/49 missing_keys=6-49 table=DIFFERS_WRITTEN calib=ON
```

La carte de position 3 est désormais la plus grave des deux : elle prend le bloc
grave (touches 1..2) et repousse la carte de position 4 vers l'aigu. C'est un
décalage **signalé** (`[TABLE]`, `[MAP]`), jamais silencieux.

### (c) Ajout d'un canal sur une carte existante

Exemple : la carte de position 4 passe de `0,1,3` à `0,1,2,3`.

```text
[BOOT] table=loaded cards=2
[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash
[TRIGGER] bouton Moteur recu -> auto-test lance (req=44)
[STATUS] code=SELFTEST_STARTED
[SELFTEST] req=44 pos=4 status=OK bitmap=0x00F faults=0x000 tag=29B1
[SELFTEST] req=44 pos=3 status=OK bitmap=0x003 faults=0x000 tag=4C2A
[STATUS] code=TABLE_PARTIAL
[STATUS] code=TABLE_DIFFERS
[DIFF] pos=4 ancien=0x00B nouveau=0x00F
[STATUS] code=TABLE_WRITTEN
[TABLE] state=PROVISIONAL flags=0x40 cards=2 keys=6
[CARD] pos=3 node=5 uid=AA10203040506070 state=OK bitmap=0x003 channels=0,1 keys=1,2 notes=DO2(36),DO#2(37)
[CARD] pos=4 node=6 uid=E660583883123456 state=OK bitmap=0x00F channels=0,1,2,3 keys=3,4,5,6 notes=RE2(38),RE#2(39),MI2(40),FA2(41)
[SUMMARY] cards_ok=2/2 channels=6 keys=6/49 missing_keys=7-49 table=DIFFERS_WRITTEN calib=ON
```

## Non vérifiable sans matériel réel

* latence exacte touche → CAN → PWM
* absence de perturbation audible pendant `flash_safe_execute`
* comportement d'un canal physiquement vide avant le premier auto-test réussi.
