# Keycodes non-note du Clavier

Référence des keycodes non musicaux (hors notes `N_*`) de la base layer
`firmware/clavier/src/keymap.c`, avec leur effet et leurs positions. Tenue à
jour par doc-aval à chaque évolution de la base de keycodes (ADR-0026).

| Keycode | Effet | Position(s) | ADR |
|---|---|---|---|
| `UNTRANSPOSE` | Remet à zéro toutes les transpositions et altérations | 73 | ADR-0011, ADR-0026 |
| `T_GlobUp` | Transposition globale +1 demi-ton, cumulative | 76 | — |
| `T_GlobDown` | Transposition globale −1 demi-ton, cumulative | aucune | — |
| `T_OctupG` | Octave +1 sur les touches gauches, cumulative | 78 | — |
| `T_OctdownG` | Octave −1 sur les touches gauches, cumulative | 77 | — |
| `T_OctupD` | Octave +1 sur les touches droites, cumulative | 79 | — |
| `T_OctdownD` | Octave −1 sur les touches droites, cumulative | 80 | — |
| `A_b` | Modificateur maintenu : bascule naturelle ↔ bémol de la racine de la note pressée | 74 | ADR-0026 |
| `A_d` | Modificateur maintenu : bascule naturelle ↔ dièse de la racine de la note pressée | 75 | ADR-0026 |
| `MAINT` | Bascule du mode maintenance (appui uniquement) | 84 | ADR-0006, ADR-0012 |
| `GET_UID` | Demande les UID des cartes Moteur à l'appui ; aucune position assignée | aucune | ADR-0003 |
| `NONE` | Position libre, aucun keycode | 61, 72, 81, 82, 83 | ADR-0026 |
| `N_*` | Note MIDI fixe de la touche (ex. `N_DO4` = 60, `N_SOLD3` = 56) | 1–60, 62–71 | — |
