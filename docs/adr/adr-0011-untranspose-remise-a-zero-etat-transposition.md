---
title: "ADR-0011: Sémantique du keycode UNTRANSPOSE — remise à zéro totale de l'état de transposition et d'altération"
status: "Validated"
date: "2026-10-21"
authors: "Agent Architecte"
tags: ["architecture", "firmware"]
supersedes: ""
superseded_by: ""
---

### Status
**Validated**

### Context

Le keymap du Clavier réserve la position 73 au keycode
`KEYCODE_STORAGE_BEHAVIOR(KEYCODE_UNTRANSPOSE)`
(`firmware/clavier/src/keymap.c:28`), face à des keycodes de transposition
cumulatifs (`[76]` = `KEYCODE_TRANSPOSE_GLOBAL_UP`, plus `T_GlobDown`,
`T_OctLeft*`, `T_OctRight*` et les keycodes `A_<note>`) définis dans
`firmware/clavier/src/keycode.h:7-22`.

Le comportement observé dans le code (`firmware/clavier/src/keycode.c:58-60`)
est déjà une remise à zéro complète : `KEYCODE_UNTRANSPOSE` appelle
`keycode_state_init`, qui écrit `*state = (keycode_state_t){0}` — donc
`global_semitones`, `left_octaves`, `right_octaves`, `alteration_state[7]` et
`change_counter`. La note réellement jouée est calculée à l'appui
(`note_state.c:21-40`) : altération par classe de hauteur d'abord, puis
`global_semitones + octave_offset*12` (`keycode.c:76-85`), puis clamp
[0, 127] (`note_state.c:5-13`). Les notes déjà enfoncées gardent la note MIDI
du Note On (`note_state.c:37`, relâchement `note_state.c:48`). Cet état est
purement volatil en RAM (`main.c:270`), non persisté en flash ; le Moteur ne
connaît pas la transposition, il reçoit la note déjà résolue.

Aucune ADR ne couvre ce keycode, et `docs/specs/clavier_keymap.json` ne le
mentionne pas : le comportement est donc **implémenté mais non décidé**. Deux
risques en découlent :

1. Les incréments de transposition ne sont pas bornés dans
   `keycode_press` (`keycode.c:40-57`, `++`/`--` nus) ; un `int16_t` peut
   déborder après 32 767 appuis. `UNTRANSPOSE` est donc le seul retour
   garanti au keymap d'origine : sa sémantique doit être absolue, jamais
   relative.
2. Les notes tenues au moment de l'appui relèvent d'un choix non écrit :
   re-voquer immédiatement les notes transposées, ou laisser chaque note
   tenue terminer sur sa hauteur d'appui. Ce choix touche des solénoïdes
   réellement actionnés et ne peut pas être laissé à l'appréciation du Codeur.

Le libellé du besoin utilisateur (« remet également toutes les touches ayant
subi une altération, par exemple `T_GlobUp`, à leur état initial ») désigne
l'ensemble des gestes qui éloignent une touche de sa note d'origine :
transpositions globales, transpositions d'octave gauche/droite et altérations
par classe de hauteur. Il ne s'agit pas seulement d'un « untranspose »
partiel.

### Decision

**Décision 1 — Remise à zéro absolue et totale.** L'appui sur
`KEYCODE_UNTRANSPOSE` rétablit d'un seul coup l'état initial de toutes les
couches de transformation du Clavier, tel que défini une seule fois par
`keycode_state_init` :

- `global_semitones = 0`, `left_octaves = 0`, `right_octaves = 0` ;
- `alteration_state[0..6] = 0` (aucun dièse/bémol actif) ;
- `change_counter = 0` et `alteration_change[0..6] = 0`.

Après l'appui, toute touche de note produit exactement sa note de keymap
d'origine (note de base de la table de commissioning, sans altération ni
décalage). La remise à zéro ne dépend ni du nombre d'appuis accumulés, ni de
l'ordre des gestes, ni de la valeur courante des décalages : elle est
idempotente et absolue. Une seule fonction définit « l'état initial » (celle
utilisée au boot), pour interdire deux définitions divergentes.

**Décision 2 — Aucune note tenue n'est re-voquée.** Les notes déjà enfoncées
au moment de l'appui conservent la hauteur MIDI du Note On qui les a
déclenchées ; leur Note Off reprend cette même hauteur. `UNTRANSPOSE` ne
génère aucun Note Off/Note On de rattrapage, aucun CC, aucune trame CAN. Le
nouvel état ne s'applique qu'aux appuis suivants. Aucun solénoïde n'est donc
ré-actionné par un geste de contrôle.

**Décision 3 — Geste local, non persistant, sans effet de bord de mode.**
L'appui est traité sur le front d'appui uniquement (relâchement sans effet,
comme les autres behaviors, `main.c:252-254`). Il n'écrit pas en flash
(invariant 3 : la table de commissioning reste la seule source de vérité et
n'est pas touchée), n'émet pas de SysEx (ADR-0005), ne quitte pas le mode
MAINTENANCE et ne modifie pas l'état `GET_UID`. Au boot, l'état reste
l'état initial (comportement inchangé).

**Décision 4 — Traçabilité obligatoire sur le journal CDC.** L'appui produit
une ligne de diagnostic sur le port série CDC indiquant la remise à zéro et
les valeurs abandonnées (`global_semitones`, octaves gauche/droite, altérations
actives). Un geste qui efface l'état de jeu ne doit jamais être silencieux ;
c'est le seul feedback disponible tant qu'aucun pilote LED RGB n'existe dans
`firmware/`. Quand le feedback RGB sera implémenté, il devra en plus signaler
visuellement la remise à zéro, sans que cette ADR impose un motif.

**Décision 5 — La spécification et le test manuel doivent couvrir
UNTRANSPOSE.** `docs/specs/clavier_keymap.json` (bloc des behaviors) doit
décrire cette sémantique et `tests/clavier_keycode_keymap_manual.md` doit
inclure un cas de remise à zéro après cumul de transpositions globales et
d'octave et après plusieurs altérations. Ces mises à jour relèvent du
Documentaliste, pas du Codeur.

### Consequences

#### Positive
* **POS-001**: Une seule définition de « l'état initial » (boot et reset
  partagent `keycode_state_init`), donc aucun risque de divergence entre
  l'état au démarrage et l'état après `UNTRANSPOSE`.
* **POS-002**: Sortie garantie d'un état de transposition non borné côté
  accumulation (`keycode.c:40-57`) : l'instrument ne peut pas rester coincé
  hors de son keymap d'origine.
* **POS-003**: Aucune re-voquation des notes tenues : pas de rafale de
  Note Off/On, donc pas de ré-actionnement massif de solénoïdes ni de charge
  CAN parasite sur un geste de contrôle.
* **POS-004**: Le comportement actuellement implicite devient auditable
  (ligne CDC, spec, test manuel), ce qui protège des régressions lors des
  futures retouches du keymap.

#### Negative
* **NEG-001**: Aucun « undo » granulaire : après un appui involontaire, toutes
  les transpositions et altérations préparées sont perdues et doivent être
  reconstruites geste par geste (assumé : c'est le prix d'un reset absolu).
* **NEG-002**: Pendant la transition, un accord tenu peut sonner à cheval sur
  deux hauteurs (anciennes notes tenues, nouvelles notes au keymap d'origine).
  Effet borné à la durée des notes tenues et préférable à un re-déclenchement.
* **NEG-003**: Le `change_counter` remis à zéro réinitialise l'horloge de
  récence des altérations ; sans conséquence tant que la remise à zéro est
  atomique, mais toute remise à zéro partielle rendrait la règle « la plus
  récente gagne » incohérente.
* **NEG-004**: Les décalages restent non bornés à l'accumulation ; le reset ne
  corrige pas le risque théorique de débordement `int16_t`, il n'en fournit
  que l'issue.

### Alternatives Considered

#### Undo relatif (annuler le dernier geste de transposition)
* **ALT-001**: **Description**: Conserver une pile des gestes et faire de
  `UNTRANSPOSE` un retour arrière pas à pas, en décrémentant les compteurs
  inversement aux appuis.
* **ALT-002**: **Rejection Reason**: Ne ramène pas au keymap d'origine si
  plusieurs gestes ont été faits, exige une pile et une politique de
  débordement, et rend le retour dépendant de l'historique plutôt que d'un
  état de référence unique. Le besoin exprimé est un retour à l'état initial.

#### Reset partiel (transpositions seules, altérations conservées)
* **ALT-003**: **Description**: Ne remettre à zéro que
  `global_semitones`/octaves, en laissant `alteration_state[]` intact.
* **ALT-004**: **Rejection Reason**: La note d'une touche resterait altérée,
  donc pas d'état initial réel ; contredit directement le besoin. Un musicien
  devrait ensuite remettre chaque altération à zéro par trois appuis.

#### Re-voquation des notes tenues (Note Off puis Note On transposé)
* **ALT-005**: **Description**: À l'appui, réémettre un Note Off sur l'ancienne
  hauteur et un Note On sur la nouvelle pour chaque note active, afin de
  garder une cohérence immédiate.
* **ALT-006**: **Rejection Reason**: Déclenche jusqu'à 49 solénoïdes
  simultanément sur un geste de contrôle (courant d'appel, usure mécanique,
  rafale de trames CAN), pour un bénéfice musical nul pendant un accord tenu.
  Risque disproportionné au regard de la fenêtre transitoire.

#### Persistance de l'état de transposition hors flash de commissioning
* **ALT-007**: **Description**: Sauvegarder l'état courant (par exemple en
  flash) pour le retrouver au boot.
* **ALT-008**: **Rejection Reason**: Usure flash à chaque appui, état de jeu
  inattendu au démarrage et confusion avec la table de commissioning, seule
  source de vérité des rôles (invariants 2 et 3). L'état reste volatil.

### Implementation Notes
* **IMP-001**: Aucun changement d'interface partagée : ni `shared/protocol/`,
  ni trame CAN. La transposition est purement Clavier ; le Moteur reçoit
  toujours une note résolue. Ne pas ajouter de message de contrôle pour ce
  geste.
* **IMP-002**: Réutiliser `keycode_state_init` comme unique définition de
  l'état initial ; ne pas dupliquer la liste des champs à zéroter dans un
  nouveau chemin. Une remise à zéro partielle (champs oubliés) casse la règle
  de récence des altérations (cf. NEG-003).
* **IMP-003**: Ne toucher ni `note_state` ni les notes mémorisées à l'appui :
  le relâchement doit continuer à renvoyer la note du Note On
  (`note_state.c:42-53`), sinon notes collées côté Moteur.
* **IMP-004**: Journaliser l'événement sur le chemin CDC existant
  (`main.c:255-262`, ADR-0005) avec les valeurs avant remise à zéro ; ne pas
  réintroduire de SysEx.
* **IMP-005**: Deux constats hors périmètre, à ne pas corriger sous couvert de
  cette ADR : (a) le `MAINTENANCE_TOGGLE` de `main.c:243-248` est imbriqué dans
  le bloc `GET_UID` et donc inatteignable à cet endroit ; (b) les décalages
  d'octave sont non bornés (`keycode.c:46-57`). Chacun relève d'un traitement
  séparé par le circuit habituel (Validateur → Architecte si un choix est
  requis).
* **IMP-006**: Le Validateur doit vérifier, en plus du cas manuel ajouté par le
  Documentaliste, qu'un appui sur `UNTRANSPOSE` pendant qu'une note est tenue
  produit exactement un Note On et un Note Off de même hauteur, sans trame
  CAN supplémentaire.

### References
* **REF-001**: `AGENTS.md` — invariants 2, 3 et 5 (aucune interface partagée
  modifiée ici).
* **REF-002**: `firmware/clavier/src/keycode.h:7-22,35-57` — keycodes de
  transposition, altération, `UNTRANSPOSE`, encodage `KEYCODE_STORAGE_*`.
* **REF-003**: `firmware/clavier/src/keycode.c:22-28,34-85` —
  `keycode_state_init`, `keycode_press`, `keycode_transposition_for_position`.
* **REF-004**: `firmware/clavier/src/note_state.c:5-54` — ordre altération →
  transposition → clamp, mémorisation de la note au Note On.
* **REF-005**: `firmware/clavier/src/keymap.c:28-39` — positions 73 à 84
  (behaviors `UNTRANSPOSE`, `T_GlobUp`, altérations).
* **REF-006**: `docs/adr/adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md`
  — feedback de diagnostic sur CDC, pas de SysEx.
* **REF-007**: `docs/adr/adr-0010-ancrage-grave-mapping-commissioning.md` —
  note de base de la table de commissioning dont l'état initial est la
  référence.
* **REF-008**: `docs/specs/clavier_keymap.json:13-19,761-763` et
  `tests/clavier_keycode_keymap_manual.md` — comportements déjà spécifiés à
  compléter pour `UNTRANSPOSE` (Décision 5).
