# Bring-up — commissioning provisoire et caractérisation des bobines (ADR-0009)

Procédure **manuelle** de mise en place progressive du système, telle qu'annoncée
pour les journées d'assemblage. Décisions de référence :

- [ADR-0009](../docs/adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)
  (table provisoire, PROBE 5 ms, `IPROPI_OHM` `0x601`)
- [ADR-0004](../docs/adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md)
  (auto-test IPROPI, table et commande solénoïde)
- [ADR-0010](../docs/adr/adr-0010-ancrage-grave-mapping-commissioning.md)
  (règle d'assignation **ascendante** : position 0, canal 0 = DO2 ; motif
  `ANCHOR_SHIFT`)
- [ADR-0006](../docs/adr/adr-0006-mode-maintenance-push-only.md) /
  [ADR-0007](../docs/adr/adr-0007-hold-limite-20s.md) (modes d'activation)
- [docs/specs/CAN.md](../docs/specs/CAN.md) (trames, lignes de journal et règle
  d'assignation)
- [docs/specs/impact-adr-0009.md](../docs/specs/impact-adr-0009.md) et
  [impact-adr-0010.md](../docs/specs/impact-adr-0010.md) (fichiers à revoir)

## 1. Objectif

Mettre en service le système **sans** exiger un autotest complet des 5 cartes, en
s'appuyant sur :

1. une table de commissioning **réécrite à chaque autotest demandé**, même
   partielle, marquée `FINAL` ou `PROVISIONAL` et toujours signalée ;
2. une estimation de **résistance** (`ohm_est`) et de **courant à 100 % de duty**
   (`i100`) par bobine, pour ne conserver que des bobines homogènes ;
3. des **lignes `[MAP]`** qui disent sans calcul quelle touche frappe quel
   solénoïde.

## 2. Pré-requis

- Clavier (Pico 2) + 1 à 5 cartes Moteur (RP2354B), CANH/CANL câblés, masses
  communes.
- `R24` (120 Ω) soudée **uniquement** aux deux extrémités physiques du bus.
- Alimentation 24 V — 20 A disponible (HLG-480) pour la phase 2.
- PC connecté au Clavier en USB, **PuTTY** ouvert sur le port COM CDC.
- Oscilloscope différentiel (ou 2 voies, mode A-B) pour la phase 1.
- 1 bobine de référence mesurée au multimètre (ohm), pour recouper `ohm_est`.

## 3. Build, flash et tests

### 3.1 Tests hôte (obligatoires avant flash)

```powershell
powershell -File tests\host\run_host_tests.ps1
```

Ces tests couvrent les helpers purs : `can_protocol` (dont le codec `0x601`),
`commissioning_table` (dont le format v2 et la lecture d'une v1) et `diag_log`.
Attendu : aucune ligne `ECHEC`.

### 3.2 Clavier

Procédure validée : [`tests/build_clavier_windows.md`](build_clavier_windows.md).

```powershell
cmake --build "build/clavier"
```

Le mode diagnostic (`IPROPI_RAW` + `IPROPI_OHM`) est **actif par défaut**
pendant le bring-up (`CLAVIER_SELFTEST_CALIBRATION` à `ON`, ADR-0009 §7) : aucun
build spécial n'est nécessaire. Pour le désactiver :

```powershell
cmake -S "firmware/clavier" -B "build/clavier" -DCLAVIER_SELFTEST_CALIBRATION=OFF
cmake --build "build/clavier"
```

Artefact : `build/clavier/clavier.uf2` (copie sur le Pico 2 en mode BOOTSEL).

### 3.3 Moteur

> **Aucune procédure de build Moteur n'est consignée comme validée à ce jour.**
> La commande ci-dessous reprend la forme de celle du Clavier ; `PICO_BOARD`
> vaut `moteur_rp2354b` par défaut dans `firmware/moteur/CMakeLists.txt`. Sa
> validation est à la charge du **Validateur** ; la consigner dans `tests/` une
> fois confirmée.

```powershell
cmake -S "firmware/moteur" -B "build/moteur" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
cmake --build "build/moteur"
```

Variante utile pour la phase 2 (diagnostic horloge désactivé) :
`-DMOTEUR_ENABLE_CLOCK_TEST=OFF`.

### 3.4 Fichiers à modifier lors de l'implémentation ADR-0009

La liste exhaustive (interface partagée, lignes d'appel, tests à ajouter) est
dans [`docs/specs/impact-adr-0009.md`](../docs/specs/impact-adr-0009.md).
Aucune de ces modifications n'est faite par le Documentaliste.

## 4. Avant de brancher des bobines

1. Remplir `firmware/clavier/src/uid_position.h` : **toutes** les cartes réellement
   présentes, avec leur UID relevé (`tests/can_phase3_uid.md`) et leur position
   physique dans le nœud (`position = nœud CAN − 2`).
   - Une carte présente mais absente de ce fichier est **exclue** de la table et
     signalée `UID_NOT_IN_TABLE`.
   - Une carte listée mais muette produit `MISSING_BOARD` et rend la table
     `PROVISIONAL` (elle ne bloque plus l'écriture).
2. Vérifier au boot, sans aucune bobine : `[TABLE] invalid ...` (pas encore de
   table) ou `[TABLE] loaded state=... flags=...`, puis envoyer un premier
   autotest et vérifier `bitmap=0x000` sur toutes les cartes : aucun canal
   occupé sans bobine.

## 5. Phase 1 — chaîne complète des 6 cartes, quelques bobines

But : intégrité du bus et comportement électrique du système complet.

1. Relier les 6 cartes (5 Moteur + Clavier) en chaîne, terminaisons aux deux
   extrémités, 24 V établie.
2. Disposer les bobines disponibles sur des canaux répartis **au hasard** (par
   exemple canal 0 d'une carte, canal 7 d'une autre) — le but est de vérifier la
   chaîne, pas le mapping musical.
3. Lancer un autotest (bouton d'une carte Moteur maintenu 3 s).
4. Relever au journal :
   - une ligne `[SELFTEST]` par carte répondante ;
   - `[TABLE] loaded state=... flags=... cards=5 keys=<nb de bobines>` ;
   - les lignes `[MAP]` (une par canal occupé) ;
   - les lignes `[SELFTEST_RAW] ... ohm_est=... i100=...` et la synthèse
     `[SELFTEST_OHM]` par carte.
5. À l'oscilloscope, **pendant** un autotest puis **pendant** du jeu :
   - niveaux différentiels CANH/CANL aux **deux extrémités** de la chaîne
     (état récessif et état dominant) ;
   - fronts sans ringing excessif, pas d'oscillation basse fréquence ;
   - absence de trames perdues : les `[SELFTEST]` attendues arrivent toutes ;
   - sur l'alim 24 V : chute de tension pendant les activations simultanées.
6. **Attendu** : `keys` = nombre de bobines réellement branchées ;
   `state=PROVISIONAL flags=...` tant que les 49 touches ne sont pas couvertes
   (`flags=0x40` seul si les positions présentes ne forment pas `0..K−1`,
   ADR-0010) ; aucune carte silencieuse non signalée.

## 6. Phase 2 — une carte Moteur seule, 10 à 12 solénoïdes

But : comportement électrique d'une carte sous forte charge, avec activations
**simultanées et répétées** pilotées au clavier.

1. Débrancher la carte du reste de la chaîne (câblage réduit), la relier au
   Clavier seul, terminaisons adaptées.
2. Monter 10 à 12 solénoïdes sur cette carte.
3. Autotest. La règle est **ascendante** (ADR-0010) : une carte seule reçoit les
   touches à partir de la **touche 1 (DO2)**, soit 12 bobines → touches `1..12`
   (`ch=0` → touche 1 = DO2). Le vérifier sur les lignes `[MAP]`.
   - Si la carte est déclarée à la position qu'elle occupera réellement dans le
     montage final, le mapping lu est celui du montage final.
   - Si elle est déclarée à une position haute (banc isolé, position 0 absente),
     le bloc grave lui revient quand même et la table porte `flags=0x40`
     (`ANCHOR_SHIFT`) : **c'est attendu** sur un banc partiel, pas une erreur.
4. Relever `ohm_est` et `i100` de chaque canal, et la synthèse `[SELFTEST_OHM]` :
   - `spread` élevé (par exemple > 15 %) ⇒ bobines hétérogènes, repérer
     `ohm_min`/`ohm_max` ;
   - `warn=i100>3.0A` ⇒ bobine dont le courant établi à 100 % de duty sort de la
     fenêtre 2–3 A.
5. Recouper `ohm_est` d'**une** bobine avec la mesure au multimètre (la même
   bobine, à froid) :
   - ratio ≈ 1.00 ⇒ la chaîne ADC/VM/VREF est cohérente ;
   - écart systématique ⇒ signaler l'écart (fenêtre de mesure, `VM`, `VREF`)
     plutôt que corriger une constante en aveugle.
6. Frapper des accords répétés au clavier (activations simultanées, puis à
   répétition) et surveiller à l'oscilloscope : chute de la 24 V, échauffement
   des bobines, régularité des impulsions.

## 7. Phase 3 — installation progressive dans le corps du clavecin

Pour **chaque** carte ajoutée au bus :

1. Mettre sous tension la nouvelle carte seule, relever son UID si inconnu,
   l'ajouter dans `uid_position.h` **à sa position physique** (la carte qui
   porte `DO2` doit être déclarée en position 0, ADR-0010), reflasher le Clavier
   si le fichier a changé.
2. Relancer un autotest, vérifier :
   - `[TABLE] loaded ... cards=<n>` : le nombre de cartes attendu ;
   - `flags` : `MISSING_BOARD` doit disparaître pour les positions désormais
     présentes ;
   - les lignes `[MAP]` : les blocs de touches ont bougé (règle ascendante,
     ADR-0010), ce qui est **normal** et signalé ; c'est la référence à utiliser
     pour jouer ;
3. Installer les solénoïdes de cette carte un par un, en relançant un autotest
   après chaque ajout pour confirmer la prise en compte du canal.
4. Vérifier physiquement que chaque solénoïde répond bien à la touche annoncée
   par `[MAP]` (et non à une touche voisine).

> La table devient `state=FINAL flags=0x00` quand **toutes** les positions
> attendues répondent, qu'aucune carte n'est en échec ou en défaut, que les
> positions présentes forment `0..K−1` (position 0 déclarée, aucun trou) et que
> `total_occupied == 49`. Toute autre situation reste utilisable et signalée.

## 8. Grille de relevés (à remplir)

| Date | Carte (pos) | Canal | Touche | `idle` | `probe` | `delta` | `ohm_est` (Ω) | multimètre (Ω) | `i100` (A) | Occurrence |
|---|---|---|---|---|---|---|---|---|---|---|
|  |  |  |  |  |  |  |  |  |  |  |

Critères de tri des bobines :

- `ohm_est` proche de la moyenne de la série (viser un `spread` faible) ;
- `i100` dans la fenêtre 2–3 A à 100 % de duty ;
- à défaut, écarter ou rembobiner les bobines hors gabarit.

## 9. Critères de réussite

- [ ] Aucun autotest ne part au boot
- [ ] Le canal occupé le plus bas de la carte de **position 0** joue **DO2**
      (touche 1, MIDI 36) — règle ascendante, ADR-0010
- [ ] Un autotest demandé écrit la table **même** avec une seule carte présente
- [ ] `[TABLE] loaded state=PROVISIONAL flags=0x..` est accompagné de
      `[STATUS] code=TABLE_PROVISIONAL`
- [ ] Les lignes `[MAP]` donnent la correspondance canal → touche → note
- [ ] `[SELFTEST_RAW]` porte `ohm_est` et `i100` sur les canaux occupés
- [ ] `[SELFTEST_OHM]` sort une synthèse par carte, avec `warn=` si pertinent
- [ ] Aucune divergence UID/bitmap/table silencieuse (tout est journalisé)
- [ ] Une carte présente mais absente de la table a ses canaux **refusés**
- [ ] Le bus reste sain avec les 6 cartes en chaîne (relevés oscillo consignés)

## 10. Sécurité (invariants)

- Aucune bobine n'est activée hors des bornes du pilote : PROBE reste bornée
  (`SOLENOID_PROBE_MAX_MS` = 10 ms, duty 20 %), le HOLD est coupé à 20 s
  (ADR-0007), la MAINTENANCE à 10 s (ADR-0006).
- Les essais à répétition de la phase 2 relèvent de la **surveillance humaine** :
  pas de mode automatique de contrainte, pas de compteur embarqué (propositions
  écartées par l'utilisateur).
- Ne jamais interpréter `i100` comme une limite de sécurité : c'est une
  estimation qui ignore la chute de la 24 V en charge et la régulation interne
  du DRV8874.

## 11. Ce que cette procédure ne valide pas

- la latence bout-en-bout appui → CAN → PWM sous charge réelle ;
- le comportement sur 49 bobines à pleine charge simultanée ;
- la résistance de boucle du bus et l'immunité CEM sur l'instrument monté.
