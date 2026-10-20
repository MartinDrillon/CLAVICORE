---
title: "ADR-0006: Mode maintenance push-only pour assemblage des solénoïdes"
status: "Accepted"
date: "2026-10-18"
authors: "Agent Architecte (rédigé sur décision humaine)"
tags: ["architecture", "firmware", "protocol", "maintenance"]
supersedes: ""
superseded_by: ""
---

### Status
**Dérogation à un invariant — validation humaine requise.**

Dérogation validée par l'utilisateur le **2026-10-18** sur l'invariant 4 de
`AGENTS.md` : un profil **MAINTENANCE** est autorisé **uniquement parce qu'il
reste borné et coupé dans la couche pilote**.

**Accepted**

### Context

L'assemblage mécanique des 49 solénoïdes sous les touches demande un mode
manuel, lent et répétable, où l'opérateur peut pousser un seul canal à la fois
tant qu'il maintient une touche du Clavier. Le workflow voulu est :

1. l'utilisateur ajoute ou déplace des canaux sur une carte Moteur ;
2. il déclenche le mode maintenance depuis le Clavier ;
3. il pousse les notes une par une pour vérifier la correspondance physique ;
4. il relance ensuite un auto-test pour réécrire la table persistante.

Contraintes inchangées :

* pas de remappage automatique ;
* les cartes Moteur restent anonymes et sans notion de rôle musical ;
* la table de commissioning persistante côté Clavier reste la seule source de
  vérité ;
* tout changement de protocole CAN partagé nécessite une ADR préalable
  (invariant 5).

### Decision

#### 1. Activation côté Clavier

* Un nouveau keycode **`KEYCODE_MAINTENANCE_TOGGLE`** est ajouté **en fin** de
  `keycode_kind_t`.
* Son nom d'affichage est **`MAINT`**.
* Il remplace l'entrée `[84] = KEYCODE_STORAGE_ALTERATION(KEYCODE_ALTERATION_FA)`
  de `firmware/clavier/src/keymap.c`.
* **Conséquence assumée** : l'altération **FA** de cette position disparaît et
  n'est **pas** déplacée ailleurs.

#### 2. Conditions d'entrée / sortie

* À l'appui, le keycode bascule entre **jeu normal** et **maintenance**.
* Le Clavier **refuse l'entrée** si la table de commissioning n'est pas valide :
  journal `[STATUS] code=MAINTENANCE_NO_TABLE` et **pas de bascule**.
* Le Clavier **refuse l'entrée** si un auto-test est en cours.
* À l'entrée **comme** à la sortie, le Clavier **coupe tous les solénoïdes
  actifs** avant de continuer.
* Le journal CDC ajoute :
  * `[MODE] maintenance=ON`
  * `[MODE] maintenance=OFF`
* Il n'y a **aucune auto-sortie** par inactivité.

#### 3. Règle fonctionnelle en maintenance

* **Une seule touche à la fois.**
* Si une seconde touche de note est pressée pendant qu'une autre est encore
  considérée active, le Clavier refuse la demande avec
  `[STATUS] code=MAINTENANCE_BUSY`.
* En maintenance, **aucune note MIDI n'est émise** : seul le chemin CAN est
  utilisé.
* La résolution note MIDI → `(UID, canal)` reste faite via la **table de
  commissioning**, exactement comme en jeu normal.
* Tant qu'une touche reste pressée, le Clavier envoie une commande CAN de
  poussée maintenance ; au relâchement, il envoie la coupure.
* Si la durée maximale est atteinte, la coupure automatique se fait côté
  driver Moteur ; un **nouveau cycle n'est autorisé qu'après relâchement**.

Journal CDC :

* `[PUSH] midi=M key=K pos=P ch=C start`
* `[PUSH] midi=M key=K pos=P ch=C stop`
* `[PUSH] midi=M key=K pos=P ch=C timeout`

#### 4. Profil électrique MAINTENANCE (dérogation bornée)

Le profil **MAINTENANCE** est ajouté dans `solenoid_profile.h` et appliqué
exclusivement dans `solenoid_driver.c` :

* `SOLENOID_MAINT_DUTY_PERCENT = 50`
* `SOLENOID_MAINT_MAX_MS = 10000`
* `SOLENOID_MAINT_DUTY_MAX_PERCENT = 100`
* `SOLENOID_MAINT_MIN_REST_MS = 2000` (valeur initiale ajustable)

Règles :

* la poussée maintenance est **PULL uniquement** ;
* **pas** de phase HOLD ;
* **pas** de phase RELEASE inversée ;
* le driver **écrête** le duty et **coupe lui-même** à 10 s ;
* le code métier **ne peut pas dépasser** ces bornes ;
* le profil normal reste inchangé :
  * HOLD à 25 %
  * coupure sécurité HOLD à 2 s *(relevée à 20 s par ADR-0007)*.

Le test utilisateur a validé thermiquement qu'un duty allant jusqu'à **100 %**
pendant **10 s** reste acceptable ; la consigne retenue demeure néanmoins
**50 %**.

#### 5. Transport CAN minimal

La solution minimale retenue, conforme à l'invariant 5, est **d'étendre**
`SOLENOID_COMMAND` au lieu d'ajouter une nouvelle trame :

* ID inchangé : **`0x100`**
* DLC inchangé : **5**
* octets inchangés
* seul le champ `[3] command` est étendu :
  * `0 = OFF`
  * `1 = ON` (jeu normal)
  * `2 = PUSH_ON` (maintenance)

La carte Moteur n'a **aucune notion de mode global** : la commande elle-même
porte le profil à appliquer.

#### 6. Comportement côté Moteur

* `PUSH_ON` applique le profil MAINTENANCE sur le canal ciblé.
* Le Moteur refuse explicitement la commande si :
  * le canal n'est pas `IDLE` ;
  * le repos minimal maintenance n'est pas écoulé ;
  * un auto-test est en cours ;
  * le canal n'appartient pas au bitmap local autorisé.
* La coupure à 10 s reste **locale au driver**.

#### 7. Endurance flash

Le workflow visé peut conduire à environ **49 écritures de secteur** pour une
séance de commissioning incrémentale (une écriture par secteur réécrit).
Avec une endurance annoncée **≥ 100 000 cycles**, ce volume est jugé
**non préoccupant** à l'échelle du produit et ne justifie pas une complexité
supplémentaire (journal circulaire, wear leveling dédié, etc.).

### Consequences

#### Positive
* Assemblage mécanique facilité sans outil externe ni remappage temporaire.
* Dérogation électrique confinée au driver, toujours bornée.
* Aucun état musical embarqué dans le Moteur : l'invariant 2 reste intact.
* Extension CAN minimale, sans nouvel ID ni nouveau DLC.

#### Negative
* Le Clavier doit gérer un état opératoire supplémentaire.
* Une touche occupée monopolise le mode jusqu'au relâchement, même après
  timeout.
* Le mode maintenance retire une altération FA précédemment présente en
  position 84.

### Alternatives Considered

#### Mode global stocké dans la carte Moteur
* **Description**: faire entrer la carte Moteur dans un « mode maintenance »
  persistant ou semi-persistant, puis lui envoyer des commandes ordinaires.
* **Rejection Reason**: introduit une notion de mode dans une carte anonyme,
  complique la synchronisation Clavier/Moteur et dépasse le changement minimal
  requis.

#### Autoriser la maintenance sans table valide
* **Description**: pousser des canaux sans commissioning, par simple numéro de
  touche ou ordre local.
* **Rejection Reason**: contourne la seule source de vérité côté Clavier et
  viole l'exigence explicite « pas de poussée sans table valide ».

#### Nouvelle trame CAN dédiée à la maintenance
* **Description**: créer un nouvel ID pour les commandes push-only.
* **Rejection Reason**: changement d'interface plus large sans bénéfice
  fonctionnel suffisant ; l'extension du champ `command` est plus simple.

### Implementation Notes
* Ajouter `KEYCODE_MAINTENANCE_TOGGLE` en fin de `keycode_kind_t`.
* Étendre `CAN_SOLENOID_COMMAND` avec `PUSH_ON = 2`.
* Ajouter les codes STATUS `MAINTENANCE_BUSY` et `MAINTENANCE_NO_TABLE`.
* Documenter le test manuel PuTTY dans `tests/phase3b_maintenance.md`.

### References
* `AGENTS.md`
* `docs/adr/adr-0001-hal-solenoide-drv8874-phen.md`
* `docs/adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md`
* `docs/adr/adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md`
* `docs/specs/CAN.md`
