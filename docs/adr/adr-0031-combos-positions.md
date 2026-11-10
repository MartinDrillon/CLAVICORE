---
title: "ADR-0031: Combos de positions — un keycode pour N touches pressées ensemble"
status: "Accepted"
date: "2026-11-04"
authors: "Agent Architecte"
tags: ["architecture", "firmware"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted** (validée par l'humain le 2026-11-04)

### Context

Le clavier n'a qu'une relation position → keycode : chaque touche produit son
keycode, sans notion de combinaison. Or l'humain veut, comme ZMK, déclarer
qu'un ensemble de positions pressées ensemble dans une fenêtre de temps
produit **un seul** keycode à la place des leurs.

Contrainte de jeu : **la latence ajoutée ne doit toucher que les positions
membres d'un combo**. Une note qui n'appartient à aucun combo doit continuer à
sortir immédiatement ; sinon le jeu musical est dégradé.

L'architecture actuelle s'y prête : `matrix_scan_poll` ne fait qu'empiler les
transitions stables dans `matrix_events`, et `process_event_queue` les traite
après le balayage complet, en trois passes (modificateurs, appuis, relâchements).
Un combo peut donc s'intercaler entre la file d'événements et l'exécution des
keycodes, sans toucher au scan ni au MIDI.

### Decision

1. **Table de combos dans le keymap.** Les combos se déclarent à côté de
   `base_layer`, dans `firmware/clavier/src/keymap.c`, et sont exposés par
   `keymap.h`. Le moteur est un module séparé, pur, sans matériel :
   `combo.h/.c`. Format proche de ZMK :

   ```c
   static const uint8_t combo_esc_positions[] = { 73u, 84u };
   static const keymap_combo_t combo_table[] = {
       { .positions = combo_esc_positions, .count = 2u,
         .timeout_ms = 50u, .binding = CLE_D },
   };
   ```

   `keymap_combo_t` porte les positions, leur nombre, la fenêtre en
   millisecondes et le keycode de sortie. Aucune position de keycode n'est fixée
   par la présente ADR (ADR-0029 §7) : les positions des combos sont des données
   utilisateur, comme le keymap.

2. **Seules les positions membres sont retardées.** L'ensemble des positions
   qui apparaissent dans au moins un combo est connu statiquement. Un appui sur
   une position hors de cet ensemble suit le chemin actuel, sans aucun délai
   supplémentaire. Un appui sur une position membre est retenu jusqu'à
   décision : combo complet, expiration de la fenêtre, ou relâchement avant
   complétion.

3. **Fenêtre de temps par combo, mesurée depuis le premier appui du groupe.**
   Un combo se déclenche quand toutes ses positions sont pressées et que
   `now − premier_appui < timeout_ms`. Deux positions pressées dans le même lot
   de scan déclenchent donc immédiatement (aucune latence ajoutée). Un membre
   pressé seul est retenu au plus `timeout_ms`, puis émis normalement.

4. **Relâchement avant complétion = pas de combo.** Si un membre retenu est
   relâché avant que le combo soit complet, les appuis en attente sont émis
   immédiatement, dans leur ordre d'appui, puis le relâchement est traité. On
   n'attend pas l'expiration : un tap rapide ne subit que le temps réellement
   écoulé.

5. **Déclenchement : le keycode de sortie remplace les appuis membres.** Les
   appuis en attente ne sont jamais émis ; le keycode de sortie est exécuté une
   fois, comme un appui physique. Les relâchements des membres sont supprimés.
   Le keycode de sortie est **relâché au premier relâchement d'un membre** : le
   combo se comporte comme une touche tenue tant que l'accord est tenu.

6. **Aucune restriction sur la sortie.** Le binding peut être **n'importe quel
   keycode** : note, `ALT_*`, `CLE_*`, `TEMP_*`, `T_TEMP_*`, `T_GLOB_*`,
   `T_OCT_*`, `UNTRANSPOSE`, `MAINT`, `GET_UID`. Le firmware n'impose ni liste
   blanche, ni mode, ni condition d'usage.

   Une sortie « note » est émise **littéralement** : la note MIDI du binding,
   sans armure, sans altération, sans transposition. C'est une nécessité
   technique, pas une restriction : la transposition existante dépend d'une
   position physique (côté gauche/droite) que la sortie de combo n'a pas. La
   note littérale est mémorisée à l'appui et relâchée avec le combo, par les
   mêmes sorties que les notes physiques (CAN et USB MIDI).

   Les keycodes momentanés (`ALT_*`, `CLE_*`, `TEMP_*`, `T_TEMP_*`) sont
   maintenus pendant le combo et relâchés avec lui.

7. **Précédence déterministe.** Si plusieurs combos sont complets au même
   instant, celui qui a le plus de positions l'emporte ; à égalité, le premier
   de la table. Une position peut appartenir à plusieurs combos. Un combo déjà
   déclenché reste actif jusqu'au relâchement de tous ses membres ; aucun
   nouveau déclenchement n'a lieu pendant ce temps.

8. **Les autres touches ne sont jamais retardées et n'annulent pas un combo.**
   Presser une touche hors combo pendant une fenêtre en cours n'empêche pas le
   combo de se déclencher si ses membres arrivent à temps.

9. **Combos toujours actifs.** Aucun mode, aucun état du clavier ne désactive
   un combo déclaré : ils sont actifs au démarrage, en jeu, en maintenance,
   quelles que soient les transpositions, l'armure ou les altérations en cours.
   Le firmware n'ajoute aucune condition d'activation.

10. **Ordre de traitement dans un lot.** Les appuis de modificateurs physiques
    (passe 0) restent prioritaires. Les appuis de positions membres sont
    résolus par le moteur de combos avant d'être exécutés ; les positions non
    membres suivent le chemin actuel. Les relâchements sont traités après les
    appuis du lot. L'expiration des fenêtres est vérifiée à la fin de chaque
    lot, avant la fin de la boucle.

11. **Aucune latence hors combo.** Le budget ajouté est borné par `timeout_ms`
    du combo concerné, et nul pour toute position non membre. La latence de
    détection existante (anti-rebond du scan) n'est pas modifiée.

### Consequences

#### Positive
* **POS-001**: Les combos permettent des gestes de calibration et de mode sans consommer de touches de jeu, avec une syntaxe lisible proche de ZMK.
* **POS-002**: Le jeu musical n'est pas pénalisé : seules les positions déclarées subissent la fenêtre d'attente.
* **POS-003**: Le moteur est du C pur, testable sur hôte avec une horloge injectée, sans dépendance Pico.
* **POS-004**: Aucune modification de `shared/protocol/`, du scan, du MIDI ni des trames CAN.
* **POS-005**: Le binding peut être une note comme un comportement : aucune décision préalable n'est nécessaire pour tester un geste.

#### Negative
* **NEG-001**: Une touche membre d'un combo répond avec jusqu'à `timeout_ms` de retard quand elle est pressée seule.
* **NEG-002**: Les combos ajoutent un état à maintenir dans `clavier_state_t` et un chemin de résolution supplémentaire à raisonner.
* **NEG-003**: Une sortie « note » ne suit pas les transpositions ni l'armure : c'est une note littérale, différente du comportement d'une touche physique portant le même keycode.
* **NEG-004**: Un combo mal déclaré (position inexistante, doublon interne) n'est pas détecté par le compilateur ; il faut un contrôle au démarrage ou un test hôte sur la table livrée.

### Alternatives Considered

#### Résoudre les combos dans `matrix_scan.c`
* **ALT-001**: **Description**: retarder les événements au niveau du scan.
* **ALT-002**: **Rejection Reason**: mêle un problème de keymap et de temps au pilote matériel, et rend le moteur intestable sur hôte.

#### Délai uniforme sur toutes les touches
* **ALT-001**: **Description**: appliquer la fenêtre de combo à toutes les positions pour simplifier.
* **ALT-002**: **Rejection Reason**: ajoute une latence au jeu musical, contraire à la contrainte explicite.

#### Transposer les sorties « note » comme une touche physique
* **ALT-001**: **Description**: faire passer la note du binding par l'armure, les altérations et la transposition.
* **ALT-002**: **Rejection Reason**: il faudrait inventer un côté (gauche/droite) et une position pour un combo, ce qui rendrait le résultat dépendant d'une convention arbitraire ; la note littérale est prévisible et vérifiable.

### Implementation Notes

* **IMP-001**: `keymap.h/.c` — `keymap_combo_t`, table `combo_table` éditable à côté de `base_layer`, accesseurs `keymap_combo_count()` et `keymap_combo_at(i)`. `_Static_assert` sur la taille de la table et les bornes (`count >= 2`, `count <= COMBO_MAX_POSITIONS`, `timeout_ms > 0`).
* **IMP-002**: `combo.h/.c` — moteur pur : `combo_reset`, `combo_on_press(state, position, now_ms)`, `combo_on_release(...)`, `combo_expire(state, now_ms, out, max_out)`. Aucun appel Pico ; le temps est un paramètre.
* **IMP-003**: `main.c` — état de combos dans `clavier_state_t` ; `to_ms_since_boot` fourni au moteur ; un point de dispatch unique exécute un keycode (physique ou de combo) : notes via `can_link_note_on/off` + `midi_note_on/off`, keycodes instantanés via `keycode_press`, familles maintenues via le suivi des modificateurs, `MAINT` et `GET_UID` par leurs actions existantes.
* **IMP-004**: Une note de combo est littérale et n'entre pas dans `note_state` (indexé par position) ; mémoriser la note active dans l'état de combo pour son Note Off, et l'inclure dans la condition « notes actives » utilisée pour refuser un auto-test pendant le jeu.
* **IMP-005**: Vérification au démarrage : détecter une position de combo hors 1..84 ou un doublon interne et l'écrire dans le journal (aucune correction silencieuse).
* **IMP-006**: `firmware/clavier/CMakeLists.txt` et `tests/host/run_host_tests.ps1` — ajouter `combo.c` (et `keymap.c` y est déjà).
* **IMP-007**: Tests hôte (`tests/host/test_combo.c`, horloge factice) — déclenchement dans la fenêtre, expiration, relâchement avant complétion, même lot, précédence (plus de positions puis ordre de table), suppression des relâchements, relâchement de la sortie au premier membre relâché, non-membre non retardé, membre partagé, sortie momentanée maintenue puis relâchée, sortie note littérale (Note On/Off identiques), bornes de la table livrée.
* **IMP-008**: `docs/specs/combos.md` — format de table, sémantique, latence, note littérale ; lien depuis `keycodes.md`. Aucune position normative.

### References
* **REF-001**: [./adr-0029-keycodes-armure-et-alteration-temporaire.md](./adr-0029-keycodes-armure-et-alteration-temporaire.md) — positions non normatives, ordre de résolution.
* **REF-002**: [./adr-0030-transpositions-temporaires-par-cote.md](./adr-0030-transpositions-temporaires-par-cote.md) — keycodes momentanés, suivi maintenu.
* **REF-003**: `firmware/clavier/src/main.c` — `matrix_events`, `process_event_queue`, passes 0/1/2, sorties note CAN/USB.
* **REF-004**: `firmware/clavier/src/matrix_scan.c` — transitions stables, anti-rebond.
