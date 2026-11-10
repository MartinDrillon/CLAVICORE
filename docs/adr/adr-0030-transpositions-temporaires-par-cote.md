---
title: "ADR-0030: Transpositions temporaires par côté (demi-ton et octave) maintenues"
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

Le clavier dispose aujourd'hui de deux familles de transposition **persistantes**
(ADR-0026, `keycode.c`) : `T_GLOB_UP/DOWN` (±1 demi-ton cumulatif) et
`T_OCT_L_UP/DOWN`, `T_OCT_R_UP/DOWN` (±1 octave cumulatif, par côté). Elles
modifient l'état jusqu'à `UNTRANSPOSE`.

ADR-0029 a introduit le premier modificateur **momentané** de transposition de
la hauteur (`TEMP_B/TEMP_D`), dont l'effet ne dure que pendant l'appui, avec la
règle : une note déjà tenue n'est pas modifiée et son Note Off reste celui
mémorisé à l'appui.

L'humain veut huit keycodes momentanés supplémentaires, de même forme : une
transposition appliquée uniquement aux touches d'un côté, active seulement
pendant l'appui, par demi-ton ou par octave.

### Decision

1. **Deux kinds ajoutés en fin d'enum** (`keycode.h`), pour ne décaler aucune
   valeur existante : `KEYCODE_TEMP_TRANSPOSE_ST = 14` et
   `KEYCODE_TEMP_TRANSPOSE_OCT = 15`. Valeur 0..3 = `L_UP`, `L_DOWN`, `R_UP`,
   `R_DOWN`. Aucun keycode n'est persisté ; `shared/protocol/` intact.

   | Nom dans le keymap | Valeur | Effet tant que maintenu |
   |---|---|---|
   | `T_TEMP_ST_L_UP` | `0x0E00` | +1 demi-ton, touches gauches |
   | `T_TEMP_ST_L_DOWN` | `0x0E01` | −1 demi-ton, touches gauches |
   | `T_TEMP_ST_R_UP` | `0x0E02` | +1 demi-ton, touches droites |
   | `T_TEMP_ST_R_DOWN` | `0x0E03` | −1 demi-ton, touches droites |
   | `T_TEMP_OCT_L_UP` | `0x0F00` | +1 octave, touches gauches |
   | `T_TEMP_OCT_L_DOWN` | `0x0F01` | −1 octave, touches gauches |
   | `T_TEMP_OCT_R_UP` | `0x0F02` | +1 octave, touches droites |
   | `T_TEMP_OCT_R_DOWN` | `0x0F03` | −1 octave, touches droites |

2. **Effet strictement momentané.** Le keycode n'agit que sur les notes
   pressées **pendant** son maintien, et seulement si la position appartient au
   côté visé (`keycode_position_is_left`, la même définition que `T_OCT_L/R`).
   Relâché, il n'a plus d'effet sur les notes suivantes. Une note pressée
   pendant le maintien conserve sa hauteur modifiée jusqu'à son Note Off, même
   si le keycode est relâché avant : la note MIDI est mémorisée à l'appui
   (règle existante d'ADR-0026 §2). Un keycode momentané n'agit pas sur une
   note déjà tenue.

3. **Deux axes indépendants, additifs.** Le demi-ton et l'octave sont deux
   axes distincts, comme les transpositions persistantes. `T_TEMP_ST_L_UP` et
   `T_TEMP_OCT_L_UP` maintenus ensemble donnent **+13** demi-tons sur la gauche.
   Les côtés sont disjoints : `T_TEMP_ST_L_UP` et `T_TEMP_ST_R_DOWN` maintenus
   ensemble donnent +1 à gauche et −1 à droite.

4. **Exclusivité montée/descente par axe et par côté.** Si la montée et la
   descente du même axe et du même côté sont maintenues ensemble, **la dernière
   pressée encore enfoncée gagne** (même règle que `ALT_B/ALT_D` et
   `TEMP_B/TEMP_D`). Relâcher la dernière pressée rend la main à l'autre si
   elle est toujours tenue, sinon à zéro.

5. **Somme avec les transpositions persistantes.** La transposition appliquée
   est `T_GLOB + octaves persistantes du côté + delta momentané d'octave du
   côté + delta momentané de demi-ton du côté`. Aucune limite n'est ajoutée.

6. **Ordre de résolution inchangé, complété.** Note physique →
   armure/surcouche `ALT` → `TEMP_B/TEMP_D` (table de hauteurs) → transposition
   persistante + momentanée → bornage 0..127. Le bornage reste le dernier
   calcul.

7. **Mode maintenance inchangé.** Les huit keycodes sont suivis mais sans effet
   en maintenance, comme `CLE_*` et `TEMP_*` : aucune modification de note.

8. **`UNTRANSPOSE`** remet à zéro armure, surcouches `ALT` et transpositions
   persistantes. Il ne peut pas annuler un keycode momentané encore maintenu :
   celui-ci reprend son effet à la note suivante tant qu'il est enfoncé.

9. **Positions libres.** Conformément à ADR-0029 §7, aucune position n'est
   fixée : l'humain place et déplace ces keycodes comme il veut.

### Consequences

#### Positive
* **POS-001**: Effets expressifs réversibles au relâchement, sans toucher aux transpositions persistantes ni à l'armure.
* **POS-002**: Réutilise la structure des modificateurs maintenus et la résolution de note existantes ; aucun changement de trame ni de format.
* **POS-003**: Les deux axes (demi-ton, octave) se combinent naturellement avec les transpositions persistantes par simple addition.

#### Negative
* **NEG-001**: Quatre directions par famille au lieu de deux : le suivi d'état maintenu dans `main.c` doit gérer quatre directions, pas deux.
* **NEG-002**: Une note tenue pendant un changement de keycode momentané garde sa hauteur d'appui : un accord joué à cheval sur deux états est volontairement figé note par note.

### Alternatives Considered

#### Somme des montées et descentes simultanées
* **ALT-001**: **Description**: additionner les quatre directions maintenues sur un même côté.
* **ALT-002**: **Rejection Reason**: deux appuis opposés s'annuleraient silencieusement (état 0 sans geste visible) ; la règle « dernière pressée gagne » est déjà celle des autres modificateurs.

#### Un seul kind à huit valeurs
* **ALT-001**: **Description**: un kind unique `KEYCODE_TEMP_TRANSPOSE` avec huit valeurs.
* **ALT-002**: **Rejection Reason**: mélange deux axes indépendants (demi-ton, octave) dans une même valeur, alors que la résolution les additionne séparément ; deux kinds rendent la table et les tests explicites.

### Implementation Notes

* **IMP-001**: `keycode.h` — kinds 14 et 15 en fin d'enum ; enums ou valeurs `TEMP_TRANSPOSE_ST/OCT` avec `L_UP=0, L_DOWN=1, R_UP=2, R_DOWN=3` ; constantes publiques de la table §1 ; `keycode_press` inerte pour ces kinds.
* **IMP-002**: `main.c` — généraliser le suivi maintenu à ces deux familles avec **quatre** directions ; l'actif est la dernière pressée encore enfoncée, par axe et par côté. Conserver les passes (modificateurs → notes → relâchements).
* **IMP-003**: `main.c` — au moment de l'appui d'une note, résoudre le côté via `keycode_position_is_left(position)` et passer à `note_state_press` le delta momentané total (`int16_t`) : octave du côté + demi-ton du côté.
* **IMP-004**: `note_state.c/.h` — appliquer ce delta dans la même étape que la transposition persistante, avant le bornage ; aucun changement de la mémorisation à l'appui.
* **IMP-005**: Trace `CLAVIER_DIAG_TRACE` : côté, keycode momentané actif, delta appliqué. Aucun nouveau niveau, aucune trame CAN.
* **IMP-006**: Tests hôte — les huit keycodes (valeur), côté gauche vs droit (positions limites de `keycode_position_is_left`), ±1 et ±12, somme demi-ton + octave (+13), montée/descente simultanées (dernière gagne, relâchement rend la main), somme avec `T_GLOB`/`T_OCT` persistants, note tenue non modifiée et Note Off mémorisé, inertie en maintenance, bornage 0/127, `UNTRANSPOSE` avec keycode momentané encore maintenu.
* **IMP-007**: `docs/specs/keycodes.md` — ajouter les huit lignes, sans position normative. Aucune modification de `shared/protocol/`.

### References
* **REF-001**: [./adr-0029-keycodes-armure-et-alteration-temporaire.md](./adr-0029-keycodes-armure-et-alteration-temporaire.md) — modificateurs momentanés, ordre de résolution, positions non normatives.
* **REF-002**: [./adr-0026-keycode-modificateur-alteration-deux-touches.md](./adr-0026-keycode-modificateur-alteration-deux-touches.md) — note tenue non modifiée, notation « dernière pression gagne ».
* **REF-003**: `firmware/clavier/src/keycode.h`, `keycode.c`, `note_state.c`, `main.c` — état, résolution, keymap.
