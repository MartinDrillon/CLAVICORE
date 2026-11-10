# Combos de positions (ADR-0031)

Un **combo** associe un ensemble de positions physiques à un keycode de sortie :
presser toutes ses positions dans une fenêtre de temps produit le keycode de
sortie au lieu des keycodes individuels. La table vit dans
`firmware/clavier/src/keymap.c`, à côté de `base_layer` ; le moteur
(`firmware/clavier/src/combo.c`) est du C pur, testable sur hôte avec une
horloge injectée.

**Aucune position n'est normative.** Les positions d'un combo sont des données
utilisateur, comme les positions du keymap (ADR-0029 §7, invariant 6). Cette
page ne fixe ni ne documente aucune position particulière.

## Format de table

```c
#define COMBO_MAX_POSITIONS 8u

typedef struct {
    const uint8_t *positions;   // positions physiques, 1..KEYMAP_POSITION_COUNT
    uint8_t count;              // de 2 à COMBO_MAX_POSITIONS
    uint16_t timeout_ms;        // fenêtre, strictement positive
    uint16_t binding;           // (kind << 8) | value, comme base_layer
} keymap_combo_t;
```

La table est terminée par **une seule entrée sentinelle**
`{ NULL, 0u, 0u, NONE }` ; `keymap_combo_count()` compte les entrées qui la
précèdent. Exemple (à décommenter et adapter : la liste de positions doit
rester vivante tant que la table y fait référence) :

```c
/*
 * static const uint8_t combo_exemple_positions[] = { 73u, 84u };
 * ...
 * static const keymap_combo_t combo_table[] = {
 *     { .positions = combo_exemple_positions, .count = 2u,
 *       .timeout_ms = 50u, .binding = N_DO4 },
 *     { NULL, 0u, 0u, NONE },   // sentinelle, toujours en dernier
 * };
 */
```

`binding` accepte **n'importe quel keycode** : note `N_*`, `ALT_*`, `CLE_*`,
`TEMP_*`, `T_TEMP_*`, `T_GLOB_*`, `T_OCT_*`, `UNTRANSPOSE`, `MAINT`, `GET_UID`.
Le firmware n'impose ni liste blanche, ni mode, ni condition d'usage.

## Sémantique

* **Positions membres uniquement retardées.** Une position qui n'apparaît dans
  aucun combo suit le chemin immédiat : aucun délai ajouté.
* **Appui membre.** L'appui est retenu. Le combo se déclenche dès que toutes
  ses positions sont retenues et que la fenêtre n'est pas dépassée
  (`now − plus ancien appui du groupe < timeout_ms`). Deux positions du même
  lot de scan déclenchent donc immédiatement.
* **Fenêtre par combo.** Chaque entrée a son propre `timeout_ms`, mesuré depuis
  son plus ancien membre encore retenu (appui en attente).
* **Relâchement avant complétion.** Si un membre retenu est relâché avant que
  le combo soit complet, tous les appuis retenus sont émis immédiatement dans
  leur ordre d'appui, puis le relâchement suit le chemin normal. Un tap rapide
  ne subit donc que le temps réellement écoulé.
* **Déclenchement.** Les appuis des membres sont supprimés (jamais émis) ; le
  keycode de sortie est émis une fois, comme un appui physique. La sortie est
  **relâchée au premier relâchement d'un membre** : le combo se comporte comme
  une touche tenue tant que l'accord est tenu. Les relâchements des autres
  membres ne produisent rien.
* **Aucun nouveau déclenchement tant qu'un combo est actif.** Pendant qu'un
  combo est actif, un appui sur une position membre déjà armée est ignoré (pas
  de nouvel appui retenu). Une position ex-membre relâchée peut de nouveau
  entrer en attente, mais aucun déclenchement n'a lieu tant que le combo reste
  actif (des membres armés subsistent) ; une fois tous les membres relâchés,
  elle redevient un appui membre normal.
* **Précédence déterministe.** Si plusieurs combos sont complets au même
  instant, celui qui a le plus de positions l'emporte ; à égalité, le premier
  de la table. Une position peut appartenir à plusieurs combos.
* **Aucune restriction de mode.** Les combos sont actifs au démarrage, en jeu
  et en maintenance, quelles que soient les transpositions, l'armure ou les
  altérations en cours.

## Latence

Le surcoût est borné par le `timeout_ms` du combo concerné, et **nul** pour
toute position non membre. L'anti-rebond du scan n'est pas modifié.

Un membre pressé seul est retenu au plus la plus grande fenêtre des combos qui
le contiennent, puis émis normalement. L'émission différée se fait à la fin du
lot de scan, dans l'ordre d'appui.

## Sortie note littérale

Une sortie de type note est émise **littéralement** : `midi = binding.value`,
sans armure, sans altération temporaire et sans transposition. C'est une
nécessité technique : la transposition existante dépend d'une position physique
(gauche/droite) que la sortie d'un combo n'a pas.

La note littérale est mémorisée dans l'état du combo et relâchée avec lui, par
les mêmes sorties que les notes physiques (CAN et USB MIDI). Elle n'entre pas
dans `note_state` (indexé par position). Elle compte comme note active pour
refuser un auto-test pendant le jeu. En maintenance, elle suit le chemin de
poussée maintenance comme une note physique.

Les keycodes momentanés (`ALT_*`, `CLE_*`, `TEMP_*`, `T_TEMP_*`) sont maintenus
pendant le combo et relâchés avec lui. Lors d'une bascule de mode maintenance,
une sortie de combo encore tenue est relâchée avant de couper les notes.

## Vérification au démarrage

Chaque entrée de la table est contrôlée au boot ; une entrée invalide produit
une ligne `DIAG_CLASS_ALWAYS` :

```
[COMBO] table error index=<i> reason=<raison>
```

Raisons : `null_positions`, `count_lt_2`, `count_gt_max`, `timeout_zero`,
`position_out_of_range`, `duplicate_position`, `note_out_of_range` (note de
binding > 127). Aucune correction ni écriture n'est effectuée : l'entrée
invalide est seulement signalée. Une table vide (sentinelle seule) ne produit
aucun message.
