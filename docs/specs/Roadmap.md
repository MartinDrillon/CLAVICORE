
# Roadmap — Firmware Clavicore

Architecture logicielle et plan d'implémentation du firmware C des deux
familles de cartes (« Clavier » et « Moteur »). Ce document sert de référence
vivante : à mettre à jour au fil de l'avancement (cocher les cases, ajuster les
constantes, noter les décisions prises en cours de route).

## Décisions d'architecture actées

| Sujet | Décision |
|---|---|
| Framework | Pico-SDK bare-metal (super-boucle + IRQ/timers), pas de RTOS |
| Bus CAN | Pas de contrôleur CAN natif sur RP2350 (Clavier) / RP2354B (Moteur) (TCAN332DR = transceiver seul) → obligatoire d'utiliser [`can2040`](https://github.com/KevinOConnor/can2040) (bit-banging CAN via PIO). Compatibilité RP2350 confirmée (`#ifdef PICO_RP2350` présent dans `src/can2040.c`) |
| Firmware Moteur | Un firmware strictement identique est compilé et flashé sur les 5 cartes. Aucune identité ni aucun mapping musical n'est choisi à la compilation |
| Identité des 5 cartes Moteur | UID matériel 64 bits unique du RP2350, lu par une commande spéciale. Lien entre position dans la ligne CAN >< UID conservée dans un fichier. Aucun strap GPIO ni résistance d'identification n'est nécessaire. L'UID ne change pas entre deux mises sous tension |
| Commissioning | La carte Clavier conserve une table modifiable et persistante `UID -> position physique -> canal -> touche`. Elle est mise à jour par commande de configuration USB série ou CAN, sans recompiler le firmware | 
| Adressage CAN | Les commandes opérationnelles sont adressées localement avec l'UID destinataire et le canal (0-11) ; le numéro de note MIDI reste côté Clavier. Le Clavier traduit note -> UID + canal à partir de la table de commissioning |
| Détection branchement CAN (Clavier) | Aucun détecteur hardware dédié trouvé sur le schéma → le Clavier envoie toujours en CAN (sans coût significatif si rien n'écoute). Le MIDI USB est conditionné par `tud_mounted()` (détection USB standard via TinyUSB) |
/ LEDs RGB | Reportés — hors périmètre de cette première roadmap |
| Outillage | Extension VS Code officielle « Raspberry Pi Pico » (installe SDK, toolchain arm-none-eabi, CMake, Ninja, OpenOCD) |
| Emplacement du code | Dossier `firmware/` à la racine du repo (protocole CAN partagé dans `shared/protocol/`) |

## Faits techniques extraits du matériel existant

- **Carte Clavier_drive_1** : module RaspberryPi_Pico_SMD (Pico 2 /
  RP2350), transceiver CAN TCAN332DR (IC12), ~80 switches mécaniques, isolation USB externe (ADUM3160/4160, dans le câble, pas soudée sur la carte) → USB natif du Pico exploitable pour USB-MIDI via TinyUSB (déjà fourni par le Pico-SDK).
  GPIO disponibles dans `pinmap.generated.yaml`
- **Carte Solenoide_drive (Moteur)** : RP2354B (QFN80, IC1), TCAN332DR (IC12),
  12× DRV8874PWPR (IC4-IC11, IC13-IC16) — 9 ou 10 utilisés selon la carte (5 cartes pour couvrir 49 touches). Brochage DRV8874 : pin1=EN/IN1, pin2=PH/IN2, pin8/10=OUT1/OUT2, pin3=nSLEEP, pin4=nFAULT. Chaque driver fournit aussi une mesure de courant IPROPI.
- ⚠️ **Non extrait, bloquant avant le codage bas niveau** : les GPIO exacts seront extrait dans du fichier kicad dans un second temps. 

## Architecture logicielle

```
firmware/                       # deux projets CMake indépendants (pas de CMakeLists racine)
├── common/
│   └── can2040/                # sous-module git KevinOConnor/can2040, tag v1.7.0 (ADR-0002)
├── clavier/                    # une seule cible (un seul Pico 2 / RP2350)
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.c              # scan → keymap → commissioning (prévu) → CAN → MIDI
│       ├── matrix_scan.c/h     # scan GPIO + anti-rebond
│       ├── keycode.c/h         # keycodes (notes, comportements, KEYCODE_GET_UID…)
│       ├── keymap.c/h          # table position matrice → keycode, facile à éditer
│       ├── note_state.c/h      # état des notes
│       ├── midi.c/h            # device USB-MIDI via TinyUSB
│       ├── diag_log.c/h        # formatage des lignes de diagnostic CDC (ADR-0005)
│       ├── can_link.c/h        # wrapper can2040 (init PIO/IRQ, UID_REQUEST, réception)
│       ├── uid_position.h      # table d'amorçage UID -> position (ADR-0003 §7)
│       ├── usb_log.c/h         # journal texte USB CDC non bloquant
│       ├── usb_descriptors.c, tusb_config.h
│       ├── hal/pinmap.generated.h   # broches (généré depuis la netlist)
│       ├── commissioning.c/h   # (prévu, Phase 4) table persistante UID -> canal -> touche + validation boot
│       └── config_service.c/h  # (prévu, Phase 4) commandes USB série/CAN de lecture/écriture de la table
└── moteur/                     # une cible, même binaire sur les 5 cartes (RP2354B)
    ├── CMakeLists.txt, boards/moteur_rp2354b.h
    └── src/
        ├── main.c              # init UID, CAN et PWM (auto-test IPROPI : prévu)
        ├── can_link.c/h        # annonce boot + réponse à UID_REQUEST (commandes ciblées UID/canal : prévu)
        ├── board_identity.c/h  # lecture UID 64 bits
        ├── solenoid_driver.c/h # machine à états non-bloquante par canal
        ├── pwm_hal.c/h         # abstraction au-dessus de hardware_pwm
        ├── solenoid_ohm_constants.h    # constantes de mesure IPROPI (carte, ADR-0009)
        ├── solenoid_profile_defaults.h # valeurs de repli du profil (jamais un point de réglage)
        ├── solenoid_profile_store.c/h  # persistance flash A/B du profil (ADR-0013)
        ├── clock_test.c/h      # clock-test-mode1 (diagnostic, désactivable par option CMake)
        ├── config.h            # rôles logiques des broches (ADR-0001)
        └── occupancy_test.c/h  # (prévu, Phase 4) auto-test IPROPI, bitmap d'occupation 12 bits

shared/protocol/                # protocole CAN partagé (AGENTS.md, ADR-0003)
├── can_protocol.h/.c           # constantes, IDs, encodage/décodage (C pur, sans SDK)
├── solenoid_profile.h/.c       # profil d'activation : format, sérialisation, CRC32 (ADR-0013)
└── uid_position_format.h       # format de la table UID -> position
```

> `shared/protocol/` est à la racine du dépôt, pas dans `firmware/common/`
> (qui ne contient que `can2040/`). Chaque projet inclut le SDK via son propre
> `CMakeLists.txt` (pas de `pico_sdk_import.cmake` local).

## Roadmap détaillée

 
### Tâche préparatoire
- [ ] 
- [x] Décider du vendoring de `can2040` (submodule git vs CMake FetchContent)
      et l'ajouter au repo — fait : sous-module `firmware/common/can2040` (v1.7.0), ADR-0002

### Phase 1 : Base du clavier >> FAIT ! 

Étape à réaliser dans l’ordre, avec demande de validation à chaque étape, et demande d’information en cas de manque ou d’incompréhension. L’objectif de cette première phase est d’arriver à un clavier parfaitement fonctionnel en midi, flashable par usb, sans gestion CAN. 
Forte inspiration de la structure des projets QMK/ZMK, mais adapté à la musique. 

- [ ] `matrix_scan.c/h` avec anti-rebond selon les GPIO. Premier flash, vérification par moi à l’oscilloscope pour la validation de la chaine de complilation. (`clavier_keymap.json`)

- [ ] `keycode.c/h` : Assignation de "keycode" correspondant à une liste de comportement musicaux (inspiration QMK - ZMK, adapté à la musique, voir `LAYOUT DE LA MATRICE.txt`)

- [ ] `keymap.c/h` : table position matrice → keycode, éditable facilement, indentation du fichier = position physique réelle, inspiration keymap ZMK-QMK 

- [ ] `midi.c/h` : gestion de l’envoi midi usb en mode device > Intégrer TinyUSB device MIDI (déjà fourni par le Pico-SDK)


### Phase 2 : Validation du fonctionnement de la carte de drive
 
#### a. Moteur : bring-up RP2354b - bring-up PWM/DRV8874 isolé - pas de CAN
- [X] Implémenter "clock-test-mode1". La fonction ne servira que lors des tests: elle doit pouvoir être désactivé facilement
- [X] Test fréquence 
- [X] Exemple minimal `hardware_pwm` : canal unique (PWM0) déclenchement de test via bouton de test. Objectifs : validation électrique basique, sens du courant, PWM. 
- [X] Écrire `solenoid_driver.c/h` + `solenoid_profile.h` (3 états : pull/hold/release). Constantes avec modification aisée pour faciliter les ajustements empiriques qui seront faits pendant les tests.
Toutes les constantes (durées de pull-in/hold/release, valeurs de duty cycle) vivaient dans `solenoid_profile.h`, séparées du code de la machine à états. **Depuis ADR-0013/ADR-0014, le point de réglage unique est côté Clavier** (`firmware/clavier/src/solenoid_profile_config.h`) ; le Moteur ne garde que des valeurs de repli et la persistance flash.
```
IDLE --(note on)--> PULL (duty haut, très court)
                      --> HOLD (duty bas, jusqu'à note off)
IDLE <--(fin timer)-- RELEASE (polarité inversée, duty moyen, court) <--(note off)-- HOLD
```

### Phase 3a : Bring-up CAN > Voir [CAN.md](CAN.md) et ADR-0005
Premier test de communication entre la carte clavier et une unique carte moteur
 Décisions de bring-up et d'affichage diagnostic : ADR-0005
 (avec historique CAN dans ADR-0003). Procédure de test :
 `tests/can_phase3_uid.md`. **État : validée sur bus réel** (premier UID relevé :
 `A3FA4C99CA526F4B`, voir addenda ADR-0003).
 - [x] Valider PIO/IRQ can2040 sur RP2350 (Clavier) ET sur RP2354B (Moteur) — validé en Phase 3a
 - [x] Décider du vendoring de `can2040` : fait (sous-module git, ADR-0002)
- [x] Écrire `can_protocol.h` (`shared/protocol/can_protocol.h` + `.c`). Première fonction à programmer, en bring-up : lorsqu’un keycode spécial (`GET_UID`) assigné à une touche de la matrice du clavier est joué, les cartes RP2354 présentes sur le nœud renvoient leur UID — code écrit (`KEYCODE_GET_UID`, `can_link.c` Clavier/Moteur, ligne `[UID]` sur CDC) et validé sur bus réel

### Phase 3b — Auto-test IPROPI, table flash et commande solénoïde

Décisions figées par [ADR-0004](../adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) :
- **sens musical définitif** (notation scientifique, [ADR-0008](../adr/adr-0008-convention-nommage-octaves-notation-scientifique.md)) : touche 1 = **DO2** = MIDI 36 ; touche 49 =
  **DO6** = MIDI 84 ;
- **règle d'assignation ascendante** ([ADR-0010](../adr/adr-0010-ancrage-grave-mapping-commissioning.md),
  remplace ADR-0004 §6) : l'ancrage se fait sur la **touche 1 = DO2** (position 0,
  canal occupé le plus bas) et les blocs montent dans l'ordre des canaux puis des
  positions ;
- **exemple de carte unique** : 3 canaux occupés sur une seule carte donnent
  les touches **1, 2, 3** donc MIDI **36, 37, 38** ;
- **déclenchement local** : bouton Moteur maintenu **3 s** ;
- **pas d'auto-test au boot** ;
- **table persistante en flash côté Clavier**, cliché du dernier auto-test
  **demandé** ([ADR-0009](../adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)) :
  écrite même si le test est partiel, avec un état `FINAL`/`PROVISIONAL` et ses
  motifs signalés au journal.

#### Livraison 1 — Auto-test observable et remontée d'état

- [ ] Moteur : ajouter le profil `PROBE` (20 % PWM, durée bornée par le profil)
      dans `solenoid_driver.c`
- [ ] Moteur : lire les 12 IPROPI via le mux 74HC4067 (`ADC5`, GPIO45) avec la
      table de mapping canal PWM → entrée mux définie par ADR-0004
- [ ] Moteur : produire `bitmap_occupation` + `fault_bits`, puis émettre
      `BOARD_ANNOUNCE_STATUS` (`0x412`, trame C)
- [ ] Moteur : sur bouton local maintenu 3 s, émettre
      `IPROPI_SELFTEST_TRIGGER` (`0x402`) vers le Clavier
- [ ] Clavier : refuser le self-test pendant le jeu, sinon diffuser
      `IPROPI_SELFTEST_REQUEST` (`0x401`) avec `request_id`
- [ ] Clavier : recevoir les trames A+B+C, gérer le timeout, et afficher sur
      le journal CDC texte le bitmap et les STATUS `0x20+`
- [ ] Validation manuelle de la livraison 1 :
      `tests/ipropi_selftest_phase3b.md`

#### Livraison 2 — Commissioning persistant et jeu

- [x] Clavier : stocker en flash la table de commissioning
      (`uid_position_entry_t` en préfixe + bitmap 12 bits + CRC32)
- [x] Clavier : écrire la table après **tout** auto-test demandé dès qu'**une**
      carte a fourni un jeu complet (A+B+C), même partiel, avec état
      `FINAL`/`PROVISIONAL` et motifs
      ([ADR-0009](../adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) §2 :
      remplace la règle « toutes les cartes attendues ont répondu » ;
      aucune écriture au boot)
- [x] Clavier : au boot, charger la table flash puis comparer seulement les UID
      présents ; **ne jamais lancer d'auto-test au boot**
- [ ] Clavier : appliquer la règle d'assignation **ascendante** depuis la
      touche 1 (`DO2`), positions parcourues de 0 à 4
      ([ADR-0010](../adr/adr-0010-ancrage-grave-mapping-commissioning.md)) —
      remplace l'ancrage descendant sur la touche 49 (ADR-0004 §6)
- [x] Clavier : résoudre note MIDI -> (UID, canal) avant chaque commande
      `note on/off`
- [x] Clavier + Moteur : implémenter `SOLENOID_COMMAND` (`0x100`, DLC 5)
- [x] Clavier + Moteur : ajouter le mode maintenance push-only (`command = 2`,
      une seule touche, pas de MIDI, timeout 10 s)
- [ ] Test bout-en-bout réel : touche pressée sur Clavier -> solénoïde activé
      sur la bonne carte Moteur (procédure écrite ; validation matérielle à faire)

#### Bring-up — commissioning provisoire et tri des bobines (ADR-0009)

Implémenté et **validé en compilation + tests hôte le 2026-10-21** (221
vérifications, 0 échec) ; la campagne **de banc** reste à faire
([`tests/bringup_commissioning_provisoire.md`](../tests/bringup_commissioning_provisoire.md)).

- [x] `shared/protocol` : format de table **v2** (en-tête 16 o, `state`,
      `flags`) + lecture d'une v1 interprétée `FINAL`
- [x] `shared/protocol` : trame `IPROPI_OHM` (`0x601`, DLC 8) avec `ohm_milliohm`
      et `i100_mA`
- [x] Moteur : PROBE **5 ms** avec fenêtre d'échantillonnage en fin d'impulsion
      (plafond `probe_max_us` du profil, 10 ms par défaut)
- [x] Moteur : estimation `ohm` / `i100` à partir de `delta`, du duty appliqué et
      des constantes de `solenoid_ohm_constants.h`
- [x] Profil d'activation centralisé côté Clavier (ADR-0013/ADR-0014) : fichier
      unique `firmware/clavier/src/solenoid_profile_config.h`, transfert
      `0x420`–`0x423` à l'auto-test, persistance flash A/B côté Moteur,
      enveloppe thermique supprimée (dérogation à l'invariant 4)
- [x] Clavier : écrire la table dès qu'**une** carte complète a répondu, calculer
      les motifs `flags`, émettre `[TABLE]`, `[MAP]`, `[SELFTEST_RAW] ohm_est/i100`
      et `[SELFTEST_OHM]`
- [x] Clavier : motif `0x40 ANCHOR_SHIFT` quand les positions présentes ne sont
      pas contiguës depuis 0 (ADR-0010) ; vecteurs de test hôte à suivre
      (`docs/specs/impact-adr-0010.md`)
- [x] Clavier : `CLAVIER_SELFTEST_CALIBRATION` à `ON` pendant le bring-up
      (retour à `OFF` en configuration finale)
- [x] Clavier : console CDC de bring-up (`t`, `c`, `d`, `m`, `?`)
- [ ] Validation manuelle : `tests/bringup_commissioning_provisoire.md`
- [ ] Recoupement d'`ohm_est` avec un multimètre sur une bobine de référence, et
      tri des bobines (dispersion, fenêtre 2–3 A à 100 % de duty)
- [ ] Vérification que l'impulsion PROBE 5 ms / 20 % n'attire pas le sautereau
- [ ] Non-régression du motif `ANCHOR_SHIFT` et des lignes `[MAP]` sur banc
      partiel (calcul non couvert par les tests hôte, il vit dans `can_link.c`)


### Phase 4 — Chainage 

- [ en cours ] Écrire `uid_position.h` (`firmware/clavier/src/uid_position.h`, format dans `shared/protocol/uid_position_format.h`), dans lequel la position de chaque cartes dans le nœud can est renseignée manuellement grâce à son UID — fichier présent mais un seul UID présent, en attente du relevé des autres UID
  - [ en cours ] Relever les 5 UID via PuTTY (procédure `tests/can_phase3_uid.md`)
  - [ ] Remplir `uid_position.h` avec les UID relevés


### Phase 5 — Clavier : USB-MIDI-extension
- [ ] Détection host via `tud_mounted()`, envoi MIDI seulement si monté,
      toujours après l'envoi CAN (priorité demandée)
- [ ] Test avec un moniteur MIDI / DAW

### Phase 6 — Intégration complète & validation performance
- [ ] Câblage réel des 49 touches sur les 5 cartes Moteur, vérification de
      chaque association UID, canal et touche
- [ ] Mesure de latence bout-en-bout (appui touche → activation solénoïde),
      vérifier absence de gigue/rebond audible
- [ ] Session de réglage empirique des constantes dans
      `firmware/clavier/src/solenoid_profile_config.h` (puis flash Clavier +
      auto-test pour propager, ADR-0013)

### Reporté (hors périmètre actuel)
- [ ] LEDs RGB d'indication

## Commissioning 

1. Mise sous tension d’une carte à la fois.
2. Exécuter le relevé d’UID Phase 3a (`GET_UID` / annonce de boot), puis noter
   l’UID et la position physique de la carte.
3. Remplir `uid_position.h` avec les 5 UID et leur ordre dans le nœud CAN : la
   carte à l'extrémité **grave** (celle qui porte `DO2`) doit être déclarée en
   **position 0** ([ADR-0010](../adr/adr-0010-ancrage-grave-mapping-commissioning.md)).
4. Une fois toutes les cartes en place, lancer l’auto-test IPROPI **sur demande
   uniquement** (bouton Moteur 3 s -> `IPROPI_SELFTEST_TRIGGER` `0x402` ->
   diffusion Clavier `IPROPI_SELFTEST_REQUEST` `0x401`).
5. Chaque carte renvoie `BOARD_ANNOUNCE_STATUS` (`0x412`) avec `uid_tag`,
   `bitmap_occupation`, `reply_to`, `test_status`, `fault_bits`.
6. Dès qu’**une** carte répondante a fourni un jeu complet (A+B+C), le Clavier
   écrit la table persistante en flash : c’est le **cliché du dernier test
   demandé** ([ADR-0009](../adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)).
   L’état est `FINAL` si toutes les positions attendues ont répondu sans échec
   avec `total_occupied = 49` **et** des positions présentes contiguës depuis 0,
   sinon `PROVISIONAL` avec ses motifs (`MISSING_BOARD`, `UNKNOWN_BOARD`,
   `BOARD_NOT_OK`, `FAULT_DURING_TEST`, `NOT_49_KEYS`, `ANCHOR_SHIFT`).
7. En cas de divergence, d’absence de carte, de collision de tag ou de table
   partielle, le Clavier signale explicitement l’erreur et ne remappe rien
   silencieusement ; une carte présente mais absente de la table a ses canaux
   refusés.
8. Procédure détaillée de mise en service progressive (chaîne complète -> carte
   seule sous charge -> installation carte par carte) :
   [`tests/bringup_commissioning_provisoire.md`](../tests/bringup_commissioning_provisoire.md).

## Vérification

- Chaque phase possède un test isolé avant intégration (voir sous-étapes ci-dessus).
- `cmake --build` doit réussir pour la cible Clavier et la cible Moteur unique.
- Latence appui-touche → activation solénoïde mesurée en Phase 6 (oscilloscope
  ou analyseur logique si disponible : appui touche → front CAN → front PWM).

### Protocole CAN à spécifier

> ADR-0003 a figé le bring-up UID (`0x400`, `0x410`, `0x411`) et ADR-0004 a
> ajouté l'auto-test IPROPI et la commande de jeu (`0x401`, `0x402`, `0x412`,
> `0x100`). [ADR-0009](../adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)
> ajoute le diagnostic `IPROPI_OHM` (`0x601`). Voir [CAN.md](CAN.md) pour les
> champs détaillés et la règle d'assignation.
`shared/protocol/can_protocol.h` définit (définira)  des messages versionnés, de taille fixe et sans
allocation dynamique :

| Message | Émetteur | Contenu minimal | But |
|---|---|---|---|
| `UID_REQUEST` (`0x400`) | Clavier | version, `request_id` | Demander les annonces UID |
| `BOARD_ANNOUNCE` (`0x410` + `0x411`) | Moteur | A : UID 64 bits ; B : `uid_tag`, version proto, version firmware, `reply_to` | Déclarer une carte au boot ou sur `UID_REQUEST` |
| `IPROPI_SELFTEST_TRIGGER` (`0x402`) | Moteur | version, réservé = 0 | Demander au Clavier d'orchestrer un auto-test |
| `IPROPI_SELFTEST_REQUEST` (`0x401`) | Clavier | version, `request_id` | Lancer l'auto-test IPROPI sur toutes les cartes |
| `BOARD_ANNOUNCE_STATUS` (`0x412`) | Moteur | `uid_tag`, bitmap 12 bits, `reply_to`, `test_status`, `fault_bits` | Retour d'auto-test IPROPI |
| `IPROPI_RAW` (`0x600`) | Moteur | `uid_tag`, `reply_to`, canal, `idle_avg`, `probe_avg` | Valeurs ADC brutes (mode diagnostic) |
| `IPROPI_OHM` (`0x601`) | Moteur | `uid_tag`, `reply_to`, canal, `ohm_milliohm`, `i100_mA` | Résistance et courant estimés d'une bobine (mode diagnostic) |
| `BOARD_ASSIGN` | Clavier | UID 64 bits, identifiant de session | Confirmer que le Clavier reconnaît la carte |
| `SOLENOID_COMMAND` (`0x100`) | Clavier | `uid_tag`, canal 0-11, commande note on/off, vélocité | Commander un driver sans mapping musical embarqué |
| `CONFIG_READ` / `CONFIG_WRITE` | Hôte ou Clavier | version, enregistrement de commissioning, contrôle d'intégrité | Modifier la table persistante sans recompilation |
| `FAULT_REPORT` | Moteur ou Clavier | UID, canal, code d'erreur | Signaler un défaut localisé |

Après `BOARD_ASSIGN`, le Clavier peut attribuer à chaque UID une adresse courte
de session pour réduire les trames de jeu. L'UID reste obligatoire pour les
annonces, diagnostics et commandes de configuration ; la carte ignore toute
commande de session qui ne lui est pas attribuée.

## Points ouverts (non bloquants pour démarrer)

- **Vendoring de `can2040`** : décidé dans [ADR-0002](adr/adr-0002-integration-can2040.md)
  (sous-module git, tag v1.7.0) — détail dans [CAN.md](CAN.md).
- **Format binaire du protocole CAN** : partiellement tranché par
      ADR-0003 (historique CAN) et
      ADR-0005,
      complétés par
      [ADR-0004](../adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md)
      (IDs 11 bits, little-endian, DLC strict, `UID_REQUEST`, `BOARD_ANNOUNCE`
      A/B/C, `IPROPI_SELFTEST_TRIGGER`, `IPROPI_SELFTEST_REQUEST`,
      `SOLENOID_COMMAND`). Restent ouverts : `FAULT_REPORT`,
      `CONFIG_READ`/`CONFIG_WRITE`, `BOARD_ASSIGN`, accusés de réception
      applicatifs et payload détaillé des lignes `[STATUS]` `0x20+`.
- **Auto-test IPROPI** : le profil PROBE est **figé** à 5 ms / 20 %
      ([ADR-0009](../adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)),
      le seuil d'occupation à `delta >= 400` (ADR-0004, addendum 4), et
      `SELFTEST_UNCALIBRATED` n'est plus émis. Restent à caractériser au banc :
      la cohérence de `ohm_est` avec une mesure au multimètre et le tri des
      bobines (fenêtre 2–3 A à 100 % de duty).
