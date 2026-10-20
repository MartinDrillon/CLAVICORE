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
4. **Les limites électriques sont appliquées dans la couche pilote**, pas dans
   la couche métier. Un solénoïde ne doit jamais pouvoir être activé hors de sa
   fenêtre de courant/durée (2–3 A, bobine sur mesure) sans coupure matérielle
   ou logicielle explicite, **sauf deux dérogations bornées dans la couche
   pilote** : le profil **MAINTENANCE** d'ADR-0006 (push-only, coupure à 10 s)
   et la phase **HOLD** du jeu normal d'ADR-0007 (coupure autonome à 20 s, duty
   HOLD plafonné à 25 %).
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

- **Architecte** (`architecte`) : décide, écrit les ADR (`Proposed`) et leur
  ligne dans `docs/adr/INDEX.md`. Pas de code d'implémentation.
- **Documentaliste** (`documentaliste`) : specs issues des ADR acceptées
  (schémas CAN, docs de module), procédures de build/test, index des ADR,
  `docs/specs/Detail_architecture.md`.
- **Codeur** (`codeur`) : implémente contre `shared/protocol/` et les docs du
  Documentaliste.
- **Validateur** (`validateur`) : compile, teste, identifie l'erreur et
  l'oriente vers l'agent adapté.
- **Maître** (`maitre`) : dernier recours, lancé seul quand le cycle est bloqué.
- **Explorateur** (`explorateur`) : sous-agent de recherche en lecture seule,
  modèle économique.

## Workflow d'agents sous Kilo

Configuration : agents dans `.kilo/agent/`, commandes dans `.kilo/command/`.
La chaîne reste pilotée par l'humain :

```
Architecte (ADR Proposed) → validation humaine (Accepted) → Documentaliste
→ Codeur → Validateur → Documentaliste (mode B)
```

- `/adr <besoin>` : Agent Architecte.
- `/documenter <adr|rapport>` : Agent Documentaliste.
- `/implementer <tâche>` : Agent Codeur.
- `/valider` : Agent Validateur.
- `/debloquer <incident>` : Agent Maître, seul, hors chaîne.
- `/explorer <question>` : Agent Explorateur, en sous-tâche lecture seule.

Les boutons de relais de Copilot n'existent pas sous Kilo : un agent ne
déclenche pas lui-même un agent primaire. L'humain change d'agent avec
`/agents` (ou Tab) ou lance la commande correspondante. Seul l'Explorateur,
sous-agent, s'invoque depuis une session avec l'outil `task`.

Permissions posées dans les agents : Architecte, Documentaliste et Explorateur
sont sans terminal (`bash: deny`) ; le Codeur demande confirmation pour toute
commande ; le Validateur et le Maître exécutent les builds et tests librement.
Chaque agent fixe aussi son modèle dans son frontmatter (`model:`) ; le Maître
est le seul sur un modèle premium, à réserver aux incidents bloqués.

Agent Manager : `.kilo/setup-script.ps1` initialise le sous-module can2040 dans
les worktrees. Les caches CMake de `build/` contiennent des chemins absolus :
dans un worktree, reconfigurer dans un dossier de build propre plutôt que
réutiliser `build/` tel quel.