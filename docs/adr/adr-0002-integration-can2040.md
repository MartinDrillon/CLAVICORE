---
title: "ADR-0002: Intégration de can2040 — vendoring, PIO/IRQ, GPIO, contrat callback"
status: "Proposed"
date: "2026-09-27"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol"]
supersedes: ""
superseded_by: ""
---

### Status
**Proposed**

Dérogation à signaler — validation humaine requise sur un point : le choix de
vendorer du code sous licence **GNU GPLv3** (`can2040`) alors qu'aucune
licence globale n'est actuellement fixée pour ce dépôt (`README.md` n'en
mentionne aucune, aucun fichier `LICENSE` trouvé à la racine). Cette ADR
suppose un usage personnel/hobby sans contrainte de distribution commerciale
fermée ; si ce projet devait un jour être distribué sous une licence
incompatible avec la GPLv3, ce choix devrait être révisé avant toute
publication du firmware compilé.

### Context

Le Roadmap (Phase 3) prévoit l'intégration de
[`can2040`](https://github.com/KevinOConnor/can2040) (bit-banging CAN via PIO)
sur les deux familles de cartes. Avant de coder quoi que ce soit, l'objectif
de cette ADR est de verrouiller les faits techniques vérifiés et les choix
d'intégration nécessaires, afin qu'une fois le feu vert donné par l'humain,
l'implémentation puisse démarrer sans blocage de dernière minute. Aucune ligne
de firmware CAN n'est écrite à ce stade.

Faits vérifiés sur can2040 (README, `docs/Features.md`, `docs/API.md`,
`docs/Tools.md`, `docs/Releases.md`, dépôt officiel, consultés le
2026-09-27) :

1. **Licence GNU GPLv3** (fichier `COPYING` du dépôt can2040). Voir la
   dérogation signalée ci-dessus.
2. **Dernière version taguée : v1.7.0 (2025-01-02)** — première version à
   supporter les puces rp2350 et à exiger pico-sdk >= 2.0.0. Notre SDK
   installé est la 2.3.1 (`docs/specs/build_clavier_windows.md`) : compatible.
3. Compilation : nécessite gcc, `-O2` minimum, et le define
   `-DPICO_RP2350` pour les puces de la famille RP2350 (notre Clavier =
   RP2350A/Pico 2, notre Moteur = RP2354B/RP2350) — **les deux cibles** ont
   donc besoin de ce define une fois can2040 intégré.
4. **Une instance de can2040 occupe un bloc PIO entier.** Le RP2040 a 2 blocs
   PIO (donc 2 bus CAN max par puce), le RP2350 en a 3. Aucune des deux
   cartes n'utilise actuellement de PIO pour autre chose (vérifié dans
   `firmware/clavier/src` et `firmware/moteur/src`) : `pio_num = 0` est donc
   libre sur les deux cartes.
5. **Contrainte GPIO sur RP2350** : `gpio_rx`/`gpio_tx` doivent être tous les
   deux dans la plage GPIO0-31, OU tous les deux dans la plage GPIO16-47 (pas
   de mélange des deux plages). Vérifié sur le matériel réel :
   - Clavier : RXD CAN = GPIO0, TXD CAN = GPIO1 (`docs/specs/pinmap.generated_clavier.yaml`)
     → tous deux dans 0-31, OK sur RP2040 et RP2350.
   - Moteur : `/RXD` = GPIO46, `/TXD` = GPIO47 (`docs/specs/pinmap.generated_drive.yaml`)
     → tous deux dans 16-47, OK sur RP2350/RP2354B.
6. **Transceiver TCAN332DR (IC12), identique sur les deux cartes** : pin1=TXD
   (entrée, depuis le GPIO TX), pin4=RXD (sortie, vers le GPIO RX), pin3=VCC,
   pin2=GND, pin6=CANL, pin7=CANH, pins 5 et 8 non connectées. **Pas de broche
   STB/enable** : le transceiver est toujours actif, aucune broche de
   sommeil/réveil à piloter côté HAL (contrairement au `nSLEEP` du DRV8874,
   voir ADR-0001).
7. **Résistance de terminaison 120 Ω présente mais non montée par défaut sur
   les deux cartes** : `R24` (valeur `R120`) posée en pont entre les nets
   `CAN+`/`CAN-` sur `Solenoide_drive` (Moteur) et sur `Clavier_drive_1`
   (Clavier), toutes deux marquées `dnp` (Do Not Populate) dans le netlist.
   Un bus CAN fonctionnel nécessite une terminaison 120 Ω à **chaque
   extrémité physique** du bus (`docs/Tools.md` de can2040) : il faudra
   souder `R24` uniquement sur les deux cartes aux extrémités de la chaîne
   physique (probablement le Clavier d'un côté, la dernière carte Moteur du
   chaînage de l'autre), une fois cette chaîne physiquement fixée.
8. **can2040 ne peut pas s'auto-valider seul** : `docs/Tools.md` précise
   qu'un bus CAN nécessite au minimum 2 nœuds avec transceiver et
   terminaison correcte pour produire le moindre trafic observable — un seul
   Pico ne permet aucun test CAN électrique isolé, même pour du débogage.
9. **Contrat callback** : `can2040_rx_cb` est invoqué en contexte IRQ (depuis
   `can2040_pio_irq_handler`). Le pointeur `msg` n'est valide que pendant
   l'appel. Il n'est pas interdit d'appeler `can2040_transmit()` depuis ce
   callback mais c'est déconseillé par la documentation officielle (surcoût
   CRC/bit-stuffing qui augmenterait la latence IRQ).
10. **Latence IRQ critique** : au-delà de ~80 temps-bit de latence IRQ, la
    FIFO PIO peut déborder et perdre des messages définitivement. Ce qui
    compte est la latence de prise en charge de l'IRQ (IRQ désactivées trop
    longtemps, ou IRQ de priorité égale/supérieure qui monopolise le cœur) —
    **pas** un simple `sleep_ms()`/`sleep_us()` bloquant en boucle
    principale, qui n'empêche pas les IRQ de se déclencher sur RP2040/2350.
11. **Aucun IRQ ni PIO n'est actuellement configuré** dans
    `firmware/clavier/src/main.c` ni `firmware/moteur/src/main.c` : can2040
    sera le premier utilisateur des deux sur chaque carte, sans conflit
    prévisible avec l'existant (matrice clavier en GPIO simple, PWM
    solénoïde sans IRQ de wrap utilisé).
12. `can2040_transmit()` ne peut mettre en file que **4 messages** au
    maximum (`tx_queue[4]`, taille fixe non extensible).
13. Le mux 74HC4067 IPROPI (ADR-0001) et son unique broche ADC restent hors
    périmètre de cette ADR.

### Decision

1. **Vendoring** : sous-module git, épinglé au tag **`v1.7.0`**, à l'emplacement
   `firmware/common/can2040/` (conforme au schéma d'architecture déjà
   esquissé dans `docs/specs/Roadmap.md`). Un sous-module git offre une
   version reproductible et auditable sans dépendre du réseau au moment de
   la configuration CMake (contrairement à `FetchContent`), ce qui est
   cohérent avec la procédure de build Windows déjà documentée
   (`docs/specs/build_clavier_windows.md`). Le fichier `COPYING` (GPLv3) du
   sous-module doit être conservé tel quel.
2. **Bloc PIO** : `pio_num = 0` sur les deux cartes (aucun conflit, premier
   bloc disponible sur les deux familles de puces).
3. **GPIO** : imposés par le matériel existant, non négociables côté
   logiciel — Clavier RX=GPIO0/TX=GPIO1, Moteur RX=GPIO46/TX=GPIO47 (voir
   point 5 du contexte). Aucune modification matérielle requise.
4. **IRQ** : `PIO0_IRQ_0` enregistré en gestionnaire exclusif sur les deux
   cartes, avec la priorité la plus haute disponible côté application (pas
   de code applicatif à priorité IRQ égale ou supérieure sur ces deux
   firmwares aujourd'hui — à revérifier si une future fonctionnalité ajoute
   un autre gestionnaire d'IRQ).
5. **Contrat du callback `can2040_rx_cb`** : il doit se limiter à copier le
   `struct can2040_msg` reçu dans un tampon circulaire simple
   producteur-unique/consommateur-unique (écrit depuis l'IRQ, lu depuis la
   boucle principale), puis retourner immédiatement. Tout parsing et
   dispatch du protocole (`can_protocol.h`, à écrire en Phase 3) se fait
   exclusivement en boucle principale, jamais dans le callback. Aucun appel
   à `can2040_transmit()` ne doit être fait depuis ce callback.
6. **Débit CAN cible proposé (à confirmer empiriquement en Phase 3)** :
   500 kbit/s, valeur intermédiaire raisonnable pour un bus multi-drop à 6
   nœuds sur quelques mètres de câble à l'intérieur du corps de
   l'instrument. Doit rester une constante nommée, jamais une valeur magique
   recopiée à plusieurs endroits.
7. **Terminaison physique** : `R24`/`R120` doit être soudée uniquement sur
   les deux cartes aux extrémités physiques du chaînage CAN, une fois cet
   ordre physique figé (voir section "Commissioning" du Roadmap).

### Consequences

#### Positive
* **POS-001**: Les GPIO, le bloc PIO et le device matériel (transceiver, pas
  de broche STB) sont déjà entièrement caractérisés ; le Codeur n'aura
  aucune ambiguïté matérielle à trancher lui-même en Phase 3.
* **POS-002**: Le contrat callback (copie + tampon circulaire, jamais de
  traitement lourd en IRQ) est fixé avant qu'un Codeur ne soit tenté
  d'appeler `can2040_transmit()` ou du parsing complexe directement dans
  `can2040_rx_cb`, ce qui éviterait des bugs de latence difficiles à
  diagnostiquer a posteriori.
* **POS-003**: L'épinglage à `v1.7.0` (premier support RP2350 officiel) rend
  le build reproductible et évite une régression accidentelle si le dépôt
  amont évolue.

#### Negative
* **NEG-001**: La licence GPLv3 du sous-module engage la question de la
  licence globale du firmware si celui-ci devait un jour être distribué
  (point de vigilance signalé en tête d'ADR, non résolu ici).
* **NEG-002**: Un sous-module git ajoute une étape (`git submodule update
  --init --recursive`) à documenter dans les procédures de build Windows
  existantes et à venir, sous peine d'échec de configuration CMake pour
  quiconque clone le dépôt sans cette étape.
* **NEG-003**: Le bus CAN ne pourra être testé électriquement qu'avec au
  moins deux cartes physiquement reliées et correctement terminées ; aucun
  test CAN isolé sur une seule carte n'est possible (voir point 8 du
  contexte), ce qui contraint l'ordre des prochaines étapes de validation.

### Alternatives Considered

#### CMake FetchContent au lieu d'un sous-module git
* **ALT-001**: **Description**: Télécharger can2040 automatiquement pendant
  la configuration CMake via `FetchContent_Declare`/`FetchContent_MakeAvailable`.
* **ALT-002**: **Rejection Reason**: Nécessite un accès réseau à chaque
  configuration CMake à froid, moins reproductible et plus difficile à
  auditer (version exacte moins visible dans le dépôt) que le hash figé d'un
  sous-module ; moins cohérent avec la procédure de build Windows déjà
  validée et volontairement hors-ligne pour la cible `clavier`.

#### Bloc PIO 1 ou 2 au lieu de 0
* **ALT-003**: **Description**: Réserver `PIO0` pour un usage futur non
  identifié et démarrer can2040 sur `PIO1`.
* **ALT-004**: **Rejection Reason**: Aucun usage concurrent de `PIO0`
  n'existe aujourd'hui sur aucune des deux cartes ; réserver arbitrairement
  `PIO0` sans besoin identifié ajoute de la complexité sans bénéfice
  actuel. Rien n'empêche de revoir ce choix si un futur besoin (ex. un
  second bus CAN par carte, capacité documentée par can2040) apparaît.

### Implementation Notes

* **IMP-001**: Ajouter `-DPICO_RP2350` aux définitions de compilation des
  cibles `clavier` ET `moteur` dès que le sous-module `can2040` sera ajouté
  (aucune des deux cibles ne l'a actuellement, car aucune n'en a besoin sans
  can2040).
* **IMP-002**: Documenter `git submodule update --init --recursive` dans
  `docs/specs/build_clavier_windows.md` et dans l'équivalent futur pour
  `moteur`, avant la Phase 3.
* **IMP-003**: `can_link.c/h` (déjà prévu dans le schéma d'architecture du
  Roadmap) est l'emplacement prévu pour l'appel à `can2040_setup`,
  `can2040_callback_config`, l'enregistrement de l'IRQ et le tampon
  circulaire de réception — code très similaire entre Clavier et Moteur,
  mais dont la mutualisation exacte (ex. un HAL commun sous
  `shared/hal/can_link`) reste une question ouverte à trancher dans une
  future ADR une fois `can_protocol.h` écrit (Phase 3).
* **IMP-004**: Ne pas souder `R24` sur toutes les cartes par défaut : une
  fois l'ordre physique du chaînage CAN figé, ne souder la résistance 120 Ω
  que sur les deux cartes en bout de chaîne.
* **IMP-005**: Un analyseur logique USB bon marché (~15 €, ex. "clone
  Saleae") avec Sigrok/PulseView et son décodeur CAN intégré est l'outil de
  test recommandé par `docs/Tools.md` pour vérifier le bit-timing réel sur
  les broches TXD/RXD de chaque carte, y compris avant l'écriture de tout
  code can2040 (utile dès la phase de vérification actuelle des cartes
  Clavier/Moteur prise isolément).

### References
* **REF-001**: [docs/adr/adr-0001-hal-solenoide-drv8874-phen.md](../adr/adr-0001-hal-solenoide-drv8874-phen.md)
* **REF-002**: can2040 — https://github.com/KevinOConnor/can2040 (README, docs/Features.md, docs/API.md, docs/Tools.md, docs/Releases.md — consultés le 2026-09-27, version référencée v1.7.0)
* **REF-003**: Datasheet TCAN332DR — `Électrique/Moteur/Datasheet/tcan332.pdf` et `Électrique/Clavier/Datasheet/`
* **REF-004**: `docs/specs/pinmap.generated_clavier.yaml`, `docs/specs/pinmap.generated_drive.yaml`
* **REF-005**: `docs/specs/Roadmap.md`, section "Phase 3 : Bring-up CAN" et "Points ouverts"
