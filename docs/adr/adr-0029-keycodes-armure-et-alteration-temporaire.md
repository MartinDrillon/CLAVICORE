---
title: "ADR-0029: Keycodes d'armure (CLE_B/CLE_D), d'altération temporaire (TEMP_B/TEMP_D) et nommage court des keycodes"
status: "Accepted"
date: "2026-10-31"
authors: "Agent Architecte"
tags: ["architecture", "firmware"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted** (validée par l'humain le 2026-10-31)

### Context

ADR-0026 a réduit l'altération à deux modificateurs maintenus, `ALT_B` et
`ALT_D`, qui échangent **une** classe de hauteur avec sa voisine (naturelle ↔
bémol, naturelle ↔ dièse) et dont l'état persiste après relâchement.

L'humain veut deux mécanismes de plus, de portée différente :

1. **Armure** : `CLE_B` maintenu + une tonique naturelle place tout le clavier
   dans la tonalité mineure de cette tonique (sol → sol mineur = si bémol et mi
   bémol) ; `CLE_D` fait de même en majeur (sol → sol majeur = fa dièse).
   L'armure reste après relâchement jusqu'à la prochaine armure ou
   `UNTRANSPOSE`.
2. **Altération temporaire** : `TEMP_B`/`TEMP_D` maintenus. Pendant l'appui,
   les naturelles sont bémolisées (resp. diésées) et les notes déjà altérées
   reviennent à la naturelle voisine — autrement dit `la` devient `lab` et
   `sol#` (= `lab`) devient `la`. Au relâchement, l'état antérieur revient.

Par ailleurs les noms actuels sont impraticables dans un keymap écrit à la
main : `KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_B)`,
`KEYCODE_STORAGE_BEHAVIOR(KEYCODE_TRANSPOSE_GLOBAL_UP)`. L'humain veut écrire
`[n] = ALT_B`, comme il écrit `[n] = N_DO4`.

Enfin, ADR-0026 §6 et IMP-002 fixent les positions 74/75/81/82/83. L'humain
modifie les positions à la volée pour ses essais : aucune ADR ne doit figer une
position de keycode.

### Decision

1. **Encodage inchangé, kinds ajoutés en fin.** Un keycode reste
   `(kind << 8) | value` sur 16 bits (`keycode.h`). Deux kinds sont ajoutés
   **après** `UNTRANSPOSE` pour ne décaler aucune valeur existante :
   `CLE` = 12 et `TEMP` = 13. Aucun keycode n'est persisté : format de
   commissioning, trames CAN et `shared/protocol/` intacts.

2. **Noms publics = keycodes complets.** Le keymap écrit directement le nom du
   keycode, sans constructeur. Les macros de construction deviennent internes
   à `keycode.h`.

   | Nom dans le keymap | Valeur | Effet |
   |---|---|---|
   | `NONE` | `0x0000` | position libre |
   | `ALT_B` / `ALT_D` | `0x0800` / `0x0801` | modificateur maintenu, une classe |
   | `CLE_B` / `CLE_D` | `0x0C00` / `0x0C01` | armure mineure / majeure |
   | `TEMP_B` / `TEMP_D` | `0x0D00` / `0x0D01` | altération temporaire maintenue |
   | `UNTRANSPOSE` | `0x0B00` | remise à zéro |
   | `MAINT` | `0x0A00` | bascule maintenance |
   | `GET_UID` | `0x0900` | demande d'UID |
   | `T_GLOB_UP` / `T_GLOB_DOWN` | `0x0200` / `0x0300` | transposition globale |
   | `T_OCT_L_UP` / `_DOWN` | `0x0400` / `0x0500` | octave gauche |
   | `T_OCT_R_UP` / `_DOWN` | `0x0600` / `0x0700` | octave droite |
   | `N_*` | `0x0100 \| midi` | note |

   `KS_A(ALT_B)`, `KEYCODE_STORAGE_*` et `KEYCODE_ALTERATION_*` disparaissent de
   l'interface du keymap ; les noms d'affichage diag (`A_b`, `A_d`) restent.

3. **Armure : modificateur maintenu, sélection par la tonique.** `CLE_B` ou
   `CLE_D` doit être enfoncé au moment de l'appui d'une touche **naturelle** ;
   cette touche est la tonique. L'armure est appliquée **avant** la résolution
   de la note tonique (une tonique n'est jamais altérée par sa propre armure).
   Une touche noire sous `CLE_B`/`CLE_D` ne change pas l'armure et sonne
   normalement. L'armure persiste après relâchement du modificateur, jusqu'à la
   prochaine armure ou `UNTRANSPOSE`.

   `CLE_B` = armure de la tonalité **mineure**, `CLE_D` = armure de la tonalité
   **majeure**. L'armure est l'ensemble des classes naturelles altérées de la
   gamme (bémol ou dièse selon la tonalité), pas un simple choix bémol/dièse :

   | Tonique | `CLE_D` (majeur) | `CLE_B` (mineur) |
   |---|---|---|
   | DO | — (DO majeur) | sib, mib, lab (DO mineur) |
   | RÉ | fa#, do# | sib (RÉ mineur) |
   | MI | fa#, do#, sol#, ré# | fa# (MI mineur) |
   | FA | sib | sib, mib, lab, réb (FA mineur) |
   | SOL | fa# | sib, mib (SOL mineur) |
   | LA | fa#, do#, sol# | — (LA mineur) |
   | SI | fa#, do#, sol#, ré#, la# | fa#, do# (SI mineur) |

4. **`ALT_B`/`ALT_D` deviennent une surcouche de l'armure.** L'état effectif
   d'une classe est : surcouche si elle existe, sinon armure, sinon naturelle.
   `ALT_B` pose un bémol sur la classe, `ALT_D` un dièse ; un second appui de la
   même direction **retire la surcouche** et rend la main à l'armure (et non à
   la naturelle). La règle « dernier changement gagne » pour les conflits
   enharmoniques (ADR-0026 §4) est conservée.

   Deux précisions de comportement : une **nouvelle armure efface les
   surcouches `ALT`** (changer de tonalité remet toutes les classes à l'armure
   choisie) ; si `CLE_*` et `ALT_*` sont maintenus ensemble au moment d'un appui
   de note naturelle, **l'armure l'emporte** pour cet appui.

5. **`TEMP_B`/`TEMP_D` : surcouche totale et momentanée.** Pendant l'appui,
   la note **résolue** (armure + `ALT` compris) est transformée par une table de
   12 hauteurs, appliquée en dernier :

   - `TEMP_B` : naturelle → −1 demi-ton ; altérée → +1 demi-ton (la naturelle
     au-dessus). Exemples : `la` → `lab`, `sol#` → `la`, `fa#` → `sol`.
   - `TEMP_D` : naturelle → +1 demi-ton ; altérée → −1 demi-ton.
     Exemples : `mi` → `fa`, `fa` → `fa#`, `sib` → `la`.

   Conséquence enharmonique assumée, validée par l'humain : sous `TEMP_B`, `fa`
   devient `mi` et aucune touche ne joue `fa` (fa bémol = mi) ; sous `TEMP_D`,
   `mi` devient `fa` et aucune touche ne joue `mi`. Au relâchement, l'état
   antérieur revient immédiatement. Une note déjà tenue n'est ni modifiée ni
   rejouée ; son Note Off reste la note mémorisée à l'appui, comme ADR-0026 §2.
   Si `TEMP_B` et `TEMP_D` sont enfoncés ensemble, le dernier pressé encore
   enfoncé gagne — même règle que les modificateurs d'altération.

6. **Ordre de résolution.** Note physique → armure/surcouche `ALT` (par classe)
   → `TEMP` (table 12 hauteurs) → transposition globale et par côté → bornage
   0..127. Aucun autre changement de la chaîne MIDI.

7. **Positions libres.** **La position des keycodes dans le keymap ne doit être
   figée dans aucun ADR.** Le keymap est un banc d'essai permanent de l'humain :
   il place et déplace les keycodes où il veut, sans qu'aucune décision écrite
   ne le contraigne. ADR-0026 §6 et IMP-002 sont dépréciés en conséquence. Les
   documents de spec peuvent citer les positions d'un build donné, à titre
   indicatif et non normatif.

8. **Mode maintenance inchangé.** Comme ADR-0026 §8 : `CLE_*` et `TEMP_*` sont
   ignorés en maintenance (aucune modification d'armure ni d'état), et les notes
   de maintenance gardent l'altération déjà mémorisée.

### Consequences

#### Positive
* **POS-001**: Un geste unique donne une tonalité complète (armure), au lieu de sept bascules classe par classe ; `CLE_B + SOL` = sol mineur.
* **POS-002**: `TEMP_B`/`TEMP_D` permettent un glissement passager de tout le clavier, réversible au relâchement, sans toucher l'armure ni les surcouches.
* **POS-003**: Le keymap devient lisible : `[n] = ALT_B`, `[n] = CLE_D`, `[n] = N_DO4`. Plus de constructeur imbriqué.
* **POS-004**: La règle « aucune position figée dans une ADR » supprime la source des divergences constatées entre keymap, tests et docs.

#### Negative
* **NEG-001**: L'armure est une notion nouvelle à tester (14 armures) ; une erreur de table est audible immédiatement.
* **NEG-002**: Deux couches d'état (`armure`, surcouche `ALT`) au lieu d'une : le raisonnement « dernier changement gagne » doit être maintenu sur les deux.
* **NEG-003**: Le renommage des keycodes touche `keymap.c`, `keycode.c`, `main.c`, les tests et les docs dans un même lot.
* **NEG-004**: `TEMP_B` fait disparaître `fa` et `TEMP_D` fait disparaître `mi` (enharmonie) : comportement voulu, mais surprenant à l'oreille.

### Alternatives Considered

#### Armure calculée par cycle des quintes plutôt que table
* **ALT-001**: **Description**: déduire l'armure de la tonique par algorithme.
* **ALT-002**: **Rejection Reason**: la table de 14 entrées est plus simple à relire, à tester et à corriger ; elle rend chaque armure vérifiable à l'œil.

#### `TEMP_*` implémenté comme « toutes les classes à bémol/dièse »
* **ALT-001**: **Description**: forcer l'état d'altération de toutes les classes pendant l'appui.
* **ALT-002**: **Rejection Reason**: ne reproduit pas le comportement demandé sur les notes déjà altérées (`sol#` doit devenir `la`, pas `sol`) ; la table de 12 hauteurs appliquée en dernier est exacte et testable.

#### Armure en surcouche unique partagée avec `ALT`
* **ALT-001**: **Description**: écrire l'armure directement dans l'état d'altération existant.
* **ALT-002**: **Rejection Reason**: un `ALT` posé puis retiré effacerait l'armure ; l'humain demande que l'armure survive aux retouches individuelles.

### Implementation Notes

* **IMP-001**: `keycode.h` — kinds `KEYCODE_CLE = 12` et `KEYCODE_TEMP = 13` ajoutés en fin ; `keycode_cle_t {CLE_MINOR, CLE_MAJOR}`, `keycode_temp_t {TEMP_FLAT, TEMP_SHARP}` ; constantes publiques de la table §2 ; constructeurs `KEYCODE_STORAGE_*` conservés en interne ou supprimés après migration.
* **IMP-002**: `keycode.h` — état : `armure_state[7]` (0/1/2), `alt_override[7]` + horodatage existant. `UNTRANSPOSE` remet les deux à zéro.
* **IMP-003**: `keycode.c` — table des 14 armures (tonique × mode → 7 états), application au moment de l'appui de la tonique ; surcouche `ALT` prioritaire ; table `TEMP` de 12 hauteurs appliquée en dernier, selon l'ordre du §6.
* **IMP-004**: `main.c` — suivre l'état maintenu de `CLE_*` et `TEMP_*` en plus de `ALT_*`, avec la même règle de simultanéité (modificateurs avant notes dans un lot, relâchements après).
* **IMP-005**: `main.c` — trace diag : tonique + armure posée, et `TEMP` actif + note avant/après. Aucun nouveau niveau ni trame CAN.
* **IMP-006**: Tests hôte — armures (les 14 lignes de la table, tonique noire ignorée, persistance après relâchement, `UNTRANSPOSE`), surcouche `ALT` sur armure (pose, retrait, retour à l'armure), `TEMP` (naturelles, altérées, enharmonies `fa`/`mi`, relâchement, notes tenues), ordre de résolution, bornes 0/127.
* **IMP-007**: Migrer `keymap.c`, `keycode.c`, `main.c`, `tests/` et `docs/specs/keycodes.md` vers les noms courts ; `keycodes.md` perd sa colonne « positions » (ou la marque non normative).
* **IMP-008**: Ne fixer aucune position dans ce lot ni dans un doc normatif ; les positions restent celles que l'humain teste. Corriger en conséquence les assertions de position dans `tests/host/test_keycode_alteration.c` : garder les tests de valeur de keycode et de comportement, retirer ceux qui imposent une position précise dans le keymap.

### References
* **REF-001**: [./adr-0026-keycode-modificateur-alteration-deux-touches.md](./adr-0026-keycode-modificateur-alteration-deux-touches.md) — modificateurs `ALT_B`/`ALT_D`, §6 déprécié par la présente.
* **REF-002**: [./adr-0011-untranspose-remise-a-zero-etat-transposition.md](./adr-0011-untranspose-remise-a-zero-etat-transposition.md) — remise à zéro de l'état.
* **REF-003**: `firmware/clavier/src/keycode.h`, `keycode.c`, `keymap.c`, `main.c` — encodage, résolution, keymap.
* **REF-004**: `docs/specs/keycodes.md` — référence des keycodes non-note.
