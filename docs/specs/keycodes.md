# Keycodes non-note du Clavier

Référence des keycodes non musicaux (hors notes `N_*`) utilisables dans la
base layer `firmware/clavier/src/keymap.c`, avec leur effet. Tenue à jour par
doc-aval à chaque évolution de la base de keycodes (ADR-0026, ADR-0029,
ADR-0030).

Depuis ADR-0029, le keymap écrit directement le nom public du keycode
(`[n] = ALT_B`, `[n] = CLE_D`) ; les macros `KEYCODE_STORAGE_*` et
`KEYCODE_ALTERATION_*` n'existent plus. Les positions des keycodes dans le
keymap sont **libres et non normatives** : aucune décision ne les fige, elles
changent à la volée (ADR-0029 §7, invariant 6), et cette référence ne les
documente donc pas.

| Keycode | Effet | ADR |
|---|---|---|
| `NONE` | Case libre du keymap : aucun keycode | ADR-0026 |
| `N_*` | Note MIDI fixe de la touche (ex. `N_DO4` = 60, `N_SOLD3` = 56) | — |
| `ALT_B` | Modificateur maintenu : surcouche bémol sur la classe de la racine ; 2e appui = retour à l'armure ; une nouvelle armure efface les surcouches | ADR-0026, ADR-0029 |
| `ALT_D` | Modificateur maintenu : surcouche dièse sur la classe de la racine ; 2e appui = retour à l'armure ; une nouvelle armure efface les surcouches | ADR-0026, ADR-0029 |
| `CLE_B` | Modificateur maintenu : pose l'armure de la tonalité **mineure** de la tonique naturelle pressée ; persiste jusqu'à la prochaine armure ou `UNTRANSPOSE` | ADR-0029 |
| `CLE_D` | Modificateur maintenu : pose l'armure de la tonalité **majeure** de la tonique naturelle pressée ; persiste jusqu'à la prochaine armure ou `UNTRANSPOSE` | ADR-0029 |
| `TEMP_B` | Altération temporaire maintenue : table de 12 hauteurs appliquée en dernier (ex. `la`→`lab`, `sol#`→`la`, `fa#`→`sol`, `fa`→`mi`), réversible au relâchement | ADR-0029 |
| `TEMP_D` | Altération temporaire maintenue : opposé exact de `TEMP_B` (ex. `mi`→`fa`, `fa`→`fa#`, `sib`→`la`), réversible au relâchement | ADR-0029 |
| `T_TEMP_ST_L_UP` | Momentané, axe demi-ton, côté gauche : +1 demi-ton sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_TEMP_ST_L_DOWN` | Momentané, axe demi-ton, côté gauche : −1 demi-ton sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_TEMP_ST_R_UP` | Momentané, axe demi-ton, côté droit : +1 demi-ton sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_TEMP_ST_R_DOWN` | Momentané, axe demi-ton, côté droit : −1 demi-ton sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_TEMP_OCT_L_UP` | Momentané, axe octave, côté gauche : +1 octave sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_TEMP_OCT_L_DOWN` | Momentané, axe octave, côté gauche : −1 octave sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_TEMP_OCT_R_UP` | Momentané, axe octave, côté droit : +1 octave sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_TEMP_OCT_R_DOWN` | Momentané, axe octave, côté droit : −1 octave sur les notes pressées pendant le maintien, réversible au relâchement | ADR-0030 |
| `T_GLOB_UP` | Transposition globale +1 demi-ton, cumulative | ADR-0011 |
| `T_GLOB_DOWN` | Transposition globale −1 demi-ton, cumulative | ADR-0011 |
| `T_OCT_L_UP` | Octave +1 sur les touches gauches, cumulative | — |
| `T_OCT_L_DOWN` | Octave −1 sur les touches gauches, cumulative | — |
| `T_OCT_R_UP` | Octave +1 sur les touches droites, cumulative | — |
| `T_OCT_R_DOWN` | Octave −1 sur les touches droites, cumulative | — |
| `UNTRANSPOSE` | Remet à zéro les transpositions **persistantes**, l'armure et les surcouches d'altération ; n'annule pas une transposition temporaire encore maintenue | ADR-0011, ADR-0026, ADR-0029, ADR-0030 |
| `MAINT` | Bascule du mode maintenance (appui uniquement) | ADR-0006, ADR-0012 |
| `GET_UID` | Demande les UID des cartes Moteur à l'appui | ADR-0003 |

Les huit `T_TEMP_*` sont des keycodes **momentanés**, non persistés : kinds 14
(`KEYCODE_TEMP_TRANSPOSE_ST`, codes `0x0E00`..`0x0E03`) et 15
(`KEYCODE_TEMP_TRANSPOSE_OCT`, codes `0x0F00`..`0x0F03`), valeur 0..3 =
`L_UP`, `L_DOWN`, `R_UP`, `R_DOWN`. Leur effet n'existe que pendant l'appui et
seulement sur les notes pressées pendant le maintien, du côté visé
(`keycode_position_is_left`) ; une note déjà tenue n'est pas modifiée. Le
demi-ton et l'octave sont deux axes **additifs** (`T_TEMP_ST_L_UP` +
`T_TEMP_OCT_L_UP` = +13) qui **s'ajoutent** aux transpositions persistantes
sans les modifier. Montée et descente d'un même axe et d'un même côté sont
exclusives : la dernière pressée encore enfoncée gagne ; relâcher celle-ci rend
la main à l'autre si elle est tenue, sinon à 0. En maintenance, les huit sont
suivis mais sans effet. Aucune position ne leur est assignée.

Ordre de résolution d'une note physique : note physique → armure / surcouche
`ALT` → `TEMP` → transposition persistante (`T_GLOB`, `T_OCT_L/R`) +
transposition temporaire du côté de la position (`T_TEMP_ST_*`,
`T_TEMP_OCT_*`, additives) → bornage MIDI 0..127. Une note déjà tenue n'est pas
modifiée par un keycode momentané ; elle garde la hauteur mémorisée à son appui
jusqu'à son Note Off, même si le keycode est relâché avant.

## Voir aussi

- [Combos de positions (ADR-0031)](combos.md) : un keycode de sortie (note ou keycode non-note) pour N positions pressées ensemble.

