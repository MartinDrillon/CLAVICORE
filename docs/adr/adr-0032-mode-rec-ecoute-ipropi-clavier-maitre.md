---
title: "ADR-0032: Mode REC — écoute des touches du clavecin par IPROPI (clavecin utilisé comme clavier maître MIDI)"
status: "Accepted"
date: "2026-11-10"
authors: "Agent Maître"
tags: ["architecture", "firmware", "protocol", "hardware", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted** — validée par l'humain le 2026-11-10 (« implémente tout d'un coup »),
avec les réponses Q1–Q3, `settle_us = 15 µs` et l'option Serial Studio. La porte
de faisabilité G1 n'est plus un arrêt de la chaîne : c'est la première étape du
banc (`tests/lecture_test_roadmap.md`).

### Context

L'humain veut jouer directement sur le clavier du clavecin et obtenir les notes
en MIDI (enregistrement, ou pilotage d'un autre instrument). Quand une touche
bouge, l'aimant collé dessous se déplace dans la bobine du solénoïde et induit
une force électromotrice (FEM). La carte Moteur peut lire le courant qui en
résulte par la broche IPROPI de chaque DRV8874, déjà multiplexée (74HC4067,
`MOTEUR_MUX_*`, ADC5 sur GPIO45, ADR-0001) et déjà utilisée par l'auto-test.

**Ce que le matériel permet réellement :**

1. **Le pont est en frein au repos.** PH/EN, nSLEEP haut en permanence après
   tWAKE (ADR-0024), EN = 0 au repos → OUT1 = OUT2 = L : la bobine est
   court-circuitée par les deux MOSFET bas, la FEM fait circuler un courant
   qu'IPROPI voit. Aucun changement d'état électrique n'est nécessaire.
2. **IPROPI redresse le signal.** Seul le courant drain → source des MOSFET bas
   est compté ; en frein, exactement un des deux le porte, quel que soit le
   sens. IPROPI mesure **|i|** : appui et relâchement donnent une impulsion
   **positive**, le sens n'est pas lisible ; à l'inversion d'une trille, |i|
   repasse par zéro.
3. **Ordre de grandeur.** FEM 50 mV → i ≈ 50 mV / 7,54 Ω ≈ 6,6 mA →
   I_IPROPI ≈ 3,0 µA → V_IPROPI ≈ 7,1 mV ≈ **9 points d'ADC**. Bruit mesuré par
   l'humain : **1 à 2 points, 3 au maximum**, stable par canal. Rapport
   signal/bruit attendu ≈ 3–9 : détectable, à confirmer au banc.
4. **Inductance.** Le courant est la FEM filtrée par L/R : l'inductance
   **allonge et lisse** l'impulsion (elle ne l'écrête pas) ; le contenu
   fréquentiel utile est borné par 1/(2π·L/R). Si τ = L/R ≈ 1 ms, rien
   d'utile au-delà de quelques centaines de Hz ; un échantillonnage de
   4–5 kHz par canal donne plusieurs points par τ. La valeur réelle de τ est
   mesurée au banc (M2).
5. **Levier matériel non décidé ici.** Augmenter R_IPROPI augmente le signal,
   mais abaisse le seuil de régulation I_TRIP = V_REF / (A_IPROPI × R_IPROPI)
   (≈ 3,05 A si V_REF = 3,3 V) : décision de sécurité réservée à l'humain.
6. **Cœurs.** Le RP2354B a deux cœurs Cortex-M33 **et** deux cœurs RISC-V
   Hazard3 ; l'architecture est choisie au boot, et le firmware est compilé
   pour ARM. Le « Core 1 » utilisé ici est le **second Cortex-M33**, qui
   tourne en vrai parallèle du Core 0 ; il n'est utilisé par aucun code Moteur.
7. **Débit maximal.** Une conversion ADC dure 2 µs. Un canal coûte
   `settle_us` + 2 µs × `samples_per_dwell` + ≈ 1 µs. Avec `settle_us = 15` :
   N = 1 → 18 µs/canal → **≈ 4,6 kHz par canal** (12 canaux) ; N = 2 → ≈ 4,2 kHz ;
   N = 4 → ≈ 3,5 kHz. **Le temps d'établissement du mux est le facteur
   limitant** : à 3 µs, on atteindrait ≈ 13 kHz (N = 1). Pour l'étude fine
   d'une touche, la capture `trace` (§3) lit un seul canal sans commuter le mux,
   jusqu'à 50 kHz.

### Decision

#### 1. Acquisition sur la carte Moteur (Core 1 dédié)

- Une boucle sur **Core 1** (second Cortex-M33, `multicore_launch_core1`)
  balaie les 12 entrées du mux en tourniquet. Pour chaque canal : sélection du
  mux, attente `settle_us`, puis `samples_per_dwell` conversions, moyennées en
  un échantillon.
- `settle_us` défaut **15 µs** pour l'écoute. L'auto-test IPROPI **conserve
  150 µs** (`IPROPI_SELFTEST_MUX_SETTLE_US`, inchangé).
- Le Core 1 ne touche **ni au CAN, ni aux solénoïdes, ni aux journaux**. Il
  publie dans des anneaux à producteur unique / consommateur unique (indices
  `volatile`, barrières `__dmb()`), vidés par la boucle principale du Core 0.
  Débordement = compteur + drapeau dans `LISTEN_STATUS`, jamais silencieux.
- La période réelle d'un balayage (µs) est mesurée et publiée dans
  `LISTEN_STATUS` (fréquence effective par canal visible dans PuTTY).
- **Exclusion avec l'auto-test** (même mux, même ADC) : Clavier refuse
  `t`/bouton Moteur en REC et REC pendant un auto-test ; côté Moteur, une
  demande d'auto-test pendant l'écoute suspend le Core 1 par rendez-vous
  explicite (`LISTEN_STATUS state=SUSPENDED_SELFTEST`) ; l'auto-test est alors
  refusé `REFUSED_BUSY` tant que l'écoute n'est pas arrêtée.
- **Canal piloté** (hors IDLE, ne doit pas arriver en REC) : échantillons
  invalides jusqu'à `drive_blank_ms` après le retour en IDLE, compteur
  `samples_masked`.
- **nSLEEP bas ou FAULT** : `LISTEN_STATUS state=UNAVAILABLE_SLEEP` ou bit de
  défaut ; jamais d'écoute silencieusement morte.

#### 2. Détecteur d'impulsions, mémoire bornée

Module pur `firmware/moteur/src/pulse_detector.{h,c}` (aucun SDK, testable sur
PC), une instance par canal, **mémoire O(1)** :

- **Ligne de base** par canal : moyenne sur `baseline_init_ms` à l'entrée en
  écoute, puis moyenne exponentielle (`baseline_tau_ms`) mise à jour **en état
  calme seulement**.
- **Début** : échantillon > base + `rise_thr`. t0 = dernier échantillon
  ≤ base + `return_thr` avant ce dépassement.
- **Crête** : maximum et instant t_peak.
- **Fin** (premier atteint) : retour ≤ base + `return_thr` maintenu
  `return_hold_us` (0 = instantané), t_end = premier échantillon de ce
  maintien ; ou **coupure de trille** : après la crête, chute sous
  `split_pct` % de (crête − base) puis remontée de plus de `rise_thr`
  au-dessus du creux → fin au creux, nouvelle impulsion.
- **Filtre** : impulsion < `min_pulse_us` comptée (`pulses_rejected`), non
  rapportée.
- **Rapport** : base (« ADC min »), crête (« ADC max »), delta,
  t_peak − t0, t_end − t_peak, t_end − t0, drapeau trille.
- **Anneau de bruit** : par canal, min/max par tranche de 1 ms sur 256 ms
  (12 Ko), interrogé en « N dernières ms » (§4) sans synchronisation d'horloge.

#### 3. Capture `trace` et flux `stream` (outils de réglage)

- **`trace`** : un seul canal, le mux reste fixe, période `trace_period_us`
  (défaut 20 µs = 50 kHz, minimum 2), tampon **4096 échantillons** (8 Ko),
  pré-déclenchement 25 %, déclenchement sur base + `trace_trig`. Une seule
  carte en trace à la fois ; le balayage des autres canaux de cette carte est
  suspendu pendant la capture (signalé). Après capture : envoi au Clavier
  (`LISTEN_TRACE`, ≈ 0,3 s de bus), puis retour au balayage.
- **`stream`** (option Serial Studio uniquement) : flux continu d'un canal
  pendant le balayage normal, un échantillon sur `stream_decim` (défaut 1).
  Charge CAN ≈ 30 % du bus à 4,6 kHz : réservé au réglage.
- **PuTTY ne reçoit jamais la suite des valeurs** : seulement un résumé
  (`[TRACE] pos= ch= touche= base= pic= delta= bruit_cc= t_pic= ms n=4096`).

#### 4. Option de compilation Serial Studio

- Option CMake Clavier **`CLAVIER_SERIAL_STUDIO`** (défaut **OFF**). Activée,
  elle ajoute un **second port CDC** (`CFG_TUD_CDC 2`) : Windows n'autorise pas
  deux programmes sur le même port COM, donc PuTTY reste sur le port 1 (journal
  et console inchangés) et Serial Studio ouvre le port 2.
- Format sur le port 2 : trames Serial Studio en mode « Quick Plot »,
  `/*v1,v2,…*/\n` (CSV entre délimiteurs) :
  - `stream` : `/*brut,base,seuil_haut,seuil_retour,evt*/` par échantillon ;
    `evt` = delta à la fin d'une impulsion, 0 sinon ;
  - `trace` : `/*index,brut*/` pour les 4096 points, précédé d'une ligne de
    séparation `/*-1,0*/`.
- Fichier de projet Serial Studio fourni : `tools/serial_studio/clavicore_rec.json`
  (graphiques nommés). Option OFF : aucun code ni descripteur USB ajouté,
  `e stream` répond `indisponible (CLAVIER_SERIAL_STUDIO=OFF)`, `e trace`
  donne son résumé dans PuTTY.

#### 5. Paramètres réglables à chaud

- Fichier unique côté Clavier : **`firmware/clavier/src/listen_config.h`**
  (`k_listen_config`, modèle `solenoid_profile_config.h`).
- Format, identifiants, unités, contrôles structurels :
  **`shared/protocol/listen_params.h/.c`** (`listen_params_validate()`, rejet en
  bloc, jamais d'écrêtage ; aucun paramètre n'est une limite électrique).
- Champs et défauts de départ :

| Champ | Défaut | Rôle |
|---|---|---|
| `settle_us` | 15 | attente après commutation du mux (écoute) |
| `samples_per_dwell` | 2 | conversions moyennées par canal (1..64) |
| `baseline_init_ms` | 200 | base initiale |
| `baseline_tau_ms` | 500 | suivi lent de la base |
| `rise_thr` | 5 | seuil de début (points au-dessus de la base) |
| `return_thr` | 3 | seuil de retour (bruit max 3 points) |
| `return_hold_us` | 1000 | maintien du retour |
| `split_pct` | 30 | coupure de trille (1..99) |
| `min_pulse_us` | 500 | impulsion minimale |
| `drive_blank_ms` | 50 | masquage après pilotage |
| `trace_period_us` | 20 | période de capture `trace` |
| `trace_trig` | 5 | déclenchement de `trace` |
| `stream_decim` | 1 | décimation de `stream` |
| `vel_fixed` | 100 | vélocité MIDI fixe |
| `vel_min_delta` / `vel_max_delta` | 0 / 0 | vélocité proportionnelle si non nuls |
| `detail` | 1 | émet `LISTEN_PULSE_TIMING` (durées dans le journal) |

- Contrôles : `return_thr < rise_thr`, `split_pct` 1..99,
  `samples_per_dwell` 1..64, `trace_period_us ≥ 2`, `stream_decim ≥ 1`,
  `vel_min_delta < vel_max_delta` si non nuls, `vel_fixed` 1..127.
- Les cartes Moteur ne compilent **aucune** valeur d'écoute : le Clavier envoie
  le jeu complet avant chaque démarrage, et `LISTEN_CONTROL` porte le CRC16 du
  jeu. Écart → `PARAM_MISMATCH`, écoute refusée. RAM Moteur seulement.
- Console Clavier, famille **`e`** : `e show`, `e get`, `e set <champ> <v> …`
  (diffusion immédiate), `e diff` (lignes à recopier dans `listen_config.h`),
  `e reset`, `e rec`, `e bruit`, `e trace <touche>`, `e stream <touche>|off`,
  `e raz`, `e ?`. `<touche>` = nom de note (`MI3`) ou numéro MIDI.

#### 6. Bruit des voisins (`e bruit`)

Après chaque impulsion de la touche k, le Clavier cherche k−1 et k+1 dans la
**table de commissioning** (toutes cartes), envoie `LISTEN_NOISE_QUERY`
(lookback = t_end − t0 + 2 ms) ; réponse `LISTEN_NOISE_REPORT` (min, max, base).
Fenêtre > 256 ms → `TRONQUE` ; voisin non attribué → `[BRUIT] k-1 : non attribuée`.

#### 7. Mode REC côté Clavier (Q1)

- Deux keycodes de bascule (positions libres, invariant 6) :
  - **`REC_SOLE`** : solénoïdes muets, **MIDI de la matrice conservé** ;
  - **`REC_MUTE`** : solénoïdes muets **et** MIDI de la matrice muet (seules
    les notes captées sortent).
  En REC, l'une ou l'autre touche fait sortir du mode. `e rec` = `REC_SOLE`
  (comportement par défaut).
- Entrée refusée sans table valide (`REC_NO_TABLE`), pendant un auto-test, une
  synchronisation de profil ou la MAINTENANCE. À l'entrée : coupure des
  solénoïdes actifs, Note Off des notes matrice actives si `REC_MUTE`, envoi du
  jeu de paramètres, `LISTEN_CONTROL ON`, attente des `LISTEN_STATUS` des
  cartes attendues (cartes muettes `REC_BOARD_SILENT`, par UID).
- En REC : aucune trame `0x100` ; MIDI IN non retransmis aux Moteur (compté,
  signalé au plus 1/s) ; les keycodes non-note (transpositions, REC…) restent
  actifs ; MAINTENANCE refusée.
- Sortie : `LISTEN_CONTROL OFF`, Note Off de toute note captée encore active,
  `[REC] OFF`.

#### 8. De l'impulsion à la note (Q2, Q3)

- Classement **côté Clavier** par (UID, canal) ; Moteur anonyme (invariant 2).
  (UID, canal) → touche → note par la table de commissioning (invariant 3) ;
  canal non attribué → `[REC] canal non attribue`, aucune note.
- **Alternance** : 1re impulsion = appui, 2e = relâchement ; toutes touches
  relevées à l'entrée en REC ; `e raz` réinitialise. Une règle par signature
  ne sera adoptée que sur mesures, par amendement.
- Note **littérale** (aucune transposition, armure ni altération), **même
  canal MIDI** que la matrice, sorties USB + DIN.
- Vélocité : `vel_fixed`, ou linéaire du delta si `vel_min_delta`/`vel_max_delta`.
- Fusion avec la matrice par compteur par note : Note On au passage 0 → 1,
  Note Off au passage 1 → 0.
- Les impulsions sont **toujours** journalisées (`[LISTEN]`) en REC : le réglage
  et l'usage final sont le même mode.

#### 9. Nouvelles trames (`shared/protocol/can_protocol.h`)

| ID | Nom | Sens | DLC | Contenu |
|---|---|---|---|---|
| `0x110` | `LISTEN_PULSE` | Moteur → Clavier | 8 | uid_tag u16, canal (bits 0–3) + drapeaux (bit 4 trille, bit 5 durée saturée), seq u8, base u12 + crête u12 (3 octets), réservé |
| `0x404` | `LISTEN_CONTROL` | Clavier → diffusion | 8 | proto, mode (0 OFF, 1 ÉCOUTE, 2 TRACE, 3 STREAM, 4 STREAM_OFF), param_crc16, uid_tag cible u16 (TRACE/STREAM), canal u8, réservé |
| `0x405` | `LISTEN_PARAM` | Clavier → diffusion | 8 | proto, param_id u8, valeur u32, réservé u16 |
| `0x406` | `LISTEN_NOISE_QUERY` | Clavier → diffusion | 6 | uid_tag cible u16, canal u8, request_id u8, lookback_ms u16 |
| `0x426` | `LISTEN_STATUS` | Moteur → Clavier | 8 | uid_tag u16, état u8, param_id refusé u8, période de balayage µs u16, débordements u8, réservé |
| `0x427` | `LISTEN_NOISE_REPORT` | Moteur → Clavier | 8 | uid_tag u16, canal u8, request_id u8, min u12 + max u12 (3 octets), drapeaux (bit 0 tronqué) |
| `0x428` | `LISTEN_PULSE_TIMING` | Moteur → Clavier | 8 | uid_tag u16, seq u8, t_peak−t0 u16, t_end−t_peak u16, réservé ; unité 100 µs, saturé à 0xFFFF |
| `0x429` | `LISTEN_TRACE` | Moteur → Clavier | 8 | seq u16, 4 échantillons u12 (6 octets) ; une seule carte émet à la fois ; sert à `trace` et `stream` |

#### 10. Journal (PuTTY)

- `[LISTEN] pos=2 ch=7 touche=MI3 min=14 max=27 delta=13 m>M=3.4ms M>m=9.1ms m>M>m=12.5ms [trille] -> ON|OFF`
- `[BRUIT] k-1 pos=2 ch=8 touche=RE#3 min=13 max=16 base=14` / `k+1 …`
- `[LISTEN] pos=N etat=… periode=…us debordements=…`
- `[TRACE] …` résumé seul ; `[REC] ON SOLE|MUTE`, `[REC] OFF`, refus nommés.

#### 11. Précisions d'implémentation (Maître, 2026-11-10)

- **MIDI** : le code actuel n'a que la sortie USB (`midi.c`) ; ni DIN ni MIDI IN
  ne sont implémentés. Les notes captées partent par **le même chemin que la
  matrice** (USB aujourd'hui, DIN le jour où il existera). Le filtrage du
  MIDI IN (§7) est sans objet tant que MIDI IN n'existe pas.
- **`LISTEN_TRACE`** n'a pas d'`uid_tag` : le Clavier l'attribue à l'unique
  carte ciblée par le dernier `LISTEN_CONTROL` TRACE/STREAM.
- **`e bruit`** bascule l'envoi automatique des `LISTEN_NOISE_QUERY` après
  chaque impulsion ; la requête part à la réception de `LISTEN_PULSE_TIMING`.
  `e bruit` est refusé si `detail = 0` (`necessite detail=1`).
- **`trace` sans déclenchement** : nouveau champ `trace_timeout_ms` (défaut
  10000) ; à l'échéance, `LISTEN_STATUS state=TRACE_TIMEOUT` et retour au
  balayage.
- **Vélocité proportionnelle** : v = 1 + (delta − vel_min_delta) × 126 /
  (vel_max_delta − vel_min_delta), bornée à 1..127 (borne MIDI, pas une limite
  électrique).
- **Auto-test et écoute** : pas de suspension en cours d'écoute. Un `0x401`
  reçu pendant l'écoute est refusé `REFUSED_BUSY` ; un `LISTEN_CONTROL ON`
  reçu pendant un auto-test est refusé `LISTEN_STATUS state=BUSY_SELFTEST`.
  À `LISTEN_CONTROL OFF`, le Core 0 attend que le Core 1 soit **parqué**
  (drapeau) avant de rendre le mux et l'ADC à l'auto-test.
- **Validation** : N1 (tests hôte, builds Clavier et Moteur 0 erreur /
  0 warning) dans la chaîne ; N2 (partage Core 0/Core 1, banc) dans
  `tests/lecture_test_roadmap.md`.

### Consequences

#### Positive
* **POS-001**: Aucun changement d'état électrique : l'écoute exploite le frein au repos (ADR-0024).
* **POS-002**: Mémoire fixe (≈ 25 Ko Moteur, dont 8 Ko de trace), aucun stockage illimité.
* **POS-003**: Tous les seuils réglables en direct et recopiables dans un seul fichier.
* **POS-004**: Courbes dans Serial Studio sans saturer PuTTY ; option retirée du build de production.
* **POS-005**: Cartes anonymes ; la musique reste dans la table de commissioning.

#### Negative
* **NEG-001**: Sens du mouvement perdu : classement par alternance ; une impulsion manquée inverse la touche jusqu'à la suivante ou `e raz`.
* **NEG-002**: Signal attendu proche du bruit ; échec au banc → décision matérielle humaine.
* **NEG-003**: Core 1 Moteur actif : données partagées entre cœurs à tester.
* **NEG-004**: `stream` charge le bus (~30 %) : outil de réglage seulement.

### Alternatives Considered

#### Détection sur le Clavier à partir d'échantillons bruts
* **ALT-001**: **Description**: envoyer en continu tous les échantillons au Clavier.
* **ALT-002**: **Rejection Reason**: 12 × 4,6 kHz × 5 cartes ≈ 280 000 valeurs/s, hors de portée du CAN.

#### Acquisition sur le Core 0 par IRQ de timer
* **ALT-003**: **Rejection Reason**: concurrence avec l'IRQ PIO de can2040 et les attentes actives de l'auto-test ; le Core 1 est libre.

#### Valeurs brutes dans PuTTY
* **ALT-004**: **Rejection Reason**: illisible et sature la console (demande humaine) ; remplacé par Serial Studio sur un second port CDC.

#### Mode coast pour lire la FEM à vide
* **ALT-005**: **Rejection Reason**: en coast aucun courant ne passe par les MOSFET bas ; aucune entrée ADC sur OUT1/OUT2.

### Implementation Notes

* **IMP-001**: Mesures banc et constantes à ajuster : `tests/lecture_test_roadmap.md` (M1 FEM à vide à l'oscilloscope, M2 inductance, M3 établissement du mux sur GPIO45, G1 rapport signal/bruit par `e trace`, puis réglage des seuils).
* **IMP-002**: Tests hôte N1 : `pulse_detector` (appui isolé, appui + relâchement, trille avec creux bref à 25 %, bruit ±3, impulsion courte, dérive de base), codecs des huit trames, `listen_params_validate`, CRC16 du jeu ; Clavier : alternance, compteur par note, voisins k±1 sur deux cartes, refus d'entrée en REC, REC_SOLE vs REC_MUTE.
* **IMP-003**: Fichiers : `shared/protocol/can_protocol.h/.c`, `shared/protocol/listen_params.h/.c` ; Moteur `listen_core1.{h,c}`, `pulse_detector.{h,c}`, `ipropi_selftest.c` (rendez-vous), `can_link.c`, `main.c`, CMake (`pico_multicore`) ; Clavier `listen_config.h`, `rec_mode.{h,c}`, `bringup_console.c` (`e`), keycodes `REC_SOLE`/`REC_MUTE`, MIDI, `tusb_config.h` + descripteurs (option), `tools/serial_studio/clavicore_rec.json`.
* **IMP-004**: Modules dépendants : filtres RX de `can_link.c` des deux cartes (nouveaux ID `0x110`, `0x404`–`0x406`, `0x426`–`0x429`).

### References
* **REF-001**: [./adr-0001-hal-solenoide-drv8874-phen.md](./adr-0001-hal-solenoide-drv8874-phen.md) — PH/EN, mux IPROPI.
* **REF-002**: [./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) — K = 1,08 V/A.
* **REF-003**: [./adr-0015-reglage-chaud-profil-console-cdc.md](./adr-0015-reglage-chaud-profil-console-cdc.md) — modèle de la console `p`.
* **REF-004**: [./adr-0024-reveil-solenoides-permanent-diagnostic-refus.md](./adr-0024-reveil-solenoides-permanent-diagnostic-refus.md) — nSLEEP haut en permanence.
* **REF-005**: DRV8874 datasheet §7.3.3.1 ; Serial Studio, mode Quick Plot (`/*…*/`).
