# Architecture courante — Clavicore

**État au 2026-11-04.** Une page, à lire avant `Detail_architecture.md`.
Elle consolide les règles **en vigueur** ; elle ne remplace ni `AGENTS.md`
(invariants) ni les ADR (justification). En cas de divergence : `AGENTS.md` >
ADR acceptée > ce document. Le détail d'implémentation est dans
`docs/specs/Detail_architecture.md` et les `docs/specs/impact-adr-*.md`.

## 1. Stack

| Élément | Choix |
|---|---|
| Langage | C (Pico SDK). Pas de C++ sans ADR. |
| Carte Clavier | Pico 2 / RP2350. Matrice 80 touches, dont 8 switchs de contrôle ; 30 LED RGB ; USB composite (MIDI USB + CDC) + MIDI DIN ; maître CAN. |
| Cartes Moteur | RP2354B, jusqu'à 5 cartes × 12 solénoïdes (49 au total : 4×10 + 1×9). |
| Driver solénoïde | DRV8874PWPR, mode PH/EN, 680 µF par solénoïde, 24 V / 20 A (HLG-480). |
| Bus | CAN 500 kbit/s via can2040 (PIO + IRQ), vendu dans `firmware/common/can2040/`. |
| Alimentation cartes | 24 V → 5 V (TPS54302) → 3,3 V (TLV1117-33). |
| Diagnostic | Journal texte sur CDC (PuTTY), `docs/specs/commandes-putty.md`. PAS de SysEx de diagnostic. |
| Toolchain | CMake + Pico SDK, build Windows documenté dans `docs/specs/setup-build-flash.md`. |

## 2. Frontières de modules (non négociable)

```
firmware/clavier/   logique carte Clavier (RP2350), maître CAN, MIDI, table de commissioning
firmware/moteur/    logique carte Moteur (RP2354B), identique sur les 5 cartes, aucune notion musicale
shared/protocol/    SOURCE UNIQUE : can_protocol, commissioning_table, solenoid_profile, uid_position_format
shared/hal/         interfaces matérielles réutilisables
```

`firmware/clavier` et `firmware/moteur` **importent** `shared/protocol/` et ne
redéfinissent jamais une structure de message ni un format de table. Tout
changement d'interface partagée exige une ADR **avant** le code (invariant 5).

## 3. Invariants (rappel — texte complet dans `AGENTS.md`)

1. Aucun remapping automatique ; toute divergence (auto-test, UID au boot) est
   **signalée** (journal, SysEx STATUS/CDC, refus du canal), jamais corrigée.
2. Les cartes Moteur sont anonymes : même firmware, identité = UID 64 bits,
   aucun rôle musical embarqué.
3. La table de commissioning (UID → canal → touche) vit **uniquement** côté
   Clavier, de façon persistante : seule source de vérité.
4. Limites d'activation = **données du profil**, appliquées par la couche
   pilote Moteur. Aucun agent n'ajoute, ne durcit ni ne retire une limite
   électrique de sa propre initiative (ADR-0014).
5. Changement d'interface partagée ⇒ ADR préalable.

## 4. Règles actives par domaine

### 4.1 Commissioning et auto-test IPROPI
- Auto-test **uniquement sur demande explicite** : bouton Moteur maintenu 3 s
  (ou commande console `probe`, ADR-0027 Proposed, non implémentée). Jamais au
  boot (invariant 1).
- Le Clavier orchestre : transfert du profil, `0x401` en diffusion à **toutes**
  les cartes, collecte des réponses A+B+C, écriture de la table.
- Table = cliché du **dernier auto-test demandé**, réécrite même partielle ;
  état `FINAL` / `PROVISIONAL` signalé au journal et au boot (ADR-0009).
- Assignation **ascendante ancrée sur la note la plus grave** : position 0,
  canal occupé le plus haut = DO2 (MIDI 36) ; dans un bloc, canaux parcourus
  11 → 0 (ADR-0028, amende ADR-0010 §1) ; touches non attribuées côté aigu
  (ADR-0010).
  Configuration non contiguë : `PROVISIONAL` + `ANCHOR_SHIFT`, appliquée et
  journalisée, jamais remappée.
- Mesure non concluante (`ABORTED`/`REFUSED_BUSY`/`UNCALIBRATED`) : **bitmap
  précédent conservé** pour le même UID, sinon bitmap 0 (ADR-0021).
- Présence **ré-établie à chaque synchronisation** par la requête UID existante
  (`UID_REQUEST_MODE_PRESENCE`, timeout du scan de boot, ~500 ms) ; l'ensemble
  attendu est exactement les cartes qui répondent maintenant, les absentes sont
  signalées `BOARD_MISSING` sans bloquer les présentes (ADR-0016).
- Verdict **par UID** : chaque attendu doit être confirmé par son propre UID
  (direct ou réconcilié) ; un acquittement d'une autre carte ne compense pas un
  attendu manquant. Bus muet (`expected == 0`) = aucun transfert,
  `[PROFILE] bus muet…` et `PROFILE_SYNC_FAILED`, jamais `sync ok`.
- Acquittements attribués par `uid_tag` (`uid_position.h`, table d'amorçage) ;
  un ACK nommant par son tag une carte absente du scan la réintègre et la
  confirme ; un tag hors table reste `PROFILE_ACK_UNATTRIBUTED` (ADR-0016). Au
  plus **3 tentatives complètes**, chacune avec un nouveau `transfer_id`, puis
  réconciliation NVM explicite (ADR-0023). Le `0x401` n'est émis qu'après
  confirmation des cartes attendues.

### 4.2 Profil d'activation des solénoïdes
- Fichier unique côté Clavier : `firmware/clavier/src/solenoid_profile_config.h`.
- Transfert `PROFILE_BEGIN`/`CHUNK`/`COMMIT` (`0x420`–`0x422`) au lancement de
  l'auto-test ; validation CRC32 + contrôles structurels ; persistance flash
  Moteur en secteurs A/B avec numéro de séquence (survit coupure et reflash SWD).
- Format **v3 : 54 octets, 9 chunks, version flash 3** ; plus aucun `*_max_us` :
  la durée de chaque phase du profil est seule autorité, sans écrêtage ni rejet
  « durée > max » (ADR-0014 amendée 2026-10-25).
- Valeurs de repli compilées côté Moteur tant qu'aucun profil n'est reçu ;
  `profile_tag32` et source annoncés au boot (`PROFILE_STATUS`, `0x423`) ; toute
  divergence signalée (`PROFILE_DIVERGENT` / `PROFILE_FALLBACK_IN_USE`).
- Phase **IMPACT** (impulsion courte 100 %) avant PULL (ADR-0019).
- Le firmware Moteur ne conserve **aucune enveloppe thermique fixe** : la
  sécurité est procédurale, portée par les valeurs du profil (ADR-0014).

### 4.3 Pilote solénoïde
- HAL DRV8874 PH/EN, IPROPI via mux 74HC4067 (ADR-0001) ; inversion de
  polarité pour RELEASE ; sortie de phase avec duty nul + délai de sécurité.
- Réveil `wake_delay_us` mémorisé par verrou one-shot, évalué même en FAULT ;
  aucun réveil périodique ni rejeu de note (ADR-0024).
- Refus du pilote ventilés par compteurs additifs (13–17,
  `CAN_CARD_COUNTER_COUNT=18`) ; lisibles par `0x403`/`0x425`.
- Toute activation a une gestion d'erreur explicite : pas de
  `// TODO handle error` sur ce chemin.

### 4.4 MAINTENANCE
- Bascule par `MAINT` (position libre, non normative), refusée sans table valide
  (`MAINTENANCE_NO_TABLE`) ou pendant un auto-test ; coupe tous les solénoïdes
  à l'entrée et à la sortie ; aucune note MIDI ; une touche de note à la fois.
- Commande inchangée : `SOLENOID_COMMAND` (`0x100`, DLC 5), `command = 2`
  (`PUSH_ON`) ; le Clavier ne transmet jamais de polarité.
- Côté Moteur : polarité **inverse** (« REPEL »), duty `maint_duty_pct` et
  durée `maint_max_ms` (**défauts 50 % / 20 s**, données du profil), sans HOLD
  ni RELEASE, tenue jusqu'au relâchement ou à `maint_max_ms`. Dérogation
  invariant 4, validée par l'humain.

### 4.5 Diagnostic et observabilité
- Journal texte CDC à trois niveaux runtime (`l 0|1|2`, `l t`) ; anomalies
  toujours visibles, verdicts non masquables ; statistiques agrégées par carte
  (`m stats`) (ADR-0025). Commande `n` retirée ; `[SELFTEST_RAW]` en niveau 2.
- Chaque ligne horodatée `[T+… ms]` ; le boot annonce `fw=`, `proto=`, `build=`,
  l'état de table et le nombre de cartes ; redémarrage de carte nommé
  `CARD_REBOOTED` (ADR-0017).
- Toute action locale du Moteur est visible : `CARD_EVENT` (`0x424`, DLC 8),
  huit événements (appui court, déclenchement en attente/émis/bloqué, défaut et
  levée de canal, commande ignorée, auto-test avorté) (ADR-0018).
- `PROFILE_STATUS` répété 3 × 20 ms, arrêt au premier succès ; toute commande
  reçue non exécutée émet `COMMAND_IGNORED` avec sa raison (limiteur 1/s hors
  tag étranger) (ADR-0020, ADR-0025).

### 4.6 Clavier, keycodes, MIDI
- Keycodes non-note écrits directement dans le keymap : `T_GLOB_UP`/`T_GLOB_DOWN`,
  `T_OCT_L_UP`/`T_OCT_L_DOWN`, `T_OCT_R_UP`/`T_OCT_R_DOWN`, `ALT_B`/`ALT_D`
  modificateurs **maintenus** d'altération, `CLE_B` (armure mineure) / `CLE_D`
  (armure majeure), `TEMP_B`/`TEMP_D` altérations temporaires maintenues,
  `T_TEMP_ST_L_UP`..`T_TEMP_ST_R_DOWN` et `T_TEMP_OCT_L_UP`..`T_TEMP_OCT_R_DOWN`
  transpositions temporaires maintenues par côté (kinds 14/15, `0x0E00`.. et
  `0x0F00`..), `UNTRANSPOSE`, `MAINT`, `GET_UID`, `NONE`, `N_*`. Les positions
  dans le keymap sont **libres et non normatives** (ADR-0029 §7). Liste tenue à
  jour : `docs/specs/keycodes.md` (ADR-0026, ADR-0029, ADR-0030).
- Les `T_TEMP_ST_*`/`T_TEMP_OCT_*` n'agissent que sur les notes pressées pendant
  leur maintien, du côté visé (`keycode_position_is_left`), jamais sur une note
  déjà tenue ; demi-ton et octave sont additifs (+13 ensemble) et s'ajoutent aux
  transpositions persistantes sans les modifier. Montée/descente d'un même axe
  et côté : la dernière pressée encore enfoncée gagne ; en maintenance elles
  sont suivies sans effet (ADR-0030).
- Un appui seul de `ALT_B`/`ALT_D` n'émet rien ; la bascule n'a lieu qu'à l'appui
  d'une note modificateur déjà enfoncé, avant `note_state_press`, puis
  transposition. Dernière pression gagne entre enharmoniques. Une note déjà
  tenue n'est pas modifiée. Une nouvelle armure efface les surcouches `ALT`.
- `UNTRANSPOSE` remet à zéro les transpositions **persistantes**, l'armure et
  les surcouches d'altération (ADR-0011, ADR-0029) ; il n'annule pas une
  transposition temporaire encore maintenue (ADR-0030).
- Notation scientifique : MIDI 36 = DO2, 84 = DO6 (ADR-0008).
- Ordre de boucle principale : anneau RX CAN, puis MIDI ; `PIO0_IRQ_0` garde la
  priorité la plus haute.
- MIDI : USB + DIN en sortie ; MIDI IN interprété puis transmis au Moteur.

### 4.7 CAN
- IDs et DLC définis **uniquement** dans `shared/protocol/can_protocol.h` ;
  table détaillée dans `docs/specs/CAN.md`.
- `0x100` SOLENOID_COMMAND, `0x401` IPROPI_SELFTEST_REQUEST,
  `0x402` IPROPI_SELFTEST_TRIGGER, `0x403`/`0x425` compteurs,
  `0x420`–`0x423` profil, `0x424` CARD_EVENT, `0x601` IPROPI_OHM.
- Toute trame nouvelle ou modifiée = ADR + mise à jour de `shared/protocol/` +
  signalement des modules dépendants.

### 4.8 Combos de positions
- Table dans `firmware/clavier/src/keymap.c` (type `keymap_combo_t`,
  `COMBO_MAX_POSITIONS` = 8, sentinelle `{NULL,0u,0u,NONE}`), livrée vide ;
  moteur pur `firmware/clavier/src/combo.h/.c` à horloge injectée (ADR-0031).
- Une entrée = N positions (2..8) → un keycode, avec `timeout_ms` par entrée.
  Seules les positions membres sont retardées ; la fenêtre court depuis le plus
  ancien appui retenu, même lot = déclenchement immédiat.
- Sortie relâchée au premier relâchement d'un membre ; relâchement avant
  complétion = vidage immédiat ; précédence au plus de positions puis au premier
  index ; aucun nouveau déclenchement tant qu'un combo est actif ; expiration en
  fin de lot.
- Binding libre (aucun mode, aucun gating). Une note de binding est émise
  **littéralement** (sans armure, altération ni transposition), via CAN + USB,
  hors `note_state`, et compte comme note active.
- Vérification au boot sans correction : `[COMBO] table error index=%u
  reason=%s`. Aucune position de combo n'est normative (ADR-0029 §7).
- Détail : `docs/specs/combos.md`.

## 5. Règles de travail

- Chercher avant de lire, lire par plage de lignes ; déléguer « où est défini
  X » à l'Explorateur.
- Aucune interface partagée dupliquée ; aucun refactor silencieux.
- Toute fonction touchant CAN, MIDI ou solénoïdes a un test hôte ou une
  procédure manuelle dans `tests/`.
- Aucun ajout, durcissement ni retrait de limite électrique sans validation
  humaine préalable (« Question sécurité »).

## 6. Validation acquise / non acquise

- **Acquis (2026-11-04, ADR-0031)** : `powershell -File
  tests\host\run_host_tests.ps1` → **1312 vérifications, 0 échec** (dont
  `test_combo` **183**) ; builds Clavier (cible `pico2`) et Moteur (cible
  `moteur_rp2354b`) **0 erreur / 0 warning**. Validation **matérielle** des
  combos non acquise.
- **Acquis (2026-11-04, ADR-0030)** : `powershell -File
  tests\host\run_host_tests.ps1` → **1129 vérifications à cette date** (1312
  depuis ADR-0031), 0 échec ; builds Clavier (cible `pico2`) et Moteur (cible
  `moteur_rp2354b`) **0 erreur / 0 warning**. Validation **matérielle** des
  transpositions temporaires non acquise.
- **Acquis (2026-10-31, correction de la synchronisation du profil)** :
  compilation Clavier et Moteur OK, **1044 vérifications hôte, 0 échec**
  (`tests/host/run_host_tests.ps1`), dont `test_profile_sync` **46/46**. Détail
  historique du 2026-10-24 (581 vérifications, quatre profils) :
  `docs/specs/validation-adr-0023-25-2026-10-24.md`.
- **Non acquis** : validation **matérielle** des chemins ADR-0023/0024/0025 et
  du profil v3 ; pour la synchronisation, les essais **de banc** bus muet, carte
  branchée après le boot du Clavier, chaîne complète et alignement `[CARD] pos=`
  restent à exécuter (`tests/procedure_adr0023_sync_profil.md`). Campagnes
  `procedure_adr0024_reveil_uptime.md` et `procedure_adr0025_journal_niveaux.md`
  également en attente.

## 7. Où lire quoi

| Besoin | Fichier |
|---|---|
| Invariants, rôles, workflow | `AGENTS.md` |
| Règles actives (ce document) | `docs/adr/ARCHITECTURE_CURRENT.md` |
| Trouver une ADR | `docs/adr/INDEX.md` |
| Détail firmware (règles) | `docs/specs/Detail_architecture.md` |
| Historique daté des implémentations | `docs/specs/historique-implementation.md` |
| Planification historique (ne fait pas foi) | `docs/specs/Roadmap.md` |
| Trames CAN | `docs/specs/CAN.md` |
| Format et cycle de vie du profil | `docs/specs/profil-solenoides.md` |
| Réglage du profil au banc | `docs/procedure-profil-solenoides.md` |
| Console PuTTY | `docs/specs/commandes-putty.md` |
| Keycodes | `docs/specs/keycodes.md` |
| Combos de positions | `docs/specs/combos.md` |
| Build / flash | `docs/specs/setup-build-flash.md` |
| Profondeur de test N0/N1/N2 | `docs/harness/niveaux-validation.md` |
