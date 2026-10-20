---
title: "ADR-0008: Convention de nommage des octaves — notation scientifique (MIDI 36 = DO2, 84 = DO6)"
status: "Accepted"
date: "2026-10-20"
authors: "Agent Architecte (rédigé sur décision humaine)"
tags: ["architecture", "firmware", "protocol", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted** 

Amende partiellement, **sans les superséder** :

* ADR-0004 §6 (« Nommage Yamaha C3 = 60 ») et sa liste de divergences
  documentaires (ligne 17) ;
* ADR-0005, dernier paragraphe des lignes de journal (`DO1 = MIDI 36` …
  `DO5 = MIDI 84`, l.145-146).

Aucune dérogation à un invariant d'`AGENTS.md` : l'invariant 5 est satisfait
par la présente ADR, et aucune structure de `shared/protocol/` n'est modifiée.

### Context

**Demande.** La Roadmap (l.132-133) fixe « touche 1 = DO1 = MIDI 36 ; touche 49
= DO5 = MIDI 84 ». L'utilisateur demande de se recaler sur la convention MIDI de
fait : MIDI 36 = **DO2** et MIDI 84 = **DO6**. Les **numéros MIDI ne changent
pas** ; c'est l'**étiquette d'octave** associée à ces numéros qui doit être
corrigée.

**Doute exprimé : « est-ce que ce n'était pas déjà le cas ? » — réponse
factuelle : à moitié.** Le dépôt contient aujourd'hui deux nommages
incompatibles, décalés d'une octave :

* **Déjà en notation scientifique (C4 = 60) — à conserver :**
  * `firmware/clavier/src/keycode.h` : `N_DO0 = 12`, `N_DO1 = 24`,
    `N_DO2 = 36`, `N_DO3 = 48`, `N_DO4 = 60`, `N_DO5 = 72`, `N_DO6 = 84`,
    commentaire « Octave 4 (DO4 = MIDI 60) » (l.63-64, 137, 174) ;
  * `firmware/clavier/src/keymap.c` (p. ex. `[8] = N_DO6`) ;
  * `docs/specs/clavier_keymap.json` (`"36":"N_do2"`, `"84":"N_do6"`) ;
  * `docs/specs/LAYOUT DE LA MATRICE.txt` (`36=N_do2`, `84=N_do6`).
* **En nommage Yamaha (C3 = 60) — à corriger :**
  * ADR-0004 §6 et sa note l.17 (« Nommage Yamaha C3 = 60 ») ;
  * ADR-0005 l.145-146 ;
  * `docs/specs/CAN.md` §« Convention musicale figée » (l.295-296) et le
    tableau d'exemple (l.314-318, `DO5`, `LA#4`, `SI4`) ;
  * `docs/specs/Roadmap.md` l.132-133 ;
  * **le journal CDC lui-même** : `diag_format_note_name_fr()`
    (`firmware/clavier/src/diag_log.c` l.312-332) calcule
    `octave = midi / 12` puis affiche `octave - 2`, donc `DO1(36)` … `DO5(84)`.

Le keymap physique est donc **déjà** celui que l'utilisateur décrit : la note
MIDI 84 y est étiquetée DO6. C'est la documentation **et** l'affichage
diagnostic qui sont restés sur l'ancienne convention. Le dépôt n'est pas
« déjà correct » : il est incohérent d'une octave entre le code et les docs.

**Ce qui n'est pas en cause.** Les numéros MIDI sont fixés en code et ne
bougent pas : `shared/protocol/commissioning_table.h` (`COMMISSIONING_KEY_MIN 1`,
`COMMISSIONING_KEY_MAX 49`, `MIDI_NOTE_MIN 36`, `MIDI_NOTE_MAX 84`,
`MIDI_NOTE_OFFSET 35`) et `commissioning_table.c` (`key = midi − 35`,
`midi = key + 35`). « Touche 49 = MIDI 84 » est vrai partout dans le dépôt.
Aucune trame CAN, aucun format de table de commissioning, aucune constante de
`shared/protocol/` ne change : il s'agit d'un **nommage**, pas d'un contrat.

**Précision de vocabulaire.** La spécification MIDI ne définit aucun nom
d'octave pour un numéro de note. La convention de fait des outils MIDI (notation
scientifique, C4 = 60) donne 36 = C2 et 84 = C6, soit DO2 et DO6 : c'est celle
que le code applique déjà et celle qui est retenue ici. Le nommage Yamaha
(C3 = 60) est l'autre convention courante, à l'origine de l'écart constaté.

### Decision

1. **Convention unique et normative : notation scientifique, C4 = MIDI 60.**
   Conséquences : MIDI 36 = **DO2**, MIDI 60 = **DO4**, MIDI 84 = **DO6**. Le
   nommage de `keycode.h` / `keymap.c` / `clavier_keymap.json` /
   `LAYOUT DE LA MATRICE.txt` devient la **référence** ; aucun de ces fichiers
   n'est modifié.
2. **Aucune valeur musicale ni aucun mapping ne change.** Touche 1 = MIDI 36,
   touche 49 = MIDI 84, `touche = note_midi − 35`, plage routable `36..84`. La
   touche 49 (note la plus aiguë) joue **DO6**, sans changer d'index de touche,
   de canal ni d'UID.
3. **Le journal CDC suit la convention** : `diag_format_note_name_fr()` doit
   afficher `DO2(36)` … `DO6(84)`, c'est-à-dire un numéro d'octave égal à
   `midi / 12 − 1`.
4. **Les documents et l'index sont corrigés par le Documentaliste** (CAN.md,
   Roadmap.md) ; les ADR antérieures (0004 §6, 0005) sont **amendées par
   renvoi** depuis la présente ADR, leur corps restant inchangé (un ADR est un
   enregistrement historique).
5. **Les attentes de test qui figent l'ancien libellé** (`DO5(84)`,
   `LA#4(82)`, `SI4(83)`) sont mises à jour en même temps que le code.

### Consequences

#### Positive
* **POS-001**: Une seule convention de nommage dans tout le dépôt (code, docs, journal, tests) : plus d'ambiguïté d'une octave à la lecture.
* **POS-002**: Alignement sur la convention de fait des outils MIDI externes (DAW, éditeurs de partition) : un diagnostic « DO6(84) » est interprétable sans conversion mentale.
* **POS-003**: Correction à risque quasi nul : aucun impact protocole, table, flash ni matériel ; seule une étiquette et son affichage changent.
* **POS-004**: Le keymap et `keycode.h`, déjà corrects, ne sont pas touchés : aucune régression possible sur le routage des notes.

#### Negative
* **NEG-001**: Ambiguïté transitoire : toute documentation, capture ou log antérieur au 2026-10-20 contient l'ancienne étiquette (décalage d'une octave à la relecture).
* **NEG-002**: ADR-0004 (Accepted) devient **partiellement faux** ; sa lecture impose de consulter la présente ADR pour la partie nommage.
* **NEG-003**: Le libellé du journal CDC change, donc les attentes de test existantes deviennent fausses jusqu'à mise à jour : un oubli de grep laisse un test rouge trompeur.

### Alternatives Considered

#### Aligner le code sur le nommage Yamaha (C3 = 60)
* **ALT-001**: **Description**: conserver `DO1 = MIDI 36` et renommer en conséquence `keycode.h` (`N_DO1 = 36` …), `keymap.c`, `clavier_keymap.json` et `LAYOUT DE LA MATRICE.txt`.
* **ALT-002**: **Rejection Reason**: contraire à la demande de l'utilisateur, contraire à la convention de fait des outils MIDI, et touche des fichiers corrects et testés (keymap, JSON) pour un gain nul ; multiplie les risques de régression sur le routage des notes.

#### Supprimer le nom de note et n'afficher que le numéro MIDI
* **ALT-003**: **Description**: le journal CDC n'affiche plus que `[CARD] ... notes=36..84` et `[NOTE] midi=M key=K`, sans nom d'octave.
* **ALT-004**: **Rejection Reason**: supprime une aide à la lecture humaine utile au commissioning et au diagnostic de banc ; ne résout pas l'incohérence des documents, qui continuent de nommer les notes.

#### Double étiquette explicite dans l'affichage (`DO5(Y)/DO6(S)`)
* **ALT-005**: **Description**: afficher les deux conventions avec un suffixe, pour lever l'ambiguïté sans trancher.
* **ALT-006**: **Rejection Reason**: verbeux et ambigu à la lecture rapide, maintient deux conventions en vie et perpétue la confusion que la présente ADR supprime.

### Implementation Notes
* **IMP-001**: `firmware/clavier/src/diag_log.c`, `diag_format_note_name_fr()` (l.312-332) : remplacer `octave - 2u` par `octave - 1u` (l.325) et ajuster la garde anti-débordement `octave < 2u` (l.321) en conséquence (`octave < 1u`). Conserver le contrôle de plage `COMMISSIONING_MIDI_NOTE_MIN..MAX` : pour MIDI 36, `36/12 = 3` donc `3 − 1 = 2` → `DO2(36)` ; pour MIDI 84, `84/12 = 7` → `6` → `DO6(84)`. Vérifier `DO4(60)`.
* **IMP-002**: Ne **rien** modifier dans `shared/protocol/commissioning_table.[ch]` (`MIDI_NOTE_MIN 36`, `MIDI_NOTE_MAX 84`, `MIDI_NOTE_OFFSET 35`), dans `keycode.h` ni dans `keymap.c` : déjà conformes. Confirmer explicitement l'absence de changement de structure partagée.
* **IMP-003**: Mettre à jour les chaînes attendues dans `tests/host/test_phase3_helpers.c` (l.239, 270-271, 303) et `tests/phase3b_bout_en_bout.md` (l.111, 122, 131, 151) : `DO5(84)` → `DO6(84)`, `LA#4(82)` → `LA#5(82)`, `SI4(83)` → `SI5(83)`.
* **IMP-004**: Passer un `grep -rn 'DO[0-9]' docs/ firmware/ tests/ shared/` **et** un grep sur les libellés d'altérations (`LA#4`, `SIb4`, `SI4`, `RE#`, `SOL#`) pour ne laisser aucune occurrence de l'ancienne convention ; les seules occurrences légitimes restantes sont celles de `keycode.h` / `keymap.c` / `keycode.c` (scientifiques).
* **IMP-005**: **Documentaliste** : corriger `docs/specs/CAN.md` (§« Convention musicale figée » l.293-298 et tableau l.314-318) et `docs/specs/Roadmap.md` l.132-133 ; ajouter la ligne ADR-0008 à `docs/adr/INDEX.md` avec la relation d'amendement. Ne **pas** réécrire le corps d'ADR-0004 ni d'ADR-0005 : la note de divergences d'ADR-0004 (l.17) et le renvoi d'ADR-0005 (l.145-146) pointent vers la présente ADR.
* **IMP-006**: Ne pas introduire de constante d'octave partagée ni de fonction de conversion dans `shared/protocol/` : la conversion reste locale à l'affichage (`diag_log.c`), afin de ne pas créer d'interface partagée pour un simple libellé.
* **IMP-007**: La relation `touche 49 ↔ MIDI 84 ↔ DO6` reste établie par `touche = note_midi − 35` ; aucun index de keymap, de canal ou d'UID n'est concerné.

### References
* **REF-001**: [./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) — §6 (règle d'assignation) et note de divergences l.17, amendés.
* **REF-002**: [./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) — l.145-146, amendées.
* **REF-003**: `docs/specs/CAN.md` §« Convention musicale figée », `docs/specs/Roadmap.md` l.132-133.
* **REF-004**: `firmware/clavier/src/diag_log.c` (`diag_format_note_name_fr`), `firmware/clavier/src/keycode.h`, `firmware/clavier/src/keymap.c`, `docs/specs/clavier_keymap.json`, `docs/specs/LAYOUT DE LA MATRICE.txt`, `shared/protocol/commissioning_table.[ch]`.
* **REF-005**: Spécification MIDI 1.0 (aucun nom d'octave normalisé) et notation de hauteur scientifique (C4 = MIDI 60), convention de fait des outils MIDI.
