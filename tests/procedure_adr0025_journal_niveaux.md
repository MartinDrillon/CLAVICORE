# Procédure de validation — ADR-0025 (journal par niveaux et statistiques agrégées)

Procédure du **Validateur**, issue d'[ADR-0025](../docs/adr/adr-0025-journal-diagnostic-niveaux-runtime-agregation.md)
(`Accepted`) et de
[`docs/specs/impact-adr-0025.md`](../docs/specs/impact-adr-0025.md).

> **État au 2026-10-24** : ADR-0025 est implémentée et validée hôte
> (`test_diag_out`, `test_console_tuning`, formateurs `m stats`). Les scénarios
> de banc §3 restent à exécuter.

> **Objectif double** : moins de lignes en fonctionnement normal, sans jamais
> perdre une divergence, un défaut, une action physique ou une mesure manquante.
> Le mode `l 0` n'est pas un mode silencieux absolu : il conserve les verdicts
> d'anomalie.

## 0. Pré-requis

* Builds générés dans un dossier propre au worktree (voir `AGENTS.md`).
* Clavier et Moteur flashables ; pour le test du mélange de versions, une carte
  Moteur ancienne est facultative.
* PuTTY sur le CDC du Clavier, **Local echo** et **Local line editing** activés.
* Journaux dans `build/logs/`, par exemple `adr0025_putty.log`.
* Pour vérifier les événements Moteur : analyseur CAN facultatif, mais
  recommandé pour B8.
* Une touche commissionnée, deux cartes Moteur pour les comptes de lignes.

## 1. Builds et tests hôte

### 1.1 Build de bring-up (TRACE actif, défaut)

```powershell
New-Item -ItemType Directory -Force "build\logs" | Out-Null
powershell -File tests\host\run_host_tests.ps1 *> "build\logs\adr0025_host.log"
cmake --build "build/clavier" *> "build\logs\adr0025_clavier.log"
cmake --build "build/moteur" *> "build\logs\adr0025_moteur.log"
```

Si les dossiers n'existent pas, exécuter d'abord les configurations de
[`build_clavier_windows.md`](./build_clavier_windows.md) et
[`build_moteur_windows.md`](./build_moteur_windows.md), puis relancer.

Options du build de bring-up :

* `CLAVIER_DIAG_DEFAULT_LEVEL=1`
* `CLAVIER_DIAG_TRACE=ON`
* `MOTEUR_DIAG_TRACE=ON`
* `CLAVIER_SELFTEST_CALIBRATION=ON`
* `CLAVIER_CDC_CONSOLE=ON`

### 1.2 Build final silencieux

Configurer deux dossiers propres, par exemple `build/clavier-final` et
`build/moteur-final` :

```powershell
cmake -S "firmware/clavier" -B "build/clavier-final" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2 -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCLAVIER_DIAG_DEFAULT_LEVEL=0 -DCLAVIER_DIAG_TRACE=OFF -DCLAVIER_CDC_CONSOLE=ON -DCLAVIER_SELFTEST_CALIBRATION=ON
cmake --build "build/clavier-final" *> "build\logs\adr0025_clavier_final.log"

cmake -S "firmware/moteur" -B "build/moteur-final" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=moteur_rp2354b -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DMOTEUR_DIAG_TRACE=OFF
cmake --build "build/moteur-final" *> "build\logs\adr0025_moteur_final.log"
```

Adapter les chemins de toolchain à `build_clavier_windows.md` §2 si nécessaire ;
le point vérifié est la présence des options, pas la recopie des chemins.

### 1.3 Critères hôte

```powershell
Select-String -Path "build\logs\adr0025_host.log" -Pattern "FAIL|ECHEC|echec|verifications|error|warning" | Select-Object -Last 20
Select-String -Path "build\logs\adr0025_*.log" -Pattern "error|warning|FAILED" | Select-Object -Last 20
```

* sortie 0 et tous les cas L-01 à L-25 exécutés ;
* cas présents dans les deux profils de compilation ;
* aucun avertissement nouveau sur les fichiers modifiés ;
* aucun changement de `CAN_PROTO_VERSION`, de trame ou de DLC.

## 2. Tests hôte attendus

| Cas | Vérification |
|---|---|
| L-01 à L-05 | commande `l`, niveaux, timestamp, refus de `l 2` sans TRACE, disparition de `n` |
| L-06/L-07 | aucune note/TAG à `l 0`, une note à `l 2` |
| L-08/L-09 | silence sain à `l 0`, boot/UID/table/map à `l 1` |
| L-10 | alertes toujours présentes à tous les niveaux |
| L-11/L-12 | PUSH/RAW uniquement en trace, timeout/refus toujours |
| L-13 à L-15 | `ohm_manquants`, warning courant, `calib=OFF` |
| L-16 à L-20 | agrégation, champs, carte ancienne, carte muette, réponse hors balayage |
| L-21/L-22 | tag étranger sans événement, limiteur ciblé |
| L-23 | `MANUAL_TEST_STARTED`, valeur 0 |
| L-24/L-25 | options de compilation et verdicts conservés |

Si les tests ne compilent qu'un seul profil de macros, la validation est
incomplète.

## 3. Banc — comportement runtime

### B1. Boot sain et commande `l`

1. Démarrer avec le build final (`CLAVIER_DIAG_DEFAULT_LEVEL=0`) ou, avec le
   build de bring-up, laisser les lignes de boot `l 1` s'afficher puis taper
   `l 0`.
2. Attendre sans jouer 60 s.
3. Attendu **après `l 0`** : aucune ligne périodique, aucun `[BOOT]`, `[HELP]`,
   `[UID]` ni `[MAP]` supplémentaire. Avec le build de bring-up, les lignes de
   boot déjà émises avant `l 0` sont normales ; elles ne sont absentes au boot
   qu'avec le défaut final à 0.
4. Taper `l` → `[CONSOLE] log niveau=0 timestamp=on trace=on`.
5. Taper `l 1`, redémarrer ou rejouer un scan : les lignes de boot/UID/table
   apparaissent selon l'action disponible.
6. Taper `l 0` : les traces suivantes disparaissent.

### B2. Notes et bruit de routage

1. À `l 0`, jouer une touche commissionnée sur un système à deux cartes.
2. Attendu : la note joue ; **aucune** `[NOTE]` et **aucun**
   `[CARD] event=COMMAND_IGNORED raison=TAG` pour l'autre carte.
3. Passer `l 2`, jouer à nouveau : une ligne `[NOTE]` par note routée.
4. Revenir `l 0`, provoquer une note non routée : `[NOTE_DROPPED]` reste visible.

### B3. `p send` et auto-test

1. `p send` à tag identique : un seul verdict `[PROFILE] sync ok`, sans
   `sync debut` ni `reglage: OK`.
2. Provoquer une perte transitoire si l'outillage le permet : une ligne de
   reprise puis un verdict unique.
3. Lancer un auto-test à `l 0` : `[SUMMARY]` et les éventuelles cartes en échec
   sont visibles ; aucun `[SELFTEST_RAW]`.
4. Passer `l 2`, relancer : les RAW apparaissent.
5. Avec `c` OFF, vérifier `calib=OFF` dans `[SUMMARY]` et l'absence de mesures
   présentées comme des échecs.

### B4. `m stats` agrégé

1. Taper `m stats` à `l 0` au repos.
2. Attendu : une ligne `[CARD] pos=N stats …` par carte, puis un `[STATS]`.
3. Vérifier les champs obligatoires :
   `can_errors`, `tx_fail`, `rx_overrun`, `solenoid_commands_ok`,
   `solenoid_rejected`.
4. Les autres compteurs n'apparaissent que s'ils sont non nuls.
5. Avec une carte ancienne : `ext=n/a`, aucune ligne
   `[STATUS] NO_RESPONSE pos=N` pour cette carte.
6. Avec une carte totalement muette : aucune ligne stats pour elle, un seul
   `NO_RESPONSE`.
7. Rejouer `m stats` pendant un jeu ou un auto-test : refus inchangé, pas de
   trafic.

### B5. Événements santé/action

1. Appui court/long sur le bouton Moteur : `MANUAL_TEST_STARTED` visible à
   `l 0`, avec le canal sollicité.
2. `TRIGGER_PENDING` et `TRIGGER_SENT` visibles à `l 1`/`l 2`, pas à `l 0`.
3. Un canal bloqué produit `TRIGGER_BLOCKED` et un `[STATUS]` visible à `l 0`.
4. Un défaut de canal produit `CHANNEL_FAULT` visible à `l 0` ;
   `CHANNEL_FAULT_CLEARED` n'apparaît qu'à `l 1`/`l 2`.
5. Un refus ciblé (canal invalide/non occupé/pilote) produit au plus une ligne
   par motif et par seconde, toujours visible à `l 0`.

### B6. Horodatage

1. Avec l'horodatage actif, vérifier le préfixe `[T+… ms]` sur une ligne.
2. Taper `l t` : les lignes suivantes n'ont plus le préfixe, contenu inchangé.
3. Refaire `l t` : préfixe restauré.
4. Vérifier que `l t` ne change pas le niveau courant.

### B7. OHM et alertes

1. Mode `c` ON, auto-test à `l 0` : les warnings de courant éventuels restent
   visibles ; les résumés OHM ordinaires sont masqués.
2. Un canal demandé sans OHM produit `ohm_manquants=0xNNN` dans `[SELFTEST]`.
3. Mode `c` OFF : `calib=OFF` dans `[SUMMARY]`, pas de `ohm_manquants` artificiel.

### B8. Build final et trafic Moteur

1. Flasher Clavier final (`TRACE=OFF`, niveau0) et Moteur final
   (`MOTEUR_DIAG_TRACE=OFF`).
2. Vérifier par analyseur ou par absence de lignes :
   * `TRIGGER_PENDING`, `TRIGGER_SENT`, `CHANNEL_FAULT_CLEARED` ne sont plus
     émis ;
   * `MANUAL_TEST_STARTED`, `TRIGGER_BLOCKED`, `CHANNEL_FAULT`,
     `SELFTEST_ABORTED` et le premier refus ciblé restent émis ;
   * `m stats` reste une commande compilée mais ne reçoit aucune réponse
     (`MOTEUR_DIAG_TRACE=OFF`) ; ne pas l'utiliser comme test de présence ;
   * `l 2` répond indisponible ;
   * `l 0` continue d'afficher les verdicts d'erreur.
3. Rejouer une touche et un `p send` : aucune régression fonctionnelle.

## 4. Mesures avant/après

Utiliser le même scénario : deux cartes, une note, un `p send`, un auto-test,
`m stats`, puis jeu de toutes les touches.

| Scénario | Avant | Après `l 0` |
|---|---|---|
| Une note, deux cartes | 1 événement TAG parasite (N−1) | 0 |
| `p send` nominal | annonce + début + OK (plusieurs lignes) | 1 verdict |
| `m stats` sans ADR-0024 | 13 × 2 + 1 lignes | 2 + 1 |
| `m stats` avec ADR-0024 | 18 × 2 + 1 lignes | 2 + 1 |
| Auto-test complet | RAW par canal/carte | `[SUMMARY]` + échecs |
| Session saine `l 0` | traces périodiques et routage | aucune ligne périodique |

Consigner les nombres réels, pas seulement « moins ». Une ligne en moins qui
supprime une alerte est un échec.

## 5. Résultats à consigner

Créer `build/logs/adr0025_banc.md` :

| Élément | Valeur |
|---|---|
| Versions Clavier/Moteur, options de compilation | |
| Nombre de lignes par scénario avant/après | |
| Niveaux testés, timestamp | |
| `m stats` : cartes, champs, `ext=n/a` | |
| Tag étranger : événements et compteur | |
| Événement 0 : nom, valeur, payload | |
| OHM/warnings/`ohm_manquants`/`calib` | |
| Build final : événements conservés/retirés | |
| Échecs ou troncatures de ligne | |
| Conclusion runtime et build final | |

## 6. Critères de sortie

ADR-0025 est validée lorsque :

* L-01 à L-25 passent dans les deux profils de compilation ;
* une session saine à `l 0` ne produit aucune ligne périodique ;
* toutes les alertes de la décision 2 restent visibles à `l 0`, y compris OHM
  manquant et warning de courant ;
* `m stats` tient sur une ligne par carte + bilan, sans troncature ;
* `ext=n/a` n'est pas confondu avec une carte morte ;
* le tag étranger n'émet aucun événement ;
* l'événement 0 est `MANUAL_TEST_STARTED`, valeur 0 et payload inchangé ;
* le build final retire les traces ordinaires sans retirer les événements de
  santé/action ni les verdicts ;
* aucune trame, aucun DLC et `CAN_PROTO_VERSION=1` ne changent.

Les points purement esthétiques du journal ne sont pas des critères : seules la
suppression documentée, la conservation des verdicts et l'absence de troncature
sont mesurées.
