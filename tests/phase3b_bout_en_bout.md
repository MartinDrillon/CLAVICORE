# Phase 3b — Livraison 2 : commissioning persistant et jeu

Procédure manuelle **PuTTY + matériel réel**.

## Préparation

1. Flasher `build\clavier\clavier.uf2` sur le Clavier.
2. Flasher `build\moteur\moteur.elf` par **SWD** (OpenOCD,
   `tests/moteur_phase2a.md` §E–F) : aucun `.uf2` Moteur n'existe (carte sans USB).
3. Ouvrir **PuTTY** sur le port **CDC** du Clavier.
4. Vérifier que `uid_position.h` contient les UID attendus.

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
3. Si `CLAVIER_NOTE_LOG` a été activé à la compilation, vérifier en plus les
   lignes :
   * `[NOTE] midi=M key=K pos=P ch=C`
   * ou, en cas de rejet, `[NOTE_DROPPED] midi=M reason=...`

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
[SUMMARY] cards_ok=1/1 channels=3 keys=3/49 missing_keys=4-49 table=WRITTEN
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
[SUMMARY] cards_ok=2/2 channels=5 keys=5/49 missing_keys=6-49 table=DIFFERS_WRITTEN
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
[SUMMARY] cards_ok=2/2 channels=6 keys=6/49 missing_keys=7-49 table=DIFFERS_WRITTEN
```

## Non vérifiable sans matériel réel

* latence exacte touche → CAN → PWM
* absence de perturbation audible pendant `flash_safe_execute`
* comportement d'un canal physiquement vide avant le premier auto-test réussi.
