# Impact ADR-0012 — mode MAINTENANCE en polarité inverse, borné à 20 s

Note de référence pour le **Codeur** et le **Validateur**. Elle liste, fichier par
fichier, ce qu'il faut revoir parce que
[ADR-0012](../adr/adr-0012-mode-maintenance-force-inverse-duree-20s.md) (statut
`Accepted`, 2026-10-21) **inverse le sens de la poussée maintenance**, **porte sa
durée de 10 s à 20 s** et **répare le keycode `MAINTENANCE_TOGGLE`** qui rendait
le mode inatteignable.

Les chemins de ligne cités sont ceux **avant** modification (état du 2026-10-21) :
ils servent de point d'entrée, pas de référence stable.

> **État : à implémenter.** Cette note est une liste de tâches, contrairement à
> [`impact-adr-0010.md`](./impact-adr-0010.md) (déjà exécutée).

## 0. Règle à implémenter (rappel normatif)

| | Avant (ADR-0006 §4) | Après (ADR-0012) |
|---|---|---|
| Sens de la poussée (`PH/EN`) | niveau **PULL** | niveau **RELEASE** = **inverse du jeu normal** (constante dédiée `SOLENOID_MAINT_DIR_LEVEL`) |
| Phases | « PULL seul » | **force constante**, aucune phase : ni HOLD, ni RELEASE |
| Fin de poussée | relâchement ou coupure locale | **inchangé** |
| Duty | 50 % (plafond 100 %) | **inchangé** |
| Coupure locale du driver (Moteur) | 10 000 ms | **20 000 ms** |
| Coupure explicite du Clavier | 10 000 ms | **20 000 ms** |
| Budget thermique | duty ≤ 100 % ; durée ≤ 10 s | **`durée × duty` ≤ 1 000 000** (ms × %), vérifié à la compilation |
| Repos minimal après maintenance | 2 000 ms | **inchangé** |
| Sortie de phase | `begin_rest` direct | **`DIR_TO_PULL`** (duty 0 + 200 µs) **puis** REST |
| Trame CAN | `command = 2` (`PUSH_ON`) | **inchangée** : même ID, même DLC, mêmes valeurs |

Invariants de comportement à conserver : une seule touche à la fois
(`MAINTENANCE_BUSY`), aucune note MIDI émise en maintenance, résolution
`note MIDI → (UID, canal)` par la table de commissioning, refus si la table est
invalide (`MAINTENANCE_NO_TABLE`) ou pendant un auto-test (`BUSY`), coupure de
tous les solénoïdes actifs à l'entrée **et** à la sortie du mode.

## 1. Bloc de constantes à écrire (`firmware/moteur/src/solenoid_profile.h`)

À substituer au bloc `// --- Phase MAINTENANCE (push only) ---` (l.50-54) :

```c
// --- Phase MAINTENANCE (force INVERSE, « REPEL ») : ADR-0006 §4, ADR-0012 ----
// L'aimant est REPOUSSÉ (polarité inverse du jeu normal) tant que la touche est
// tenue : aucune phase HOLD, aucune phase RELEASE. Dérogation bornée à
// l'invariant 4 : le driver écrête le duty et coupe lui-même à SOLENOID_MAINT_MAX_MS.
#define SOLENOID_MAINT_DIR_LEVEL SOLENOID_DIR_LEVEL_RELEASE
#define SOLENOID_MAINT_DUTY_PERCENT 50u
#define SOLENOID_MAINT_MAX_MS 20000u            // ADR-0012 (relevé de 10 s à 20 s)
#define SOLENOID_MAINT_DUTY_MAX_PERCENT 100u
#define SOLENOID_MAINT_MIN_REST_MS 2000u
// Enveloppe thermique validée par l'utilisateur (ADR-0006 §4) : équivalent de
// 10 s à 100 % de duty, soit 1 000 000 en (ms x %). 20 s à 50 % dissipe la même
// énergie (1 000 000). Vérifiée à la compilation dans solenoid_driver.c : un
// réglage de banc ne peut pas la dépasser.
#define SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT (10000u * 100u)
```

`SOLENOID_MAINT_DIR_LEVEL` peut être défini n'importe où dans le fichier (la
macro est développée au point d'usage, pas à sa définition) ; pour la lisibilité,
le placer dans la section « Polarité de DIR_CHANNEL » (l.56-59), auprès des deux
niveaux qu'il référence.

## 2. `firmware/moteur/src/solenoid_driver.c` — cœur du sujet

| Point d'entrée | Revue requise |
|---|---|
| `:11-23` bloc `_Static_assert` | ajouter `SOLENOID_MAINT_DUTY_PERCENT <= SOLENOID_MAINT_DUTY_MAX_PERCENT` et **le budget thermique** : `SOLENOID_MAINT_MAX_MS * SOLENOID_MAINT_DUTY_PERCENT <= SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT` (ADR-0012 Décision 3). Ajouter aussi la borne anti-débordement `SOLENOID_MAINT_MAX_MS <= 4000000u` (20 000 ms → 20 000 000 µs : conforme). |
| `:239-266` `solenoid_driver_push_start`, **`:261`** | écrire `SOLENOID_MAINT_DIR_LEVEL` au lieu de `SOLENOID_DIR_LEVEL_PULL`. **Commenter la preuve de sécurité** : l'écriture directe de `dir_gpio` est sûre parce que la fonction n'est admise qu'en `IDLE` (`:252`), état atteint uniquement via `REST` (`:330-333`), donc avec duty nul depuis au moins `SOLENOID_MIN_REST_MS` (10 ms) ≫ `SOLENOID_DIR_SAFETY_DELAY_US` (200 µs). Si un nouveau chemin vers `IDLE` apparaît, l'inversion devra passer par un état transitoire calqué sur `DIR_TO_RELEASE` (`:311-317`). |
| `:204-206` `solenoid_driver_note_off` (branche `MAINTENANCE`) | **ne plus appeler `begin_rest` directement** : passer par l'inversion de polarité de retour (voir §2.1). |
| `:290-294` `solenoid_driver_tick`, branche `SOLENOID_STATE_MAINTENANCE` (timeout) | idem : la coupure locale à 20 s doit elle aussi restaurer la polarité de jeu (voir §2.1). |
| `:377-391` `solenoid_state_name` | aucun changement (`"MAINTENANCE"` reste le nom d'état). |

### 2.1 Retour à la polarité de jeu : contrainte et mécanisme recommandé

**Pourquoi c'est obligatoire** (ADR-0012 Décision 4) : `:189` documente
l'invariant « `dir_channel` est au niveau PULL en IDLE » et `solenoid_driver_note_on`
(`:190-192`) **n'écrit pas** `dir_gpio` — il suppose la polarité déjà bonne. Si
une sortie de MAINTENANCE laissait `dir_channel` au niveau RELEASE, le **prochain
jeu normal partirait en sens inverse**, sans délai de sécurité. C'est un risque
introduit par cette ADR, pas un défaut préexistant.

**Contrainte** : la sortie de MAINTENANCE doit (i) ramener le duty à 0, (ii)
attendre ≥ `SOLENOID_DIR_SAFETY_DELAY_US`, (iii) écrire `SOLENOID_DIR_LEVEL_PULL`,
(iv) purger **le repos maintenance** (`SOLENOID_MAINT_MIN_REST_MS` = 2 s), pas le
repos court du jeu normal (`SOLENOID_MIN_REST_MS` = 10 ms).

Le point (iv) est le piège : le cas `DIR_TO_PULL` existant (`:324-328`) termine par
`begin_rest(drv, now_us, SOLENOID_MIN_REST_MS)` — **10 ms**, valeur codée en dur.
Le réutiliser tel quel perdrait les 2 s de repos exigées après une poussée
maintenance.

**Mécanisme recommandé** (minimal, symétrique de l'existant) :

```c
// Sortie d'une phase active vers le retour à la polarité de jeu : duty à 0,
// attente du délai de sécurité, puis DIR_TO_PULL -> REST (durée mémorisée).
static void begin_dir_to_pull(solenoid_driver_t *drv, uint32_t now_us, uint32_t rest_ms) {
    pwm_hal_set_duty_percent(&drv->pwm_channel, 0u);
    drv->rest_duration_us = rest_ms * US_PER_MS;   // consommé par le cas DIR_TO_PULL
    set_state(drv, SOLENOID_STATE_DIR_TO_PULL, now_us);
}
```

puis, dans `solenoid_driver_tick` :

```c
case SOLENOID_STATE_DIR_TO_PULL:
    if (elapsed(now_us, drv->state_start_us, SOLENOID_DIR_SAFETY_DELAY_US)) {
        gpio_put(drv->dir_gpio, SOLENOID_DIR_LEVEL_PULL);  // duty nul : sûr
        set_state(drv, SOLENOID_STATE_REST, now_us);       // rest_duration_us déjà posé
    }
    break;
```

avec les deux appelants du jeu normal adaptés : `RELEASE` (`:318-322`) et
`reset_fault` (`:361`) posent `rest_ms = SOLENOID_MIN_REST_MS`, tandis que la
sortie de MAINTENANCE (`note_off` `:204-206` et timeout `:290-294`) pose
`rest_ms = SOLENOID_MAINT_MIN_REST_MS`. **Recommandation, non normatif** : toute
autre structure est acceptable si les points (i) à (iv) ci-dessus sont tenus.

## 3. `firmware/clavier/src/main.c` — dispatch et borne

| Point d'entrée | Revue requise |
|---|---|
| `:31` `#define MAINTENANCE_TIMEOUT_MS 10000u` | passer à `20000u` (ADR-0012 Décision 3). Commenter le couplage : cette constante est la borne de la **coupure explicite envoyée par le Clavier** ; elle doit rester **≤** la borne locale du Moteur (`SOLENOID_MAINT_MAX_MS`), qui est la **dernière barrière** (`docs/specs/CAN.md`). |
| `:235-250` `handle_matrix_transition`, bloc `if (keycode.kind == KEYCODE_GET_UID)` | **extraire** le sous-bloc `if (keycode.kind == KEYCODE_MAINTENANCE_TOGGLE)` (`:243-248`) de l'imbrication : il devient un `if` **frère**, au même niveau que `KEYCODE_GET_UID`, avec `return` sur appui **et** sur relâchement pour ne pas imprimer la ligne générique `behavior MAINT ...` (`:261`). |
| `:279-284` boucle principale | **aucun changement de logique** : elle utilise `MAINTENANCE_TIMEOUT_MS` et émet `can_link_maintenance_push_stop(..., true)` ; seul l'effet de la constante change. |
| `:119-136` `handle_maintenance_toggle`, `:138-193` `handle_maintenance_transition` | **aucun changement** : la logique d'entrée/sortie (refus sans table, refus pendant auto-test, coupure des solénoïdes) est déjà conforme à ADR-0006 §2, et la bascule fonctionne déjà quand le mode est actif (`:143-148`). |

Contrôle négatif utile au banc : aujourd'hui, un appui en position 84 imprime
`position 84 behavior MAINT pressed` **sans** `[MODE] maintenance=ON`. Après
correction, cette ligne ne doit plus apparaître sur un appui de la position 84.

## 4. `firmware/moteur/src/solenoid_driver.h` — commentaires de contrat

| Point d'entrée | Revue requise |
|---|---|
| `:5-6` diagramme d'états | mentionner `MAINTENANCE` (par ex. `IDLE -push_start-> MAINTENANCE -off/timeout-> DIR_TO_PULL -> REST -> IDLE`). |
| `:9-10` commentaire des états transitoires | inchangé : `DIR_TO_PULL` sert désormais aussi au retour de MAINTENANCE. |
| `:90-93` commentaire de `solenoid_driver_push_start` | remplacer « Poussée maintenance unidirectionnelle » par « Poussée maintenance en **polarité inverse** du jeu normal (ADR-0012), bornée par le driver (duty + durée + budget thermique) ». Les conditions de refus listées sont exactes : ne pas les modifier. |

## 5. Séquence attendue (référence de revue)

```
Clavier (main.c)            CAN 0x100            Moteur (can_link.c)      driver (solenoid_driver.c)
------------------------    ----------------     ---------------------    --------------------------
appui pos 84 (MAINT)
  handle_maintenance_toggle
  table valide ? auto-test ?
  -> [MODE] maintenance=ON
                            (aucune trame)

appui touche de note
  [PUSH] ... start
                            PUSH_ON, ch=C  --->  channel occupé ?
                                                 pas d'auto-test ?
                                                 push_start()      --->  IDLE -> MAINTENANCE
                                                                         DIR=RELEASE, duty 50 %
                                                                         (DIR écrit depuis IDLE :
                                                                          duty nul >= 10 ms)

maintien (jusqu'à 20 s)
  timeout 20 s
  [PUSH] ... timeout
                            OFF, ch=C      --->  note_off()        --->  MAINTENANCE -> DIR_TO_PULL
  ou relâchement                                                          duty 0, 200 µs
  [PUSH] ... stop                                                        -> DIR=PULL
                                                                         -> REST 2000 ms
                                                                         -> IDLE (DIR=PULL)

second appui pos 84
  handle_maintenance_toggle
  -> [MODE] maintenance=OFF
```

Points à vérifier sur cette séquence : **aucune** note MIDI USB ; **aucune**
nouvelle trame CAN pour le mode lui-même ; une seule touche à la fois ; à l'état
final, `DIR` est revenu au niveau PULL (sinon le jeu normal suivant est inversé).

## 6. Tests

| Fichier | Revue requise |
|---|---|
| `tests/phase3b_maintenance.md` | mis à jour par cette passe (REPEL, 20 s, critère de **sens**, dépannage). C'est la campagne de référence. |
| `tests/host/run_host_tests.ps1` / `tests/host/test_phase3_helpers.c` | **aucun changement** : le harnais hôte couvre `shared/protocol` et le journal, pas le dispatch de `main.c` ni le driver Moteur. Ne pas chercher à couvrir la Décision 1 en hôte : la vérification est **manuelle**. |
| `tests/clavier_keycode_keymap_manual.md` | corrigé par cette passe : la position 84 y attendait l'altération `A_fa` ; elle doit désormais produire `[MODE] maintenance=ON` (et non une ligne `behavior`). |

## 7. Build et tests (procédure)

Aucun changement d'interface partagée : **`shared/protocol/` n'est pas modifié**
(invariant 5 non sollicité). Les deux firmwares doivent néanmoins être reflashés
ensemble (le sens de `command = 2` change).

```powershell
powershell -File tests\host\run_host_tests.ps1        # attendu : 221 vérifications, 0 échec
cmake --build "build/clavier"                         # -> build/clavier/clavier.uf2 (Pico 2, BOOTSEL)
cmake --build "build/moteur"                          # -> build/moteur/moteur.elf (flash SWD, OpenOCD)
```

Rediriger les sorties vers un fichier (`*> "build\...\build.log"`) et n'en
extraire que les erreurs et le résumé. Détail des deux chaînes de build :
[`../../tests/build_clavier_windows.md`](../../tests/build_clavier_windows.md) et
[`../../tests/build_moteur_windows.md`](../../tests/build_moteur_windows.md).

**Forcer la recompilation des unités touchées** (Ninja affiche `no work to do`
sinon) : supprimer les objets, par exemple
`build/clavier/CMakeFiles/clavier.dir/src/main.c.obj` et
`build/moteur/CMakeFiles/moteur.dir/src/solenoid_driver.c.obj`, puis relancer les
builds.

**Contrôle de l'écrêtage à la compilation** : après modification, vérifier que le
build **échoue** si l'on porte temporairement `SOLENOID_MAINT_MAX_MS` à `30000u`
en laissant le duty à `50u` (1 500 000 > 1 000 000), puis rétablir `20000u`.
Sans ce `_Static_assert`, un réglage de banc pourrait doubler l'enveloppe
thermique validée sans que rien ne le signale.

**Validation matérielle obligatoire** :
[`../../tests/phase3b_maintenance.md`](../../tests/phase3b_maintenance.md) — le
**sens** du geste ne peut pas être vérifié autrement qu'au banc.

## 8. Ce qui **n'est pas** touché

- **Aucune trame CAN** : `shared/protocol/can_protocol.[ch]` reste identique
  (`SOLENOID_COMMAND` `0x100`, DLC 5, `command = 2`). Seul le **sens physique**
  de `command = 2` change, côté Moteur.
- **Table de commissioning** : aucun changement de format ni d'écriture
  (invariant 3). La maintenance n'écrit jamais en flash.
- **`firmware/clavier/src/keymap.c`** : non touché. La position 84 porte déjà
  `KEYCODE_STORAGE_BEHAVIOR(KEYCODE_MAINTENANCE_TOGGLE)` (`:39`).
- **`firmware/clavier/src/keycode.[ch]`** : non touché (`KEYCODE_MAINTENANCE_TOGGLE`
  existe, libellé `MAINT`).
- **Bornes de la couche pilote** (invariant 4) : seul le profil MAINTENANCE est
  re-borné ; PULL/HOLD/RELEASE/PROBE sont inchangés.
- **`docs/specs/clavier_keymap.json`** : corrigé par cette passe (position 84 =
  `MAINT`), dette d'ADR-0006, sans rapport avec le code.

## 9. Écarts et points hors périmètre constatés

1. **Dette ADR-0006 soldée ici** : `docs/specs/clavier_keymap.json`
   (`keymap.base_layer["84"]` = `A_fa`, `behaviors.alteration.codes.A_fa.keymap_position`
   = 84) et `tests/clavier_keycode_keymap_manual.md` (attendu `A_fa` en position 84)
   déclaraient encore l'altération FA, qu'ADR-0006 §1 a remplacée par `MAINT`
   **sans la déplacer**. Corrigé dans les deux fichiers ; un groupe
   `behaviors.mode.codes.MAINT` a été ajouté à la spec.
2. **`AGENTS.md:13` / `Detail_architecture.md:5`** annoncent « 8 switchs de
   contrôle » alors que `keymap.c:28-39` en assigne 12 (positions 73 à 84) :
   **non corrigé** (hors périmètre, signalé par ADR-0012 IMP-007).
3. **Note dupliquée** : `keymap.c:19` et `:22` assignent tous deux `N_DOD1` :
   non traité ici, à signaler à l'Architecte si c'est involontaire.
4. **Travail en attente d'ADR-0011** : sa Décision 5 demande aussi une mise à
   jour de `clavier_keymap.json` (sémantique `UNTRANSPOSE`) et de
   `tests/clavier_keycode_keymap_manual.md`. Non traité dans cette passe.
