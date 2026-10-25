# AGENTS.md — Clavecin automatique

Source de vérité lue par tout agent IA (Kilo, Claude Code, CLI locales sur
Qwen/Colibri...) avant de toucher au repo. Kilo l'injecte automatiquement dans
chaque session : ne le relis pas. Il fixe les invariants et le mode de travail ;
le détail est dans `docs/specs/Detail_architecture.md`. En cas de conflit avec
un prompt utilisateur, ce fichier gagne — signale le conflit plutôt que de
l'ignorer.

## Contexte en une phrase

Clavecin automatique en deux sous-systèmes reliés en CAN : une carte
**Clavier** (Pico 2 / RP2350, matrice 80 touches, dont 8 switchs de contrôle,
30 LED RGB, MIDI USB+DIN) et jusqu'à 5 cartes **Moteur** interchangeables
(RP2354B, 12 solénoïdes max chacune via drivers DRV8874, 49 solénoïdes au
total) alimentées en 24V/20A.

## Invariants non négociables

Ils priment sur toute optimisation ou suggestion « pratique ». Un agent qui
devrait en enfreindre un s'arrête et demande confirmation humaine.

1. **Pas de remapping automatique.** L'auto-test IPROPI n'est exécuté que sur
   demande explicite (bouton Moteur maintenu 3 s), jamais au boot. Toute
   divergence entre un résultat d'auto-test (ou la liste d'UID présents au
   boot) et la table de commissioning côté Clavier doit être **signalée
   explicitement** (SysEx STATUS, log CDC, refus d'activer le canal concerné).
   Jamais de correction silencieuse.
2. **Les cartes moteur sont anonymes.** Elles exécutent toutes le même
   firmware, s'identifient uniquement par leur UID 64 bits RP2350 et
   n'embarquent aucune connaissance de leur rôle musical. Ce rôle vit
   uniquement dans la table de commissioning (UID → canal → touche) côté
   Clavier.
3. **La table de commissioning est persistante et côté Clavier.** C'est la
   seule source de vérité sur « quel solénoïde joue quelle touche ».
4. **Les limites d'activation des solénoïdes sont celles du profil défini par
   l'humain** (ADR-0014 : toutes les limites sont des données du profil Clavier,
   appliquées par la couche pilote Moteur, jamais figées dans le firmware).
   **Aucun agent n'ajoute, ne durcit ni ne retire une limite électrique de sa
   propre initiative** (bridage, plafond, coupure, budget thermique). En cas de
   doute de sécurité, l'agent le signale à l'humain (« Question sécurité »)
   *avant* de rédiger une ADR ou d'implémenter, et attend sa réponse. Reformulé
   le 2026-10-25 sur demande humaine ; l'ancien texte (fenêtre 2–3 A, dérogations
   MAINTENANCE/HOLD d'ADR-0006/0007) est remplacé par ADR-0014.
5. **Un changement d'interface partagée** (structure d'un message CAN, format
   de la table de commissioning, contrat d'une HAL) nécessite une ADR
   (`docs/adr/`) *avant* l'implémentation. Jamais après, jamais en même temps
   dans le même commit sans référence à l'ADR.

## Frontières de modules

```
firmware/clavier/     → logique carte Clavier (RP2350)
firmware/moteur/      → logique carte Moteur (RP2354B), identique sur les 5 cartes
shared/protocol/      → schéma des messages CAN, table de commissioning : SOURCE UNIQUE
shared/hal/           → interfaces matérielles réutilisables (si pertinent entre cartes)
```

`firmware/clavier` et `firmware/moteur` ne redéfinissent **jamais** une
structure de message ou un format de table déjà défini dans `shared/protocol` :
ils l'importent. Si le besoin d'un module dépasse ce que `shared/protocol`
permet, c'est un signal pour écrire une ADR, pas pour dupliquer.

## Conventions de code

- Langage : C (Pico SDK), pas de C++ sauf justification dans une ADR.
- Toute fonction touchant au CAN, au MIDI ou aux solénoïdes a un test unitaire
  ou, à défaut, un test manuel documenté dans `tests/`.
- Gestion d'erreur explicite (pas de `// TODO handle error`) sur tout chemin
  touchant à l'activation d'un solénoïde ou à la validation de commissioning.
- Commentaires et noms de fonctions en français ou en anglais, mais
  **cohérents dans un même fichier**.

## Mode de travail attendu d'un agent

1. **Contexte ciblé.** ADR : pars de `docs/adr/INDEX.md` et n'ouvre que les
   pertinentes. `docs/specs/Detail_architecture.md` : les sections utiles
   seulement.
2. **Chercher avant de lire**, puis lire par plage de lignes. Pour « où est
   défini X » ou « qui utilise Y », délègue à l'Explorateur (outil `task`,
   `subagent_type: "explorateur"`).
3. **Logs volumineux** : redirige-les vers un fichier et ne lis que l'extrait
   utile (ils sont sinon renvoyés à chaque agent suivant).
4. **Pas d'historique recopié** : pointe vers les fichiers (`AGENTS.md`, ADR,
   doc de module) au lieu de ré-expliquer le contexte.
5. **Interface partagée modifiée** : mets à jour `shared/protocol/` ET l'ADR, et
   signale les modules dépendants. Aucun refactor silencieux d'une struct
   partagée, aucun contrat public cassé sans documentation.

## Répartition des rôles (voir `.kilo/agent/`)

- **Architecte** (`architecte`) : interlocuteur principal. Décide, écrit une ADR
  (`Proposed`, humain valide) **seulement** pour une décision d'architecture du
  firmware, un changement d'interface partagée, une fonction majeure ou une
  dérogation à un invariant ; sinon une fiche de tâche. Après validation, il
  lance seul la chaîne par sous-agents. Pas de code d'implémentation.
- **Maître** (`maitre`) : même rôle que l'Architecte, modèle le plus puissant,
  diagnostic en profondeur. Lancé par l'humain (`/maitre`) en cas de blocage ;
  il orchestre alors la chaîne.
- **doc-amont** (`doc-amont`) : sous-agent. ADR acceptée → fiche de lot pour le
  Codeur. Sauté s'il n'y a pas d'ADR.
- **Codeur** (`codeur`) : sous-agent. Implémente contre `shared/protocol/` et la
  fiche.
- **Validateur** (`validateur`) : sous-agent. Compile, teste à la profondeur
  N0/N1/N2 (`docs/harness/niveaux-validation.md`), diagnostique.
- **doc-aval** (`doc-aval`) : sous-agent. Met la documentation en phase avec ce
  qui est réellement validé.
- **Explorateur** (`explorateur`) : sous-agent de recherche en lecture seule,
  modèle économique.
- **Harness** (`harness`) : hors chaîne. Fait évoluer agents, commandes, skills,
  `AGENTS.md`, `kilo.json` à la demande de l'humain. Ne touche ni au firmware ni
  aux ADR.

## Workflow d'agents sous Kilo

Configuration : agents dans `.kilo/agent/`, commandes dans `.kilo/command/`,
procédure de chaîne dans le skill `.kilo/skill/chaine/SKILL.md` (source unique,
partagée par l'Architecte et le Maître).

```
Humain ⇄ Architecte ── ADR ? ── validation humaine ──┐
                                                     ▼
   (automatique, sous-agents)  doc-amont* → codeur → validateur ⟲ codeur
                                                     → doc-aval → rapport
   * sauté sans ADR.   Blocage → l'humain lance /maitre
```

Règles de la chaîne : fiche de tâche avec niveau de validation minimal ; deux
allers-retours codeur↔validateur au plus ; retour des sous-agents au format
`STATUT: OK|ECHEC|BLOQUE` (30 lignes au plus) ; un sous-agent ne pose pas de
question à l'humain (`BLOQUE`, l'orchestrateur relaie) ; aucun commit
automatique.

Commandes :

- `/adr <besoin>` : Architecte (ADR ou fiche de tâche, puis chaîne).
- `/maitre <incident>` : Maître (diagnostic et orchestration, modèle puissant).
- `/harness <demande>` : agent Harness, hors chaîne.
- `/documenter <adr>`, `/implementer <tâche>`, `/valider`,
  `/documenter-aval <rapport>` : sous-agents appelés à la main (hors
  orchestration).
- `/explorer <question>` : Explorateur, en sous-tâche lecture seule.

Un agent primaire ne s'invoque pas depuis une autre session : l'humain change
d'agent avec `/agents` (ou Tab) ou lance la commande. Seuls les sous-agents
s'invoquent avec l'outil `task`.

Permissions : l'Architecte n'écrit que dans `docs/adr/` et n'a pas de terminal ;
le Maître, le Codeur et le Validateur ont un terminal sans `commit`, `push`,
`reset`, `stash`, `clean`, ni flash ; le Validateur n'édite pas ; doc-amont et
doc-aval n'ont pas de terminal ; le Harness n'écrit que dans le harness.
Chaque agent fixe son modèle dans son frontmatter (`model:`) : le Maître est le
seul sur le modèle le plus puissant.
Agent Manager : `.kilo/setup-script.ps1` initialise le sous-module can2040 dans
les worktrees. Les caches CMake de `build/` contiennent des chemins absolus :
dans un worktree, reconfigurer dans un dossier de build propre plutôt que
réutiliser `build/` tel quel.