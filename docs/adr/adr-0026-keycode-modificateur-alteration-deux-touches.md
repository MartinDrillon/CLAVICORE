---
title: "ADR-0026: Keycode modificateur d'altération à deux touches (Bémol / Dièse)"
status: "Accepted"
date: "2026-10-25"
authors: "Agent Architecte"
tags: ["architecture", "firmware"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

### Context

Aujourd'hui l'altération est portée par cinq keycodes `KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_RE|MI|SOL|LA|SI)`, chacun avec un cycle à trois états (base → dièse → bémol → base). Un appui agit sur la classe de hauteur de la racine, pour toutes les octaves, et l'inversion est un échange de deux positions (par exemple RÉ ↔ RÉ bémol) : l'état 1 échange la naturelle et sa dièse, l'état 2 la naturelle et sa bémol (`keycode.c`, `keycode_alteration_delta`).

Ce mécanisme mobilise cinq positions physiques (74, 75, 81, 82, 83) pour un résultat que l'humain veut obtenir avec deux touches : une touche « bémol » et une touche « dièse », maintenues pendant l'appui de la note. Aucun mécanisme de touche maintenue n'existe : actuellement chaque keycode est résolu seul, à son propre appui.

### Decision

1. **Deux valeurs seulement.** `KEYCODE_ALTERATION` ne porte plus les racines DO…SI mais deux directions : `KEYCODE_ALTERATION_B` (bémol, −1 demi-ton) et `KEYCODE_ALTERATION_D` (dièse, +1 demi-ton). Les keycodes A_re, A_mi, A_fa, A_sol, A_la, A_si disparaissent du jeu de keycodes et du keymap. Aucune donnée persistée n'encode un keycode : la base layer est recompilée, aucun format de commissioning n'est touché.

2. **Ce sont des modificateurs maintenus, pas des bascules.** L'appui ou le relâchement d'une touche B/D seule ne produit ni note MIDI ni changement d'altération. Le changement n'a lieu qu'à l'appui d'une touche **NOTE** alors que le modificateur est déjà enfoncé. Une note déjà tenue au moment où B/D est pressé n'est ni modifiée ni rejouée ; son Note Off reste celui mémorisé à l'appui.

3. **Le modificateur choisit la direction, la note choisit la racine.** Soit `p` la classe de hauteur de la note physique pressée et `N` l'ensemble des naturelles {DO, RÉ, MI, FA, SOL, LA, SI} :
   - `D` (dièse) : racine `r = p` si `p` est naturelle, sinon `r = p − 1 (mod 12)` (la naturelle sous la note) ; l'échange est `{r, r+1}`.
   - `B` (bémol) : racine `r = p` si `p` est naturelle, sinon `r = p + 1 (mod 12)` (la naturelle au-dessus) ; l'échange est `{r, r−1}`.
   Ainsi `B + RÉ` échange RÉ et RÉ bémol (la touche DO#/RÉb joue RÉ), `D + RÉ` échange RÉ et RÉ dièse, `D + SI` échange SI et DO (la touche DO joue SI et réciproquement), Le modificateur appliqué à une touche noire est interprété selon sa direction, comme aujourd'hui.

4. **Bascule à deux états par racine, dernier changement gagne.** Chaque racine conserve trois états (0 = base, 1 = échange dièse, 2 = échange bémol) et son horodatage de changement. L'appui du couple modificateur + note applique :
   - `D` : nouvel état = 0 si l'état courant vaut 1, sinon 1 ;
   - `B` : nouvel état = 0 si l'état courant vaut 2, sinon 2.
   L'horodatage est actualisé à chaque appui. La résolution d'une note continue d'utiliser la règle existante : parmi les racines dont l'état donne un décalage non nul pour la classe de hauteur, le changement le plus récent prime (conflit entre positions/sons enharmoniques : dernière pression gagne). La note est calculée **après** la bascule, puis la transposition s'applique comme aujourd'hui.

5. **Simultanéité déterministe.** Dans un même lot de scan (modificateur et note devenus stables au même cycle), les appuis de modificateurs sont pris en compte avant les appuis de notes ; l'état utilisé est celui du cycle. Les relâchements sont traités après les appuis du lot. Si la note est pressée avant le modificateur (cycles différents), le modificateur n'a aucun effet sur elle, conformément au point 2.

6. **Keymap.** `B` en position 74 (ex-A_re) et `D` en position 75 (ex-A_mi), deux touches adjacentes de la colonne centrale gauche. Les positions 81, 82, 83 passent à `KEYCODE_NONE` (libres) ; 61 et 72 restent libres. Le choix des deux positions est un choix d'ergonomie révisable par l'humain sans changer la sémantique.

7. **Référence des keycodes.** Un fichier `docs/specs/keycodes.md` liste, une ligne par keycode, les keycodes non-note et leur effet, et les positions assignées. Il est tenu à jour à chaque évolution de la base de keycodes (doc-aval).

8. **Mode maintenance inchangé.** Les modificateurs sont ignorés en mode maintenance (aucune bascule d'altération) ; les notes de maintenance gardent l'altération déjà mémorisée, comme aujourd'hui.

### Consequences

#### Positive
* **POS-001**: Deux touches au lieu de cinq, geste unique et mémorisable (maintenir B ou D, jouer la note), sans cycle à trois états à parcourir.
* **POS-002**: La sémantique d'échange de positions déjà validée (RÉ ↔ RÉ bémol, SI ↔ DO) est conservée ; seul le déclencheur change.
* **POS-003**: Le panneau central libère trois positions de contrôle, disponibles pour de futurs keycodes.
* **POS-004**: Le fichier `docs/specs/keycodes.md` donne une vue unique, tenue à jour, de la base de keycodes.

#### Negative
* **NEG-001**: Une note déjà tenue ne profite pas d'un modificateur pressé après elle : il faut relâcher puis rejouer la note (comportement demandé, mais différent d'un « shift » classique).
* **NEG-002**: Le cycle n'est plus strictement à trois états : B puis D sur la même racine remplace l'état au lieu de le parcourir ; c'est la règle « dernière pression gagne ».
* **NEG-003**: Le keymap et `docs/specs/clavier_keymap.json` changent ; les anciennes combinaisons A_re/A_mi/A_sol/A_la/A_si ne sont plus disponibles.

### Alternatives Considered

#### Conserver les cinq keycodes d'altération à cycle 3 états
* **ALT-001**: **Description**: garder A_re…A_si et ajouter B/D en plus.
* **ALT-002**: **Rejection Reason**: deux mécanismes concurrents pour la même fonction, cinq positions occupées, et un état ambigu entre l'ancien cycle et les nouveaux modificateurs.

#### Modificateur en bascule (appui B, puis appui note, sans maintenir)
* **ALT-001**: **Description**: B/D basculent un mode persistant jusqu'au prochain appui.
* **ALT-002**: **Rejection Reason**: l'humain demande explicitement un maintien ; une bascule persistante laisse la carte dans un état modifié sans geste visible et complique le diagnostic.

#### Cycle trois états conservé derrière chaque modificateur
* **ALT-001**: **Description**: B parcourt base → bémol → double bémol, D base → dièse → double dièse.
* **ALT-002**: **Rejection Reason**: non demandé, et les positions physiques n'ont pas de double altération ; le besoin est une bascule naturelle ↔ altérée.

### Implementation Notes

* **IMP-001**: `keycode.h` — réduire `keycode_alteration_t` à `KEYCODE_ALTERATION_B` et `KEYCODE_ALTERATION_D` (plus une constante de racines pour l'état) ; les états restent indexés par racine naturelle, avec l'horodatage existant. Aucune valeur de keycode n'est persistée, mais la base layer et la spec JSON doivent être mises à jour dans le même lot.
* **IMP-002**: `keymap.c` — 74 = B, 75 = D, 81/82/83 = `KEYCODE_NONE`. Vérifier `keymap_keycode_for_position` et le nommage (`keycode_name` : « A_b », « A_d » ou équivalent cohérent).
* **IMP-003**: `main.c` — suivre l'état maintenu des deux modificateurs (appui/relâchement) et appliquer la bascule au moment de l'appui d'une note, avant `note_state_press`. Prévoir l'ordre déterministe du point 5 (appuis modificateurs → appuis notes → relâchements) dans le traitement d'un lot de scan, sans modifier le contrat `matrix_scan_event_handler_t` si possible.
* **IMP-004**: Si les deux modificateurs sont maintenus, le dernier pressé des deux encore enfoncés détermine la direction ; le documenter dans le code.
* **IMP-005**: Tests — test hôte de la résolution (naturelle/dièse/bémol, touche noire selon la direction, bascule aller-retour, conflit dernière pression, bornes MIDI 0/127, SI ↔ DO) et procédure manuelle USB-MIDI dans `tests/` (Note On/Off identiques, note tenue non modifiée).
* **IMP-006**: Journal CDC en TRACE sur l'appui d'une note avec modificateur : position, modificateur, racine, nouvel état, note résolue. Aucun nouveau niveau ni nouvelle trame CAN.
* **IMP-007**: `docs/specs/keycodes.md` — une ligne par keycode non-note (effet + positions), créé et maintenu par doc-aval ; mettre à jour `docs/specs/clavier_keymap.json` (section `alteration`) et `docs/specs/LAYOUT DE LA MATRICE.txt` (A_fa/A_do obsolètes, 74/75 renommés).

### References
* **REF-001**: [./adr-0011-untranspose-remise-a-zero-etat-transposition.md](./adr-0011-untranspose-remise-a-zero-etat-transposition.md) — UNTRANSPOSE remet à zéro l'état d'altération.
* **REF-002**: `firmware/clavier/src/keycode.c` — `keycode_alteration_delta`, `keycode_alteration_for_note`.
* **REF-003**: `firmware/clavier/src/keymap.c` — base layer, positions 73–84.
* **REF-004**: `docs/specs/clavier_keymap.json` — section `alteration` (règles de cycle à remplacer).
