---
title: "ADR-0027: Commande console `probe` — déclenchement de l'auto-test IPROPI depuis PuTTY"
status: "Proposed"
date: "2026-10-24"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Proposed**

Aucune dérogation à un invariant. Invariant 1 est respecté : la commande est
une **demande explicite** de l'utilisateur (comme l'appui 3 s sur le bouton),
jamais un auto-test automatique. Aucun changement de `shared/protocol/`
(invariant 5 non sollicité) : aucune trame CAN n'est créée ni modifiée.

### Context

L'auto-test IPROPI n'est déclenché aujourd'hui que par une action locale sur
une carte Moteur : bouton `PIN_BOUTON_TEST` (GPIO34) maintenu 3 s, qui émet
`0x402 IPROPI_SELFTEST_TRIGGER` vers le Clavier (ADR-0004 §2). Le Clavier, seule
autorité, refuse pendant le jeu puis lance la chaîne complète : transfert du
profil d'activation, `0x401 IPROPI_SELFTEST_REQUEST` en diffusion, collecte
A+B+C de **toutes** les cartes, écriture de la table de commissioning, et
signalement de toute divergence (invariant 1).

Les cartes vont être installées au fond du clavecin. Le bouton devient
matériellement inaccessible : le déclenchement doit devenir possible depuis le
poste de travail, par le port série CDC déjà utilisé pour la console (ADR-0005,
ADR-0015), où l'utilisateur tape `probe` dans PuTTY.

Constats qui cadrent la décision :

1. **La chaîne existe déjà côté Clavier et est déjà pilotable par la console.**
   `can_link_request_selftest()` (`firmware/clavier/src/can_link.c:2803`) pose la
   source `"console"`, applique **les mêmes refus** que le bouton (auto-test en
   cours, requête de contrôle en vol, jeu récent < 500 ms), puis appelle
   `begin_profile_sync(&k_solenoid_profile, PROFILE_SYNC_GOAL_SELFTEST, "console")`
   — exactement le chemin du déclenchement `0x402`
   (`can_link.c:2209`, source `"bouton"`). Seul diffère le libellé de source
   porté par la ligne `[TRIGGER]`.
2. **Cette commande est aujourd'hui accessible par la touche `t`**
   (`firmware/clavier/src/bringup_console.c:232-245`), documentée comme
   `t=auto-test` dans l'aide. La demande de l'utilisateur porte donc sur le
   **nom et l'ergonomie**, pas sur un nouveau chemin technique.
3. Le découpage des mots de la console existe déjà : `take_word()`
   (`bringup_console.c:86`) et la branche `p` n'accepte que `p` suivi d'un blanc
   ou de la fin de ligne (`bringup_console.c:227`). Le mot `probe` tombe
   actuellement dans « commande inconnue » : l'ajout est local et sans conflit
   avec la grammaire `p set/get/show/diff/fields/send/reset` d'ADR-0015.
4. `probe_*` est le nom des champs du profil « impulsion d'auto-test »
   (ADR-0004) et ces champs sont **verrouillés** au réglage à chaud
   (ADR-0015 D1, `p fields`). Un mot-clé de commande `probe` au premier niveau
   ne doit donc pas être confondu avec le réglage des champs `probe_*`, qui
   restent sous `p set`.
5. La console est compilable hors du firmware final (`CLAVIER_CDC_CONSOLE=OFF`,
   ADR-0015 IMP-002) : une commande console disparaît avec elle.

### Decision

**1. `probe` est la commande officielle d'auto-test sur la console CDC du
Clavier.**

- Mot unique, **sans argument**, reconnu comme mot complet (`probe` suivi d'un
  blanc ou de la fin de ligne), insensible à la casse comme les autres
  commandes.
- Effet : appel de `can_link_request_selftest()`, **sans aucune logique
  dupliquée**. Toute la chaîne (profil compilé diffusé, `0x401` en diffusion à
  toutes les cartes, délais, timeouts, écriture de la table, journal) reste
  celle d'ADR-0004/0009/0013/0023. `probe` ne crée, ne supprime et ne réordonne
  aucune étape.
- Conséquence assumée et explicitement dite : `probe` fait **exactement** ce que
  ferait l'appui 3 s sur le bouton, y compris l'écriture de la table de
  commissioning et le signalement des divergences. Ce n'est pas un mode
  « lecture seule ».

**2. `t` reste un alias conservé.**

`t` continue de fonctionner et produit le même effet. Il n'est pas supprimé
(documentation, procédures de test et habitudes existantes) mais cesse d'être la
forme recommandée : l'aide `?` affiche `probe` en premier et `t` comme alias.

**3. Argument refusé, pas d'interprétation implicite.**

`probe` suivi d'autre chose (`probe 3`, `probe all`, `probe pos=2`) affiche une
ligne d'usage et **n'émet rien**. Le ciblage d'une carte ou d'un canal n'est pas
introduit : l'auto-test est par définition collectif (ADR-0004 §2, « toutes les
cartes testent et répondent ») et un ciblage exigerait un changement de
`shared/protocol/`.

**4. Le refus et le compte rendu passent par le journal existant.**

- Refus (jeu récent, opération CAN en cours, auto-test déjà en cours) : le code
  `SELFTEST_REFUSED_PLAYING` est déjà émis par `can_link_request_selftest()`.
  La console ajoute **une** ligne explicite, par exemple
  `[CONSOLE] probe -> refuse (voir [STATUS])`. Aucun nouveau code de statut,
  aucune interprétation locale du motif : le journal reste la source.
- Acceptation : ligne `[CONSOLE] probe -> auto-test demande (voir [TRIGGER])`,
  puis les lignes existantes `[TRIGGER]`, `[TABLE]`, `[MAP]`, `[SELFTEST_OHM]`
  et éventuels `[STATUS]` d'anomalie.
- Aucune ligne de synthèse nouvelle n'est inventée par `probe` : le résultat de
  l'auto-test se lit là où il se lit déjà pour le bouton.

**5. Rattachement au build.**

`probe` n'existe que si la console est compilée (`CLAVIER_CDC_CONSOLE=1`, défaut
actuel). Le firmware destiné au clavecin installé doit donc **conserver la
console**, puisque c'est désormais le seul moyen d'atteindre l'auto-test. Cette
contrainte est écrite ici pour ne pas la perdre lors d'une future configuration
finale « sans console ».

### Consequences

#### Positive
* **POS-001**: L'auto-test devient accessible sans toucher les cartes enfoncées
  dans le clavecin, depuis le même PuTTY que le réglage du profil.
* **POS-002**: Aucun changement de trame, de `shared/protocol/` ni de firmware
  Moteur : aucun reflash des cartes n'est nécessaire.
* **POS-003**: Aucune duplication de la chaîne d'auto-test : un seul chemin
  d'exécution pour le bouton, `probe` et `t`, donc pas de divergence de
  comportement à maintenir.
* **POS-004**: Les garde-fous restent ceux d'ADR-0004 (refus pendant le jeu) et
  d'ADR-0013/0023 (profil transféré avant `0x401`, reprises bornées).

#### Negative
* **NEG-001**: `probe` déclenche une **écriture flash de la table** côté Clavier,
  comme le bouton. Un usage répété et mécanique de la commande laisse donc la
  même trace d'usure qu'un usage répété du bouton (aucune usure supplémentaire
  par commande, mais la facilité d'accès peut favoriser la répétition).
* **NEG-002**: L'auto-test envoie le **profil du fichier compilé** ; un réglage à
  chaud obtenu par `p set` est remplacé (comportement ADR-0015, déjà journalisé
  par une ligne `[PROFILE] auto-test: le profil du fichier compile remplace le
  profil regle a chaud`). `probe` ne change pas cette règle, mais la rend plus
  facile à déclencher par inadvertance.
* **NEG-003**: Deux noms pour un même effet (`probe`, `t`) : ambiguïté mineure,
  bornée par l'aide qui désigne `probe`.
* **NEG-004**: Avec `CLAVIER_CDC_CONSOLE=OFF`, le seul déclencheur redevient le
  bouton physique — donc hors d'atteinte dans la configuration installée.

### Alternatives Considered

#### Commande équivalente sur une console CDC de la carte Moteur
* **ALT-001**: **Description**: ajouter un port série sur chaque carte Moteur et
  y recevoir `probe`, la carte émettant alors `0x402` comme le bouton.
* **ALT-002**: **Rejection Reason**: les cartes Moteur n'ont ni console ni USB
  (aucun `usb_log_read_char` côté Moteur) ; cela ajouterait un chemin d'entrée
  par carte et toucherait l'idée que le rôle/le pilotage vit côté Clavier
  (invariants 2 et 3). Le Clavier est déjà la seule autorité d'orchestration.

#### Renommer `t` en `probe` et supprimer `t`
* **ALT-003**: **Description**: `probe` remplace `t`, qui n'est plus reconnu.
* **ALT-004**: **Rejection Reason**: casse les procédures et tests existants
  (`tests/host/test_console_tuning.c`, aide, habitudes) pour un gain nul ;
  conserver un alias ne coûte aucune ligne de logique.

#### Nouvelle trame CAN « trigger console »
* **ALT-005**: **Description**: le Clavier émettrait une trame dédiée vers les
  cartes pour demander l'auto-test.
* **ALT-006**: **Rejection Reason**: `0x402` est Moteur → Clavier et `0x401` est
  déjà la demande Clavier → cartes ; une trame supplémentaire modifierait
  `shared/protocol/` (invariant 5) sans apporter de comportement nouveau.

#### `probe` ciblant une carte ou un canal (`probe <position>`)
* **ALT-007**: **Description**: tester une seule carte pour localiser un défaut
  sans solliciter les autres.
* **ALT-008**: **Rejection Reason**: l'auto-test est collectif et l'écriture de
  la table n'a de sens que si toutes les cartes répondent ; un ciblage
  demanderait une extension de trame et de la machine d'état, hors du besoin
  exprimé (« aussi simple que possible »). À rouvrir par une nouvelle ADR si le
  diagnostic par carte devient nécessaire.

### Implementation Notes
* **IMP-001**: Le point d'insertion est `execute_line()` de
  `firmware/clavier/src/bringup_console.c`, en **mot complet via `take_word()`**,
  avant la branche `p` (qui n'accepte que `p` + blanc/fin). Ne pas ajouter de
  `case 'p'` dans le `switch` : cela capturerait `p`, `probe`, `profil`… et
  changerait la grammaire d'ADR-0015.
* **IMP-002**: `probe` appelle `can_link_request_selftest()` et **rien d'autre** :
  aucun accès direct à `begin_profile_sync()`, `start_selftest_request()` ni aux
  structures `static` de `can_link.c`. `can_link.[ch]` n'a pas besoin d'être
  modifié.
* **IMP-003**: Aide à mettre à jour : `print_help()` et la ligne d'aide du
  journal (`diag_log.c`, ligne `[CONSOLE]`) doivent annoncer
  `probe=auto-test` (et `t` comme alias). Le Documentaliste aligne la procédure
  manuelle (`tests/…`) et toute doc de console.
* **IMP-004**: Cas d'usage à couvrir par le test hôte
  `tests/host/test_console_tuning.c` (stub `can_link_request_selftest`) :
  `probe` → 1 émission ; `PROBE` → 1 émission ; `t` → 1 émission ; `probe 3`
  → 0 émission et ligne d'usage ; `p set probe_` reste une sous-commande de `p`
  (aucune régression de la grammaire de réglage).
* **IMP-005**: PuTTY : conserver « Local echo » / « Local line editing » = *Force
  on* (ADR-0015 IMP-003) ; sinon l'utilisateur ne voit pas ce qu'il tape.
* **IMP-006**: Ne pas déduire de `probe` un droit d'écriture hors des limites du
  pilote : l'invariant 4 reste appliqué côté Moteur (PROBE 3 ms / 20 %, ADR-0004
  addendum 4), sans exception ajoutée.
* **IMP-007**: Vérification matérielle à faire par l'utilisateur : `probe` depuis
  PuTTY produit bien une ligne `[TRIGGER] source=console`, puis les réponses de
  toutes les cartes et, si tout est nominal, `[STATUS] code=TABLE_WRITTEN`.

### References
* **REF-001**: [./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) — auto-test IPROPI, déclenchement par bouton, trame `0x401`
* **REF-002**: [./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) — diagnostic sur console CDC (PuTTY)
* **REF-003**: [./adr-0015-reglage-chaud-profil-console-cdc.md](./adr-0015-reglage-chaud-profil-console-cdc.md) — grammaire de la console, `p` sous-commandes, `CLAVIER_CDC_CONSOLE`
* **REF-004**: [./adr-0013-profil-activation-solenoides-centralise-clavier.md](./adr-0013-profil-activation-solenoides-centralise-clavier.md) et [./adr-0023-synchronisation-profil-reprise-verification-etat.md](./adr-0023-synchronisation-profil-reprise-verification-etat.md) — profil diffusé avant `0x401`, reprises bornées
* **REF-005**: `firmware/clavier/src/can_link.c` (`can_link_request_selftest`, `handle_selftest_trigger`), `firmware/clavier/src/bringup_console.c` (`execute_line`, `take_word`)
