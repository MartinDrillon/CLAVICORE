---
title: "ADR-0009: Table de commissioning « provisoire » (bring-up) et estimation de résistance des bobines dans l'auto-test IPROPI"
status: "Accepted"
date: "2026-10-20"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

Amende partiellement, **sans la superséder** :

* ADR-0004 §5 (« Écriture automatique après auto-test réussi, mais seulement si
  toutes les cartes listées dans `uid_position.h` ont répondu ») ;
* ADR-0004 addendum (5) §2 (format flash de la table, en-tête 12 octets,
  `format_version` 1) ;
* ADR-0004 addendum (4) : le profil **PROBE 3 ms / 20 %** y est validé
  empiriquement ; il est porté à **5 ms** avec échantillonnage en fin
  d'impulsion (voir §5).

**Révision du 2026-10-20** (demande humaine, faite alors que l'ADR était encore
`Proposed`) :

* le défaut de multiplexeur de la carte 1 est **corrigé** ; toutes les
  références à cet ancien défaut sont retirées de la présente ADR ;
* `R_IPROPI` est confirmée à **2,4 kΩ** avec `A_IPROPI` = **450 µA/A**, soit
  `K_IPROPI` = **1.08 V/A** (constante de carte établie, non plus hypothèse) ;
* PROBE porté de 3 à **5 ms**, la valeur de référence devenant la moyenne des
  **derniers** échantillons de l'impulsion (§5a, §5b) ;
* ajout du courant estimé à **100 % de duty** (`i100`, §5d, trame 0x601) ;
* les trois propositions de confort (déclencheur Clavier, mode `STRESS`,
  métrique de charge) sont **rejetées par l'utilisateur** et passent en
  alternatives écartées (ALT-013 à ALT-016).

Aucune dérogation à un invariant d'`AGENTS.md` :

* **invariant 1** : la table provisoire n'est jamais une correction silencieuse
  — elle résulte d'un auto-test **explicitement demandé**, et son état
  (`PROVISIONAL`) plus ses motifs sont signalés à l'écriture **et à chaque
  boot** ;
* **invariant 3** : la table reste persistante et côté Clavier, seule source de
  vérité ;
* **invariant 4** : aucune borne du pilote n'est touchée ; le calcul ohmique
  utilise le **duty effectivement appliqué** après écrêtage par le pilote ;
* **invariant 5** : c'est précisément l'objet de la présente ADR (format de
  table et nouvelle trame CAN).

### Context

**Séquence de travail annoncée par l'utilisateur (2026-10-21 et suivants).**
Trois phases de mise en place, toutes pilotées par l'auto-test IPROPI et la
frappe au clavier :

1. chaîne complète des 6 cartes (5 Moteur + Clavier) reliée au banc, quelques
   bobines seulement, réparties au hasard, mesures à l'oscilloscope de
   l'intégrité du bus et du comportement électrique ;
2. **une carte Moteur seule** (hors chaîne, pour limiter le câblage) avec 10 à
   12 solénoïdes, activations massives et répétées depuis le clavier ;
3. installation carte par carte dans le corps du clavecin, solénoïdes ajoutés
   progressivement, auto-test relancé en permanence pour remapper.

**Blocage constaté.** L'écriture flash de la table est aujourd'hui conditionnée
par `finish_selftest_session()` (`firmware/clavier/src/can_link.c:968-1096`) :

* refus si **une seule** position seedée dans `uid_position.h` n'a pas répondu
  (`:978-987`), si une carte a un `test_status != OK` (`:989-992`), si une carte
  répondante n'est pas listée (UID inconnu), si deux `uid_tag` sont identiques,
  ou si `received_count < expected_count` (`:1046`) ;
* en conséquence : **une carte seule sur le banc n'écrit rien** (4 positions
  seedées muettes), et il suffit d'**une** carte absente ou non câblée pour
  perdre le remappage demandé ;
* `total_occupied != 49` (`runtime.partial`) ne bloque déjà pas l'écriture :
  c'est déjà `TABLE_PARTIAL` signalé, et c'est le seul cas « partiel »
  aujourd'hui supporté ;
* `firmware/clavier/src/commissioning.c:89-131` (`commissioning_state_store`)
  n'expose aucune autre voie d'écriture, et le format flash
  (`shared/protocol/commissioning_table.h:11-16`, en-tête 12 o, `version = 1`)
  ne contient **aucun champ d'état** : une table écrite est implicitement
  « complète et valide ».

**Conséquence pour la phase 3.** Sans changement, l'utilisateur doit soit
maintenir `uid_position.h` au nombre exact de cartes présentes (rebuild + flash
à chaque étape), soit renoncer à persister le mapping. Aucune des deux n'est
acceptable : le mapping doit être réécrit **à chaque test réussi**, y compris
partiel.

**Second besoin : résistance des bobines.** Les bobines sont faites main ; leur
dispersion de résistance fait varier le courant donc la force. L'utilisateur
demande, à côté des valeurs ADC brutes du test, une estimation « ohm » calculée
directement (pas de conversion à la main), avec la chaîne :

```
V_IPROPI = ADC/4095 × 3.3 V
I_OUT    = V_IPROPI / 1.08 V/A
V_bobine = D × V_VM − I_OUT × R_DS(on)
R_bobine = D × V_VM / I_OUT − R_DS(on) = 32164.36 × D / ADC_RAW − 0.24
```

**Faits à ne pas ignorer (vérifiés au dossier) :**

* La formule est algébriquement exacte pour un courant **établi** et suppose :
  `V_VM = 24.000 V`, référence ADC `3.300 V`,
  `K_IPROPI = A_IPROPI × R_IPROPI = 450 µA/A × 2,4 kΩ = 1.08 V/A`
  (valeurs confirmées sur schéma), `R_DS(on) = 0.24 Ω`. À 3 A, 1.08 V/A donne
  3.24 V : l'ADC sature vers **3.055 A**, cohérent avec la fenêtre 2–3 A.
* La phase PROBE dure aujourd'hui **3 ms** (`SOLENOID_PROBE_DURATION_MS`) à
  **20 %** de duty, et `probe_avg` est prélevé après un délai fixe **depuis le
  départ** de l'impulsion. Mesure à l'oscilloscope (2026-10-20) : le courant est
  quasi établi vers 3 ms, donc un échantillon prélevé tôt dans l'impulsion
  reste en régime transitoire et conduirait à surestimer `R`.
* L'ADC lit `probe_avg`, qui contient le **décalage à courant nul** de l'IPROPI
  et du conditionnement. Utiliser l'ADC brut gonfle le courant d'un facteur qui
  dépend du canal et **fausserait la comparaison entre bobines**, qui est le but
  même de la valeur : c'est `delta = probe_avg − idle_avg`, la grandeur déjà
  utilisée par le critère d'occupation, qui est la bonne entrée.
* `CLAVIER_SELFTEST_CALIBRATION` (option CMake, défaut `OFF`) est requis pour
  que les trames `IPROPI_RAW` 0x600 soient émises. Un build « normal » ne
  montre donc rien.

### Decision

#### 1. Règle de snapshot

La table de commissioning est, à tout instant, **le cliché du dernier auto-test
explicitement demandé**. Elle n'est jamais calculée ni réécrite au boot
(ALT-009 d'ADR-0004 reste valide), et il n'existe **aucune** commande de
réécriture manuelle ni d'édition d'entrée : seul un auto-test produit une table.

#### 2. Conditions d'écriture assouplies

Une écriture a lieu si et seulement si :

* un auto-test a été **demandé** (bouton Moteur 3 s, ou commande `t` de la
  console CDC en livraison B) et le Clavier n'était pas en train de jouer
  (refus existant d'ADR-0004 §2) ;
* **au moins une** carte répondante a fourni un jeu de réponses **complet**
  (A + B + C reçues, `test_status` connu) ;
* aucun `uid_tag` dupliqué parmi les cartes retenues, et aucune position
  dupliquée (validations existantes de `commissioning_validate_entries`) ;
* le contenu à écrire (entrées **et** état/motifs) diffère de la table en
  mémoire — l'écriture « identique » reste supprimée (usure flash).

Ce qui **ne bloque plus** l'écriture, mais la marque provisoire (§3) :

| Situation | Traitement | Motif |
|---|---|---|
| Position seedée dans `uid_position.h` sans réponse | entrée **absente** de la table | `MISSING_BOARD` |
| Carte répondante absente de `uid_position.h` | **exclue** de la table (aucune position devinée), signalée `UID_NOT_IN_TABLE` | `UNKNOWN_BOARD` |
| Carte répondante avec `test_status != OK` (occupé, abandonné, non calibré) | entrée **conservée** avec `occupation_bitmap = 0` (état inconnu, aucun canal activable) | `BOARD_NOT_OK` |
| `fault_bits != 0` sur une carte | bitmap tel quel, canal fautif à 0 (règle ADR-0004 §1) | `FAULT_DURING_TEST` |
| `total_occupied != 49` | bitmap tel quel, `TABLE_PARTIAL` conservé | `NOT_49_KEYS` |
| Mode diagnostic actif et RAW manquantes pour un canal | n'affecte **jamais** le bitmap ni l'écriture | `DIAG_INCOMPLETE` |

Les cartes muettes **disparaissent** de la table : c'est volontaire. Le cliché
doit correspondre exactement à ce que le dernier test a vu, sinon une carte
réapparue plus tôt que prévu se verrait attribuer des touches héritées d'un
état périmé. Toute suppression est signalée (§4).

#### 3. État de table explicite : `FINAL` / `PROVISIONAL`

Un champ d'état est persisté avec la table. Règle unique et testable :

```
state = FINAL        si flags == 0
state = PROVISIONAL  sinon
```

Motifs (`flags`, 8 bits) : `0x01 MISSING_BOARD`, `0x02 UNKNOWN_BOARD`,
`0x04 BOARD_NOT_OK`, `0x08 FAULT_DURING_TEST`, `0x10 NOT_49_KEYS`,
`0x20 DIAG_INCOMPLETE` (bits 5–7 réservés, émis à 0).

> **Ajout par ADR-0010 (2026-10-20, `Accepted`)** : `0x40 ANCHOR_SHIFT` — les
> positions présentes ne forment pas `0..K−1` (position 0 absente ou trou de
> position). Bit 6 jusque-là réservé ; `COMMISSIONING_FLAG_ALL` passe à `0x7F`.
> Aucun autre élément du §3 ne change.

Une table `PROVISIONAL` est **pleinement utilisable** (les canaux présents
jouent) : c'est un état informatif, pas un verrou. Aucune promotion manuelle :
la table devient `FINAL` au premier test qui satisfait tous les critères.

#### 4. Signalement obligatoire (invariant 1)

* À l'écriture : `[TABLE] state=PROVISIONAL flags=0xNN cards=K keys=N` puis, si
  l'ancienne table différait, la ligne de différentiel existante (`TABLE_DIFFERS`)
  **par position**.
* À chaque boot : la même ligne `[TABLE] ...` au chargement, plus
  `[STATUS] code=TABLE_PROVISIONAL` (nouveau code sémantique `0x31`, transport
  CDC d'ADR-0005) quand l'état est provisoire.
* À chaque boot et à chaque écriture : une ligne **par canal assigné**,
  `[MAP] pos=P ch=C key=K midi=M note=DO3(48)` (nommage scientifique,
  ADR-0008), pour que l'utilisateur sache quelle touche frappe quel solénoïde
  sans calcul. Sous constante `CLAVIER_MAP_LOG` (défaut `1` pendant le
  bring-up ; `0` en configuration finale).
* Carte présente sur le bus mais absente de la table : ses canaux restent
  **refusés** et signalés (`UID_NOT_IN_TABLE`), sans remappage.
* Aucune ligne `[MAP]`/`[TABLE]` n'est supprimée pour « faire propre » tant que
  l'état est provisoire.

#### 5. Mesure IPROPI : PROBE 5 ms, référence en fin d'impulsion, résistance et courant estimés

##### 5a. Profil PROBE

`SOLENOID_PROBE_DURATION_MS` passe de **3 à 5 ms**, duty inchangé (**20 %**).
Le plafond de la couche pilote `SOLENOID_PROBE_MAX_MS` (10 ms) reste inchangé :
la nouvelle durée reste dans les bornes appliquées par le driver (invariant 4).
Justification : courant quasi établi mesuré vers 3 ms à l'oscilloscope ; 5 ms
donne une marge et permet une fenêtre d'échantillonnage stable en fin
d'impulsion.

##### 5b. Fenêtre d'échantillonnage en fin d'impulsion

* `probe_avg` devient la moyenne des **N derniers échantillons de l'impulsion**
  (fenêtre calée sur la fin, marge `IPROPI_SELFTEST_ADC_TAIL_MARGIN_US` avant la
  coupure par le pilote), et non plus une moyenne prélevée après un délai fixe
  depuis le départ.
* La fenêtre doit couvrir **au moins une période PWM complète** (50 µs à
  20 kHz) et rester entièrement à l'intérieur de l'impulsion.
* `IPROPI_SELFTEST_ADC_SAMPLE_DELAY_US` est **remplacé** par
  `IPROPI_SELFTEST_ADC_TAIL_MARGIN_US` ; l'assertion statique de
  `ipropi_selftest.c` est réécrite en conséquence.
* `idle_avg` reste mesuré **avant** l'impulsion, mux stabilisé sur le canal.
* **Un seul instant de référence** : la même valeur `probe_avg` sert au critère
  d'occupation (`delta`) et aux estimations ci-dessous. Pas de seconde mesure.

##### 5c. Estimation de résistance : calcul côté Moteur

Le calcul appartient au Moteur, seul détenteur du **duty réellement appliqué**
(après écrêtage par le pilote) et des constantes électriques du profil. Le
Clavier ne refait pas ce calcul et ne duplique aucune constante électrique.

* Entrée : `delta = max(probe_avg − idle_avg, 0)` — **la même grandeur que le
  critère d'occupation**, et non `probe_avg` brut (décalage à courant nul).
* Constantes nommées, dans `firmware/moteur/src/solenoid_profile.h` :
  `SOLENOID_VM_MILLIVOLT` (24000, à mesurer), `SOLENOID_ADC_VREF_MILLIVOLT`
  (3300, à mesurer), `SOLENOID_IPROPI_UA_PER_A` (**450**, `A_IPROPI` du
  DRV8874), `SOLENOID_IPROPI_RESISTOR_OHM` (**2400**, `R_IPROPI`),
  `SOLENOID_RDSON_MILLIOHM` (240, datasheet DRV8874 à 25 °C). La sensibilité
  `K = A_IPROPI × R_IPROPI` est calculée à partir de ces deux constantes, jamais
  écrite en dur.
* Formule implantée (entière, milliohms) :

```
R_mΩ = (duty_permil × VM_mV × 4095 × K_V) / (1000 × delta × VREF_mV) − Rds_mΩ
```

  soit, avec les valeurs nominales (`K = 1.08 V/A`), exactement la formule
  demandée :

```
R_mΩ = 32164 × duty_permil / delta − 240        (duty_permil = 200 → R = 6432.8/delta)
```

* Valeur **invalide** (pas de chiffre) si : `delta < IPROPI_SELFTEST_THRESHOLD_DELTA_ADC`
  (aucun courant détecté : canal vide), `delta == 0`, ou résultat > 65 534 mΩ
  (clamp). Résultat transmis en milliohms u16, `0xFFFF` = invalide.
* Portée : **estimation de la résistance DC**, valable dans la mesure où le
  courant est établi dans la fenêtre de mesure — c'est l'hypothèse de §5a. Elle
  reste affectée par la température du cuivre (±15 % entre froid et chaud), le
  câblage et les tolérances de `VM`, `VREF`, `R_DS(on)`. Elle ne doit **jamais**
  servir de seuil, de décision d'occupation ou de limite de courant (invariant 4
  inchangé).
* **Auto-vérification** : comparer une bobine à la mesure au multimètre. Si le
  rapport est systématiquement ≠ 1.00 (par exemple courant encore en
  transitoire), c'est la fenêtre de §5b ou `VM`/`VREF` qui est en cause, pas la
  formule.

##### 5d. Courant estimé à 100 % de duty (`i100`)

À moindre frais, avec `R` et `VM` connus :

```
I100_mA = VM_mV × 1000 / (R_mΩ + Rds_mΩ)        (R = 6000 mΩ → 3846 mA)
```

* C'est le courant que la bobine tirerait en régime établi à **100 % de duty** —
  c'est-à-dire la phase **PULL** (100 %, 15 ms) une fois le courant établi.
  Éclaire directement la fenêtre 2–3 A : une bobine sous **7.8 Ω** dépasserait
  3 A à 100 % de duty (et l'ADC saturerait à 3.055 A).
* **Purement informatif** : il ignore la chute de la 24 V en charge et la
  régulation interne de courant du DRV8874. Ce n'est **pas** une limite de
  sécurité et il n'est jamais utilisé pour autoriser ou refuser une activation.
* Invalide (`0xFFFF`) si `R` est invalide ; clampé à 65 534 mA.
* Le Clavier affiche, **par carte**, une ligne de synthèse :
  `[SELFTEST_OHM] pos=N n=K ohm_min=A.AA ohm_max=B.BB spread=P% i100_min=X.XX i100_max=Y.YY warn=...`
  avec `spread = (max−min)/moyenne` sur les canaux occupés, et `warn=i100>3.0A`
  si au moins un canal dépasse la fenêtre. C'est cette ligne qui répond au besoin
  « toutes mes bobines à peu près pareilles et dans la fenêtre ».

#### 6. Nouvelle trame de diagnostic `IPROPI_OHM` (0x601)

* ID **0x601**, **DLC 8**, sens **Moteur → Clavier**, plage diagnostic (après
  0x600), priorité inférieure aux IDs de jeu.
* Format : `[0..1]` `uid_tag` LE ; `[2]` `reply_to` ; `[3]` `channel` (0..11) ;
  `[4..5]` `ohm_milliohm` u16 LE ; `[6..7]` `i100_mA` u16 LE
  (`0xFFFF` = invalide pour l'un comme pour l'autre).
* Émission : **une trame par canal**, uniquement en mode diagnostic, **après**
  les 12 trames `IPROPI_RAW`, même espacement, même délai initial UID-dérivé.
* Décodage à DLC strict. La trame est **purement informative** : son absence, sa
  perte ou son invalidité ne changent ni `occupation_bits`, ni `fault_bits`, ni
  l'écriture de la table.

#### 7. Mode diagnostic activé par défaut pendant le bring-up

`CLAVIER_SELFTEST_CALIBRATION` passe de `OFF` à **`ON`** par défaut (option CMake
conservée), pour que `IPROPI_RAW` + `IPROPI_OHM` sortent sans build dédié. À
repasser à `OFF` en configuration finale (une ligne de `docs/specs/Roadmap.md`).
Le flag transmis reste `request_id | 0x80` : **aucune trame existante ne
change**.

#### 8. Format flash de la table — version 2

En-tête **16 octets** (entrées reprises à l'octet 16, enregistrement 13 octets
inchangé) :

| Octet | Contenu |
|---|---|
| 0..3 | `magic` `'CLVC'` |
| 4..5 | `format_version` = **2** |
| 6..7 | `count` |
| 8..11 | `crc32` sur les **enregistrements** uniquement |
| 12 | `state` (0 `FINAL`, 1 `PROVISIONAL`) |
| 13 | `flags` |
| 14..15 | `reserved` = 0 |

* Lecture d'une table **version 1** : acceptée, interprétée `state = FINAL`,
  `flags = 0` (une v1 n'a pu être écrite qu'avec toutes les cartes présentes).
* Écriture : **toujours** en version 2. Version inconnue : `BAD_VERSION`, table
  refusée (comportement actuel).
* `commissioning_entry_t` (uid, position, bitmap, reserved) est **inchangé** ;
  `reserved` reste à 0.

#### 9. Livraisons

* **A — avant les essais (priorité)** : format v2 + conditions d'écriture +
  état/motifs + lignes `[TABLE]`/`[MAP]` + PROBE **5 ms** et fenêtre de fin
  d'impulsion + trame 0x601 (`ohm_milliohm`, `i100_mA`) + calcul ohmique et
  `i100` + `[SELFTEST_RAW] ... ohm_est=... i100=...` + synthèse
  `[SELFTEST_OHM]` + `CLAVIER_SELFTEST_CALIBRATION` à `ON`.
* **B — console CDC de bring-up** : entrée `stdin` CDC (`getchar_timeout_us`)
  acceptant `t` (déclencher un auto-test, même chemin et mêmes refus que la
  trame 0x402), `c` (basculer le mode diagnostic pour la session), `d` (dumper
  table + mapping), `m` (état : mode maintenance, état de table, cartes en
  ligne), `?` (aide). **Aucune** commande d'écriture ou d'effacement de la
  table. Gardée par `CLAVIER_CDC_CONSOLE` (défaut `1` en bring-up).

### Consequences

#### Positive
* **POS-001**: Le mapping demandé est persisté à **chaque** test réussi, y compris avec une seule carte : la phase 2 et la phase 3 deviennent possibles sans rebuild.
* **POS-002**: L'état `PROVISIONAL` est une information lisible (journal, boot) et non un verrou : aucun test de banc n'est bloqué, aucune activation légitime n'est perdue.
* **POS-003**: La table reste le cliché d'un test **demandé** : invariant 1 respecté, aucune correction silencieuse, aucune édition manuelle possible.
* **POS-004**: L'estimation ohmique compare les bobines sur une base homogène (décalage retiré, fenêtre de fin d'impulsion, même duty) et fait apparaître les outliers et les bobines hors fenêtre 2–3 A dans une ligne de synthèse.
* **POS-005**: Les constantes électriques restent à un seul endroit (`solenoid_profile.h`, côté Moteur) : le Clavier n'en duplique aucune.

#### Negative
* **NEG-001**: Une table `PROVISIONAL` peut être jouée alors qu'elle ne couvre pas tout le clavier ; seules les lignes de journal l'indiquent. Risque assumé, borné à la phase de bring-up.
* **NEG-002**: `ohm_est` reste une **estimation** : elle suppose le courant établi dans la fenêtre de fin d'impulsion (vérifié à l'oscillo, à reconfirmer sur 5 ms) et ignore la température du cuivre, le câblage et les tolérances de `VM`/`VREF`/`R_DS(on)`. Un écart systématique avec un multimètre doit être diagnostiqué (fenêtre, `VM`, `VREF`), pas corrigé en aveugle.
* **NEG-003**: Chaque test qui change le contenu écrit la flash : davantage de cycles d'effacement (endurance ~100 k cycles/secteur, non contraignante à cette échelle) et davantage de fenêtres à IRQ coupées — toujours **hors jeu**, puisque le test est refusé pendant la frappe.
* **NEG-004**: Une carte absente ou en échec **disparaît** ou voit son bitmap forcé à 0 : son retour exige un nouveau test, ce que l'utilisateur fait déjà. Signalé, jamais deviné.
* **NEG-005**: Le format v2 et la trame 0x601 sont deux interfaces partagées à synchroniser (Codeur + Documentaliste + tests) ; un build mixte Moteur/Clavier ancien/nouveau reste compatible mais n'affiche pas l'ohm (simple absence de la trame).
* **NEG-006**: Les lignes `[MAP]` (jusqu'à 49) et `[SELFTEST_RAW]` (24 lignes) rendent le journal bavard au boot/test ; réglable par constante.

### Alternatives Considered

#### Maintenir `uid_position.h` au nombre exact de cartes présentes
* **ALT-001**: **Description**: l'utilisateur retire les cartes absentes du fichier seed, rebuild, reflash, à chaque étape d'installation.
* **ALT-002**: **Rejection Reason**: impose une chaîne de build à chaque étape de câblage, introduit un UID « inconnu » dès qu'une carte revient, et ne répond pas au besoin (test à répétition, plusieurs fois par heure).

#### Écriture sur commande manuelle uniquement (touche ou console)
* **ALT-003**: **Description**: le test ne persiste rien ; l'utilisateur enregistre explicitement la table courante.
* **ALT-004**: **Rejection Reason**: deux sources de vérité possibles (table chargée vs table en RAM), et une étape oubliée donne un mapping perdu au reboot ; le snapshot automatique d'un test demandé est plus simple et plus sûr.

#### Conserver les entrées des cartes muettes (non destructif)
* **ALT-005**: **Description**: les positions absentes gardent leur bitmap précédent, marquées « non confirmées ».
* **ALT-006**: **Rejection Reason**: avec la règle compacte descendante, une carte de banc seule ne recevrait plus les touches 49..38 (elle serait bloquée sous les entrées conservées) : le test de la phase 2 deviendrait illisible.

#### Estimer la résistance côté Clavier à partir de `IPROPI_RAW`
* **ALT-007**: **Description**: aucune nouvelle trame ; le Clavier applique la formule avec les constantes électriques copiées en local.
* **ALT-008**: **Rejection Reason**: duplique des constantes de carte et de profil hors du Moteur, et ignore le duty réellement appliqué après écrêtage ; contraire à la logique « limites électriques dans la couche pilote ».

#### Étendre `IPROPI_RAW` 0x600 plutôt que créer 0x601
* **ALT-009**: **Description**: ajouter l'ohm dans la trame existante (DLC > 8 ou compression de `probe_avg`).
* **ALT-010**: **Rejection Reason**: 0x600 est déjà plein à DLC 8 ; le compresser casserait un format accepté et testé. Une trame diagnostic supplémentaire coûte 12 trames par test.

#### Ajouter tout de suite une phase `RES` (courant établi) pour mesurer la vraie résistance
* **ALT-011**: **Description**: phase dédiée de la couche pilote, duty bas, durée ≥ 5 τ, pour obtenir une résistance DC correcte indépendamment de PROBE.
* **ALT-012**: **Rejection Reason**: inutile dès lors que le courant est établi vers 3 ms et que la mesure se fait sur la fin d'une impulsion de 5 ms (§5a, §5b) : une phase supplémentaire ajouterait une borne électrique de plus dans le pilote (invariant 4) sans gain de précision, pour un risque mécanique supérieur.

#### Déclencheur Clavier de l'auto-test (touche de contrôle, appui 1 s)
* **ALT-013**: **Description**: une touche de contrôle du Clavier déclenche une demande d'auto-test, sans devoir atteindre le bouton de la carte Moteur.
* **ALT-014**: **Rejection Reason**: **rejeté par l'utilisateur le 2026-10-20** ; consomme un switch de contrôle, et la console CDC (`t`, livraison B) couvre le besoin sans matériel.

#### Mode `STRESS` borné et métrique de charge embarquée
* **ALT-015**: **Description**: balayage répétitif des canaux occupés d'une carte (coupure dure 30 s) et compteurs du maximum de canaux simultanément actifs et de la durée d'activation cumulée.
* **ALT-016**: **Rejection Reason**: **rejeté par l'utilisateur le 2026-10-20** ; les essais « 12 solénoïdes en même temps et à répétition » se font à la frappe au clavier, sans outil embarqué supplémentaire.

### Implementation Notes
* **IMP-001**: `shared/protocol/commissioning_table.[ch]` : `COMMISSIONING_FLASH_FORMAT_VERSION` → 2, `COMMISSIONING_FLASH_HEADER_SIZE` → 16, nouvelle API d'encodage/décodage portant `state` et `flags` (enum `commissioning_table_state_t` + `#define` de motifs). Décodage v1 → `FINAL`/`0`. Tests hôte : v1 lue, v2 aller-retour, CRC, états.
* **IMP-002**: `firmware/clavier/src/can_link.c` (`finish_selftest_session`) : remplacer le refus global « toutes les cartes » par la table de décision du §2 ; conserver les refus `DUPLICATE_TAG`/position et le refus pendant le jeu ; ne jamais écrire si aucune carte complète.
* **IMP-003**: Calculer `flags` **avant** l'écriture et l'exposer aussi dans la ligne `[TABLE]` ; une carte `test_status != OK` donne une entrée bitmap 0, jamais un bitmap partiel.
* **IMP-004**: Trame 0x601 : ID + DLC **8** dans `shared/protocol/can_protocol.h`, encodeur/décodeur DLC strict, `can_ipropi_ohm_t` (`ohm_milliohm`, `i100_mA`), tests hôte. Ne pas toucher 0x600.
* **IMP-005**: `firmware/moteur/src/ipropi_selftest.c` : `SOLENOID_PROBE_DURATION_MS` → **5 ms** dans `solenoid_profile.h` ; remplacer `IPROPI_SELFTEST_ADC_SAMPLE_DELAY_US` par `IPROPI_SELFTEST_ADC_TAIL_MARGIN_US` et déplacer la fenêtre d'échantillonnage **en fin d'impulsion** ; assertion statique `N × t_adc + marge ≤ SOLENOID_PROBE_DURATION_MS`, et fenêtre ≥ 1 période PWM (50 µs). Calcul de `R` et `i100` en 64 bits (`UINT64_C`) puis clamp u16 ; utiliser le duty PROBE **appliqué** exposé par le pilote (écrêtage `SOLENOID_PROBE_DUTY_MAX_PERCENT`), jamais la constante brute du profil.
* **IMP-006**: Constantes électriques dans `solenoid_profile.h` uniquement : `SOLENOID_VM_MILLIVOLT` et `SOLENOID_ADC_VREF_MILLIVOLT` (à mesurer), `SOLENOID_IPROPI_UA_PER_A` = **450**, `SOLENOID_IPROPI_RESISTOR_OHM` = **2400**, `SOLENOID_RDSON_MILLIOHM` = **240**. `K = A_IPROPI × R_IPROPI` est **calculé**, jamais écrit en dur (450 µA/A × 2,4 kΩ = 1.08 V/A).
* **IMP-007**: `[SELFTEST_RAW]` : ajouter `ohm_est=` (ohms, 2 décimales, `--` si invalide) et `i100=` (ampères, 2 décimales) sur la ligne `delta=` ; ne pas créer de nouvelle ligne par canal. Synthèse `[SELFTEST_OHM]` par carte calculée côté Clavier, uniquement sur les canaux occupés, avec `warn=i100>3.0A`.
* **IMP-008**: `[MAP]` et `CLAVIER_MAP_LOG` dans `diag_log.c` ; nommage de note via `diag_format_note_name_fr()` (déjà conforme ADR-0008). `[TABLE] state=... flags=0xNN` à l'écriture **et** au chargement.
* **IMP-009**: `CMakeLists.txt` du Clavier : `CLAVIER_SELFTEST_CALIBRATION` par défaut `ON` (bring-up). Documenter le retour à `OFF` en phase finale.
* **IMP-010**: `solenoid_driver.c` : **aucune nouvelle phase, aucun nouveau plafond**. Seuls changent la constante de durée PROBE du profil (3 → 5 ms, sous le plafond existant de 10 ms) et la fenêtre d'échantillonnage côté auto-test.
* **IMP-011**: Valeurs de référence pour les tests hôte (duty 200 ‰, VM 24.000 V, Vref 3.300 V, K 1.080 V/A, Rds 240 mΩ) : `delta = 1030` → **6.00 Ω** et **3.85 A** ; `delta = 800` → **7.80 Ω** et **2.99 A** ; `delta = 1200` → **5.12 Ω** et **4.48 A** ; `delta = 400` (seuil) → **15.84 Ω** et **1.49 A** ; `delta = 0` → invalide.
* **IMP-012**: Livraison B : lecture CDC non bloquante dans la boucle principale (`getchar_timeout_us(0)`), une lettre + `Entrée`, échos sur la même ligne `[CMD]`. Aucune commande d'écriture/effacement.

**Tâches ordonnées — Codeur**
1. `shared/protocol` : format v2 (en-tête, état, motifs) + tests hôte v1/v2.
2. `shared/protocol` : trame `IPROPI_OHM` 0x601 + tests hôte.
3. Moteur : PROBE 5 ms + fenêtre de fin d'impulsion, calcul `R` et `i100`, émission 0x601, assertions de fenêtre, constantes de profil.
4. Clavier : conditions d'écriture §2, `state`/`flags`, lignes `[TABLE]`/`[MAP]`, `[SELFTEST_RAW] ohm_est/i100`, `[SELFTEST_OHM]`, option CMake `ON`.
5. Clavier (livraison B) : console CDC `t/c/d/m/?`.

**Tâches — Documentaliste**
1. `docs/specs/CAN.md` : trame 0x601, état de table, codes `0x31`.
2. Spécifier le format flash v2 et la règle « snapshot du dernier test demandé ».
3. Procédure de test de banc pour le bring-up (phases 1–3 décrites dans le Context).

### Questions ouvertes

Aucune question bloquante. La seule inconnue restante est une **mesure de banc**
à faire par l'utilisateur, pas un choix d'architecture :

* écart entre `ohm_est` et une mesure au multimètre sur une bobine de référence
  (§5c, auto-vérification).

### References
* **REF-001**: [./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) — §5, addendum (4), addendum (5) §2.
* **REF-002**: [./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) — transport CDC du diagnostic.
* **REF-003**: [./adr-0008-convention-nommage-octaves-notation-scientifique.md](./adr-0008-convention-nommage-octaves-notation-scientifique.md) — nommage `DO2(36)`…`DO6(84)`.
* **REF-004**: `shared/protocol/commissioning_table.[ch]`, `shared/protocol/can_protocol.[ch]`, `firmware/clavier/src/{can_link,commissioning,diag_log,usb_log}.c`, `firmware/moteur/src/{ipropi_selftest,solenoid_driver,solenoid_profile}.[ch]`.
* **REF-005**: Datasheet DRV8874 (`A_IPROPI` 450 µA/A, `R_DS(on)`, régulation de courant), datasheet RP2350 (ADC 12 bits, 3.3 V), schéma de conditionnement IPROPI (`R_IPROPI` 2,4 kΩ → 1.08 V/A).
