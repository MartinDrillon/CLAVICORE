# Impact ADR-0010 — ancrage grave de la règle d'assignation des touches

Note de référence pour le **Codeur** et le **Validateur**. Elle liste, fichier par
fichier, ce qu'il faut revoir parce que
[ADR-0010](../adr/adr-0010-ancrage-grave-mapping-commissioning.md) (statut
`Accepted`, 2026-10-20) **inverse la règle d'assignation des touches** de la
table de commissioning et ajoute **un motif d'état**.

Les chemins de ligne cités sont ceux **avant** modification (état du 2026-10-21) :
ils servent de point d'entrée, pas de référence stable.

> **État : implémenté et validé le 2026-10-21.** Tests hôte : 221 vérifications,
> 0 échec ; builds Clavier et Moteur sans warning (Clavier `text 68 440`,
> `bss 11 060`). Procédure conservée dans
> [`../../tests/build_clavier_windows.md`](../../tests/build_clavier_windows.md)
> §5. Le tableau du §0 et les points d'entrée restent utiles comme référence de
> revue, mais **le travail est fait** : ne pas relire cette note comme une liste
> de tâches à exécuter.
>
> Configuration actuelle du seed : `firmware/clavier/src/uid_position.h`
> position 0 **vide** → table jouable mais `PROVISIONAL flags=0x40` tant que la
> carte grave n'y est pas déclarée (voir §5).

## 0. Règle implémentée (rappel normatif)

`commissioning_build_runtime` (`shared/protocol/commissioning_table.c`) doit
devenir le **miroir** de la version actuelle :

| | Avant (ADR-0004 §6) | Après (ADR-0010) |
|---|---|---|
| Ancre | touche **49** (`COMMISSIONING_KEY_MAX`) | touche **1** (`COMMISSIONING_KEY_MIN`) |
| Parcours des positions | **4 → 0** | **0 → 4** |
| Sens du remplissage | descendant (`current_high`, `block_start = current_high - N + 1`) | ascendant (`current_low`, `block_start = current_low`, puis `current_low = block_end + 1`) |
| Dans un bloc | canaux occupés croissants → touches croissantes | **inchangé** |
| Touches non attribuées | au **grave** | à l'**aigu** |

Invariants de comportement à conserver : canaux non occupés sautés (trous
autorisés) ; `COMMISSIONING_TOO_MANY_KEYS` si le total dépasse 49 ou si un bloc
sort de `1..49` ; `position_first_key[]` / `position_last_key[]` gardent leur sens
(bornes du bloc d'une position) ; `commissioning_lookup_midi_note` **inchangé**.

**Nouveau motif** : `COMMISSIONING_FLAG_ANCHOR_SHIFT = 0x40`, positionné quand
les positions présentes ne sont **pas** exactement `0..K−1` (trou de position ou
position 0 absente). Il entre dans la règle d'état existante (`flags != 0` ⇒
`PROVISIONAL`), sans changement de `format_version` (le champ `flags` existe
depuis ADR-0009 §3, bit 6 était réservé).

Précision pour lever une ambiguïté : une position **présente mais à bitmap vide**
(carte `test_status != OK`, motif `BOARD_NOT_OK`) ne consomme aucune touche — le
bloc suivant remonte donc d'autant — et cela reste couvert par `BOARD_NOT_OK`,
**pas** par `ANCHOR_SHIFT`, qui ne traite que l'absence ou le trou de position.

## 1. `shared/protocol/commissioning_table.[ch]` — cœur du sujet

Fichiers qui atteignent ce module (revue obligatoire, même si aucun changement
n'y est attendu) : `firmware/clavier/src/commissioning.h` (`:7` inclut
`commissioning_table.h`), `firmware/clavier/src/diag_log.h` (`:8`),
`firmware/clavier/src/can_link.c`, `tests/host/test_phase3_helpers.c` (`:7` et
`:10`), et `firmware/moteur/CMakeLists.txt` (compile le module **sans
l'utiliser** : le build Moteur doit rester vert).

| Fichier | Point d'entrée | Revue requise |
|---|---|---|
| `shared/protocol/commissioning_table.c` | `:130` `commissioning_build_runtime`, `:132` `current_high`, `:150` boucle `position` décroissante, `:167-172` `block_start` / `position_first_key` / `position_last_key`, `:194` `current_high = block_start - 1` | inverser l'ancre et le sens du parcours selon le tableau du §0 ; ne pas toucher `commissioning_lookup_midi_note` (`:202`), ni l'encodage/décodage flash (`:220`, `:281`) |
| `shared/protocol/commissioning_table.h` | `:54-59` motifs `0x01`…`0x20`, `:60` `COMMISSIONING_FLAG_ALL 0x3F` | ajouter `COMMISSIONING_FLAG_ANCHOR_SHIFT 0x40` et étendre `COMMISSIONING_FLAG_ALL` à `0x7F` ; `commissioning_table_state_from_flags()` (`:104`) reste inchangée (`FINAL` ⇔ `flags == 0`) |

## 2. Clavier — calcul du motif et journal

| Fichier | Point d'entrée | Revue requise |
|---|---|---|
| `firmware/clavier/src/can_link.c` | `:1207` `finish_selftest_session`, `:1211` `uint8_t flags = 0`, `:1227`/`:1232`/`:1235`/`:1242`/`:1262`/`:1277` pose des motifs | ajouter le calcul de `ANCHOR_SHIFT` (positions retenues ≠ `0..K−1`), **avant** l'écriture ; il se combine en OR avec les motifs existants |
| `firmware/clavier/src/can_link.c` | `:237` `emit_map_lines` (lit `position_first_key`), `:273-275` `emit_table_entry_for_position`, `:851` résumé `keys=`, `:907` `preview->position_last_key` | **aucun changement de code** attendu (ces points lisent les bornes du bloc) ; à vérifier au test : les bornes doivent désormais décrire le bloc grave en premier |
| `firmware/clavier/src/commissioning.c` / `.h` | `:72`, `:112` appels `commissioning_build_runtime` ; `table_flags` (`commissioning.h:15`) | aucun changement attendu : le motif circule par le champ `flags` existant |
| `firmware/clavier/src/diag_log.c` | `:284-294` `diag_format_table_state_line` (`[TABLE] ... flags=0x%02X`) | aucun changement de format ; `0x40` doit simplement s'afficher |
| `firmware/clavier/src/uid_position.h` | `:10-15` commentaire de convention, `:20` position 0 vide | commentaire corrigé (documentation) ; **la donnée** (UID de la carte grave en position 0) est à saisir par l'utilisateur — voir §5 |

## 3. Tests

| Fichier | Point d'entrée | Revue requise |
|---|---|---|
| `tests/host/test_phase3_helpers.c` | `:121-155` `test_commissioning_assignment_examples` (vecteurs descendants) | réécrire les vecteurs selon le §4 ci-dessous |
| `tests/host/test_phase3_helpers.c` | `:182-199` `test_commissioning_five_boards_total_49` | reste valide (positions `0..4`, total 49) : **renforcer** en vérifiant qu'aucun motif `ANCHOR_SHIFT` n'est levé et en ajoutant une borne de bloc (par ex. MIDI 45 → pos 0, MIDI 46 → pos 1) |
| `tests/host/test_phase3_helpers.c` | `:157-180` `test_commissioning_totals_and_duplicates` | ajouter un cas « trou de position » (pos 0 + pos 2) : construction OK, bornes de bloc correctes, `total_occupied` exact |
| `tests/host/test_phase3_helpers.c` | `:411` `test_diag_lines`, `:438-442` (`[TABLE] pos=4 ... keys=47-49`), `:505-516` (`[MAP] pos=4 ch=3 key=49 midi=84`) | mettre à jour les chaînes d'exemple (le formateur est neutre) |
| `tests/host/test_phase3_helpers.c` | `:256-325` `test_commissioning_flash_state_and_legacy` | ajouter `0x40` aux motifs testés (aller-retour v2, `ANCHOR_SHIFT` ⇒ `PROVISIONAL`) ; la lecture d'une v1 reste `FINAL`/`0` |
| `tests/host/run_host_tests.ps1` | `:9-21` | **aucun changement** : la commande de build/test hôte ne bouge pas |

## 4. Vecteurs de référence à utiliser (règle ascendante)

| Cas | Entrées | Attendu |
|---|---|---|
| carte seule en pos 4, bitmap `0x00B` (ch 0,1,3) | 1 entrée | ch0 → touche 1 / MIDI 36 ; ch1 → 2 / 37 ; ch3 → 3 / 38 ; `partial = true` |
| carte seule en pos 4, bitmap `0x841` (ch 0,6,11) | 1 entrée | ch0 → 1 ; ch6 → 2 ; ch11 → 3 |
| pos 3 (2 ch) + pos 4 (3 ch) | 2 entrées | pos 3 → touches 1,2 ; pos 4 → touches 3,4,5 ; MIDI 36 → pos 3 ch 0 |
| pos 0 (9 ch, `0x1FF`) + pos 1..4 (10 ch chacun, `0x3FF`) | 5 entrées | total 49, `partial = false` ; pos 0 → 1..9 ; pos 1 → 10..19 ; pos 4 → 40..49 ; MIDI 36 → pos 0 ch 0 touche 1 ; MIDI 84 → pos 4 ch 9 touche 49 |
| pos 0 (`0x001`) + pos 2 (`0x001`) | 2 entrées | pos 0 → touche 1 ; pos 2 → touche 2 ; motif `ANCHOR_SHIFT` attendu (trou en pos 1) |

## 5. Pré-requis de configuration (utilisateur)

`firmware/clavier/src/uid_position.h` doit déclarer en **position 0** la carte
physiquement à l'extrémité **grave** (nœud CAN 2). Aujourd'hui la position 0 est
vide (`:20`) : la table sera construite et jouable, mais marquée
`PROVISIONAL flags=0x40` et les lignes `[MAP]` montreront que DO2 est joué par
une autre position. C'est un signalement, pas une panne.

## 6. Ce qui **n'est pas** touché

- **Aucune trame CAN** : `shared/protocol/can_protocol.[ch]` reste identique
  (`SOLENOID_COMMAND` `0x100` ne transporte que `channel`, jamais la touche).
- **Format flash inchangé** : toujours v2, en-tête 16 octets ; les touches ne
  sont pas stockées, donc **aucune migration** ; une table déjà écrite reste
  lisible et se recalcule au chargement.
- **`firmware/moteur/`** : non touché. Le Moteur compile
  `commissioning_table.c` sans l'utiliser (`firmware/moteur/CMakeLists.txt`) :
  vérifier que le build Moteur reste vert, mais aucun changement de code.
- **`firmware/clavier/src/keymap.c`** : non touché. Ses positions 1..84 sont des
  positions de matrice, sans lien avec les positions de carte ; le seul lien est
  la note MIDI (`N_DO2 = KEYCODE_STORAGE_NOTE(36)`).
- **Bornes de la couche pilote** (invariant 4) : aucun changement.

## 7. Build et tests (procédure)

1. Tests hôte (obligatoires, couvrent la règle) :
   ```powershell
   powershell -File tests\host\run_host_tests.ps1
   ```
   Attendu : aucune ligne `ECHEC`.
2. Build Clavier : `cmake --build "build/clavier"` (procédure
   [`tests/build_clavier_windows.md`](../../tests/build_clavier_windows.md)).
3. Build Moteur (non touché, mais il lie le module) : `cmake --build "build/moteur"`.
4. Validation matérielle : [`tests/phase3b_bout_en_bout.md`](../../tests/phase3b_bout_en_bout.md)
   (Test 2, résolution de notes) et
   [`tests/bringup_commissioning_provisoire.md`](../../tests/bringup_commissioning_provisoire.md)
   (§6 et §7, blocs de touches et motif `ANCHOR_SHIFT`).
