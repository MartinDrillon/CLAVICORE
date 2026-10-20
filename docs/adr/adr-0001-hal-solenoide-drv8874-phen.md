---
title: "ADR-0001: Contrat HAL de commande solénoïde DRV8874 (mode PH/EN)"
status: "Proposed"
date: "2026-09-27"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "hardware"]
supersedes: ""
superseded_by: ""
---

### Status
**Proposed**

### Context

La carte Moteur pilote 12 canaux de solénoïdes via des drivers DRV8874PWPR
(IC4-IC11, IC13-IC16 sur `Solenoide_drive.kicad_sch`). La Roadmap (Phase 2a)
prévoit une machine à états par canal (`IDLE -> PULL -> HOLD -> RELEASE`) où
la phase `RELEASE` nécessite une **inversion de polarité**. Avant de laisser
le Codeur écrire `pwm_hal.c/h` et `solenoid_driver.c/h`, le contrat électrique
réel doit être verrouillé à partir du netlist généré
(`Netlist/Solenoide_drive.xml` et `docs/specs/pinmap.generated_drive.yaml`),
car le nommage des nets du schéma induit en erreur.

Faits vérifiés dans le netlist :

1. **PMODE (pin 16) de chaque DRV8874 est tiré au même net que GND/PGND/EP**
   (`GND_15`, `PGND_9`, `EP_17` regroupés). D'après la datasheet TI, PMODE bas
   sélectionne le **mode PH/EN** : pin 1 = EN/IN1 (vitesse/courant, PWM), pin 2
   = PH/IN2 (direction, simple niveau logique).
2. **Piège de nommage** : les nets `/SENS0` à `/SENS11` du schéma (et donc les
   symboles générés `SENS0`..`SENS11` dans `pinmap.generated_drive.yaml`) ne
   portent **pas** le signal analogique IPROPI. Ils relient en réalité le GPIO
   du RP2354B à la pin 2 (PH/IN2) de chaque DRV8874 — c'est le **signal de
   direction**, pas une mesure de courant.
3. Le vrai signal IPROPI (pin 6 de chaque DRV8874) part vers des nets
   `/ADC0`..`/ADC11`, qui rejoignent les entrées Y0..Y11 d'un multiplexeur
   analogique 74HC4067 (IC17). La sortie commune du mux (`/MUX OUT`) alimente
   une unique broche ADC du RP2354B (GPIO45_ADC5), sélectionnée par 4 lignes
   `/S0`../S3` + `/MUX_EN`. Il n'existe donc **aucune lecture de courant
   directe et simultanée sur les 12 canaux** : un seul canal à la fois peut
   être échantillonné via le mux.
4. `NSLEEP` (pin 3) est un **net unique partagé** (`/nSLEEP`, GPIO15) entre les
   12 drivers : il n'existe pas de sleep individuel par canal.
5. `NFAULT` (pin 4) est câblé individuellement par canal (`/FAULT0`..`/FAULT11`),
   un GPIO dédié par canal.

### Decision

Le HAL Moteur (`pwm_hal.c/h`, `solenoid_driver.c/h`) expose un contrat par
canal fondé sur les rôles logiques suivants, indépendants des noms de nets du
schéma :

| Rôle logique | Registre DRV8874 | Net schéma | Type GPIO RP2354B |
|---|---|---|---|
| `PWM_CHANNEL[n]` | pin1 EN/IN1 | `/PWMn` | Sortie PWM matérielle (`hardware_pwm`) |
| `DIR_CHANNEL[n]` | pin2 PH/IN2 | `/SENSn` (nom trompeur, ne pas reprendre tel quel dans le HAL) | Sortie GPIO simple (0/1) |
| `FAULT_CHANNEL[n]` | pin4 NFAULT | `/FAULTn` | Entrée GPIO, actif bas, pull-up |
| `GLOBAL_SLEEP` | pin3 NSLEEP (commun aux 12) | `/nSLEEP` | Sortie GPIO unique, partagée |
| `CURRENT_SENSE_MUX` | pin6 IPROPI (via IC17) | `/ADCn` -> `/MUX OUT` | 1 ADC + 4 lignes de sélection (`/S0-S3`, `/MUX_EN`) |

Règles de contrat :

- **DIR_CHANNEL ne doit jamais être basculé pendant que PWM_CHANNEL a un duty
  cycle non nul.** Toute transition de polarité (passage à `RELEASE`) doit
  d'abord ramener le duty cycle à 0 (ou couper le canal), attendre un court
  délai de sécurité, puis changer `DIR_CHANNEL`, avant de relancer le PWM.
  Ceci évite un pic de courant transitoire dans la bobine et protège le pont
  en H (cf. Invariant 4 d'`AGENTS.md` : limites électriques appliquées dans la
  couche pilote).
- `GLOBAL_SLEEP` doit être positionné actif (driver réveillé) avant toute
  activation de canal, et peut servir de coupure d'urgence matérielle
  commune aux 12 canaux.
- `CURRENT_SENSE_MUX` est un service **partagé et séquentiel** : une seule
  lecture de canal à la fois. Il n'est **pas** utilisable pour une régulation
  de courant temps réel par canal en Phase 2. Son seul usage prévu à ce jour
  est l'auto-test d'occupation au boot (Phase 4 de la Roadmap) et le
  diagnostic ponctuel de défaut.
- Le code du HAL (`pwm_hal.h`, `solenoid_driver.h`) doit nommer ses champs et
  fonctions selon les rôles logiques ci-dessus (`dir_channel`, `pwm_channel`,
  `fault_channel`, `global_sleep`), **jamais** en reprenant littéralement le
  nom de net `SENS` du pinmap généré, pour ne pas propager la confusion avec
  une mesure de courant.
- Le point ouvert de la Roadmap ("Auto-test IPROPI : durée, duty cycle et
  seuils de courant à mesurer sur prototype") reste inchangé et hors
  périmètre de cette ADR.

### Consequences

#### Positive
* **POS-001**: L'inversion de polarité nécessaire à l'état `RELEASE` est bien
  réalisable canal par canal (un GPIO `DIR_CHANNEL` dédié existe), ce qui
  confirme que la machine à états pull/hold/release de la Roadmap est
  implémentable telle que décrite, sans modification matérielle.
* **POS-002**: Le nommage `SENS` trompeur est neutralisé dès la conception du
  HAL, avant qu'un Codeur ne l'interprète à tort comme une mesure de courant
  par canal.
* **POS-003**: Le contrat clarifie explicitement que la lecture de courant
  est une ressource partagée séquentielle, évitant une tentative future de
  boucle de régulation de courant temps réel par canal qui serait
  électriquement impossible avec ce câblage.

#### Negative
* **NEG-001**: Le mux 74HC4067 + ADC unique interdit toute surveillance de
  courant simultanée sur les 12 canaux ; un défaut de courant en cours de jeu
  ne peut être détecté que via `NFAULT` (binaire, seuil interne du DRV8874),
  pas via une valeur de courant lue en continu.
* **NEG-002**: La règle « ne pas changer `DIR_CHANNEL` à duty non nul »
  ajoute un état transitoire supplémentaire dans la machine à états
  (coupure -> attente -> bascule -> relance), ce qui complexifie légèrement
  `solenoid_driver.c` par rapport à un simple changement de niveau immédiat.

### Alternatives Considered

#### Reprendre le nommage `SENS` tel quel dans le HAL
* **ALT-001**: **Description**: Nommer directement les champs du HAL
  `sens_channel[n]` en reprenant le nom du net généré automatiquement.
* **ALT-002**: **Rejection Reason**: Entretient la confusion avec une mesure
  de courant réelle par canal, alors qu'il s'agit électriquement de la
  direction (PH). Risque de bug de compréhension pour tout agent futur lisant
  le pinmap généré sans avoir cette ADR sous les yeux.

#### Bascule de direction sans coupure préalable du PWM
* **ALT-003**: **Description**: Changer `DIR_CHANNEL` à tout moment, y compris
  pendant un duty cycle non nul, pour simplifier la machine à états.
* **ALT-004**: **Rejection Reason**: Risque de pic de courant transitoire dans
  la bobine et de contrainte électrique non maîtrisée sur le pont en H,
  contraire à l'invariant n°4 d'`AGENTS.md` (limites électriques imposées par
  la couche pilote, pas laissées à l'appréciation de la couche métier).

### Implementation Notes

* **IMP-001**: Table de correspondance GPIO RP2354B (IC1) par rôle logique,
  issue de `docs/specs/pinmap.generated_drive.yaml` — à recopier dans
  `firmware/moteur/src/config.h`, jamais à redéfinir ailleurs :

  | Canal | PWM_CHANNEL (GPIO) | DIR_CHANNEL (GPIO, ex-"SENS") | FAULT_CHANNEL (GPIO) |
  |---|---|---|---|
  | 0 | GPIO35 | GPIO0 | GPIO39 |
  | 1 | GPIO1 | GPIO2 | GPIO37 |
  | 2 | GPIO5 | GPIO6 | GPIO4 |
  | 3 | GPIO7 | GPIO8 | GPIO3 |
  | 4 | GPIO11 | GPIO13 | GPIO12 |
  | 5 | GPIO14 | GPIO9 | GPIO10 |
  | 6 | GPIO20 | GPIO21 | GPIO17 |
  | 7 | GPIO18 | GPIO19 | GPIO16 |
  | 8 | GPIO26 | GPIO27 | GPIO25 |
  | 9 | GPIO23 | GPIO24 | GPIO22 |
  | 10 | GPIO32 | GPIO33 | GPIO31 |
  | 11 | GPIO29 | GPIO30 | GPIO28 |

  Signaux partagés : `GLOBAL_SLEEP` = GPIO15 (`/nSLEEP`), bouton de test =
  GPIO34 (`/Bouton test`). Mux courant (hors périmètre Phase 2) : `/S0`=GPIO44,
  `/S1`=GPIO43, `/S2`=GPIO38, `/S3`=GPIO36, `/MUX_EN`=GPIO42,
  `/MUX OUT`=GPIO45 (ADC5).
* **IMP-002**: `NFAULT` est open-drain actif bas côté DRV8874 ; vérifier sur
  le schéma/datasheet si une résistance de pull-up externe est présente sur
  chaque net `/FAULTn`, sinon activer le pull-up interne du GPIO RP2354B en
  entrée (`gpio_pull_up`).
* **IMP-003**: Pour le tout premier test isolé (Phase 2a, canal unique, duty
  30%), fixer `DIR_CHANNEL[0]` (GPIO0) à un niveau constant documenté dans le
  code (ex. `LOW = PULL`) avant de démarrer le PWM, et activer `GLOBAL_SLEEP`
  (GPIO15) au tout début du programme — sans ces deux prérequis, le driver
  reste en veille ou en état indéfini.
* **IMP-004**: Le signal `/FREQ OSC` (GPIO41_ADC1) reste non documenté à ce
  stade — ne pas l'utiliser dans le HAL solénoïde tant que sa fonction n'est
  pas confirmée avec l'auteur du schéma.

### References
* **REF-001**: `docs/specs/pinmap.generated_drive.yaml` (généré depuis
  `Netlist/Solenoide_drive.xml`)
* **REF-002**: Datasheet DRV8874, section "PH/EN Interface" —
  `Électrique/Moteur/Datasheet/drv8874_H-BRIDGE_DRIVER.pdf`
* **REF-003**: `docs/specs/Roadmap.md`, Phase 2a et Phase 4 (auto-test IPROPI)
