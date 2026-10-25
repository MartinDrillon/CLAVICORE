---
title: "ADR-0019: Phase IMPACT — impulsion courte à pleine puissance avant PULL dans le profil d'activation"
status: "Accepted"
date: "2026-10-22"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol", "hardware"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted** — validée par l'humain le 2026-10-22. L'implémentation de `shared/protocol/` et du firmware Moteur peut commencer ; la liste des fichiers à revoir est dans `docs/specs/impact-adr-0019.md`.

Aucune nouvelle dérogation à un invariant. L'invariant 4 reste dérogé par
ADR-0014 : les trois nouveaux champs sont des données du profil, sans borne fixe
côté firmware. L'invariant 5 est respecté : cette ADR précède toute modification
de `shared/protocol/`. L'invariant 1 est respecté : une divergence de version ou
de profil reste signalée, jamais corrigée en silence.

**Amendé le 2026-10-25** par [ADR-0014, amendement 2026-10-25](./adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md).
`impact_max_us` — et, avec lui, les maxima de durée redondants `pull_max_us` et
`probe_max_us` — est supprimé du profil : le motif `min(duration, max)` et la
règle de rejet `duration <= max` décrits ci-dessous (§2, §4, IMP-003, IMP-006)
ne s'appliquent plus. `impact_duration_us` est appliqué tel quel. Le format
passe directement de v2 à **v3, 54 octets, 9 chunks**, version flash 3. La
décision IMPACT (nouvel état, `impact_duty_pct`, valeurs initiales, mise en
service) reste en vigueur.

### Context

Le profil d'activation (ADR-0013/ADR-0014) décrit aujourd'hui trois segments de
jeu : PULL (duty haut, durée `pull_duration_us`), HOLD (duty bas jusqu'au note
off, coupure `hold_max_ms`) puis RELEASE (polarité inversée,
`release_duration_us`). Le fichier de réglage compile actuellement
`pull_duration_us = 15000` et `pull_duty_pct = 50`
(`firmware/clavier/src/solenoid_profile_config.h:34-35`).

L'utilisateur constate au banc que les solénoïdes sont très puissants : un PULL
à 100 % sur toute sa durée déplace le sautereau trop brutalement et produit un
bruit fort. Il abaisse donc le duty de PULL vers 60 %, au prix d'un départ moins
franc, et demande une phase **IMPACT** avant PULL : une impulsion très courte à
100 % (500 à 1 000 µs) destinée à charger la bobine et à accélérer le début de
course, suivie du PULL à duty réduit.

Physique : le courant d'une bobine monte selon `I(t) = (V/R)·(1 − e^(−t/τ))`,
avec `τ = L/R`. Une impulsion pleine tension augmente `di/dt` et rapproche
l'instant où le courant atteint le seuil de force ; le duty réduit qui suit
limite la force entretenue et le bruit. L'effet dépend de `τ`, donc de
l'inductance à entrefer ouvert, elle-même variable pendant la course : il n'est
pas garanti a priori et devra être mesuré (IPROPI + oscilloscope). Le budget
thermique de l'impulsion est faible : à 100 % pendant 2 ms au plus, elle ajoute
au plus l'équivalent de 2 ms à pleine puissance, contre environ 9 ms pour un
PULL de 15 ms à 60 % — mais c'est le critère acoustique et mécanique, pas
seulement thermique, qui fixera la valeur retenue.

Contraintes qui cadrent la décision :

1. Le profil est une interface partagée
   (`shared/protocol/solenoid_profile.[ch]`) sérialisée champ par champ :
   24 champs, 57 octets, `format_version = 1`. Ajouter des champs impose une
   nouvelle version de format et une ADR avant tout code (invariant 5).
2. La phase est exécutée par la machine à états du **Moteur**
   (`firmware/moteur/src/solenoid_driver.c`), pas par le Clavier. Une constante
   ajoutée seule dans `solenoid_profile_config.h` ne produirait aucun effet :
   le firmware Moteur doit connaître l'état IMPACT. C'est une mise à jour unique
   des cinq cartes, ensuite réglable à chaud.
3. ADR-0015 D1 (implémentée) : la console PuTTY modifie le profil actif côté
   Clavier et le transfère aux cartes (but `TUNING`) ; le pilote Moteur relit le
   profil actif à chaque transition. Les champs IMPACT seront donc pris en
   compte au prochain Note On **sans reflash du Clavier ni du Moteur**. D2
   (transfert volatil) n'est pas implémentée : chaque envoi persiste en flash
   Moteur, limite déjà assumée par ADR-0015.
4. Les versions sont vérifiées au `COMMIT` : un Moteur encore en v1 rejette un
   profil v2 avec `UNSUPPORTED_VERSION` (7) ; un Moteur v2 rejette un profil v1.
   Les enregistrements flash v1 ne sont plus relus (longueur et version) et la
   carte repart sur ses valeurs de repli, signalées `source = DEFAULTS`. Aucune
   interprétation silencieuse n'est possible.
5. Le clavier ne pousse jamais un profil automatiquement (invariant 1) : la
   phase IMPACT n'est active qu'après un transfert explicitement demandé
   (auto-test ou `p send`).

### Decision

#### 1. Un état IMPACT dans la machine à états du Moteur

Un état `IMPACT` est inséré entre `IDLE` et `PULL` :

```
IDLE --(note on)--> IMPACT (impact_duration_us, impact_duty_pct)
                      --(fin du timer)--> PULL (pull_duration_us, pull_duty_pct)
                      --> HOLD (jusqu'au note off)
                      --> RELEASE (polarité inversée)
```

Règles de la transition :

* IMPACT utilise la polarité `dir_level_pull` et s'exécute après les mêmes
  conditions d'éveil que PULL aujourd'hui (`global_ready`, `wake_delay_us`
  écoulé). Aucun délai supplémentaire n'est ajouté : la latence Note On →
  premier front PWM est inchangée.
* À l'expiration de `impact_duration_us`, le duty passe à `pull_duty_pct`,
  l'état devient `PULL` et **le timer de `pull_duration_us` redémarre** :
  `pull_duration_us` conserve sa sémantique de durée du seul segment PULL.
  La durée totale à courant élevé devient `impact_duration_us + pull_duration_us`
  (bornée par `impact_max_us + pull_max_us`).
* Un Note Off pendant IMPACT positionne `note_off_pending` (comme pendant
  PULL) ; à la fin d'IMPACT, le driver va directement en RELEASE, sans passer
  par PULL.
* Un Note On pendant IMPACT est refusé `BUSY`, comme dans tout état non IDLE :
  aucune régression de comportement.
* `impact_duration_us == 0` désactive la phase : le Note On va directement en
  PULL, comportement identique à l'actuel.
* MAINTENANCE (ADR-0012) et PROBE (ADR-0009) sont inchangés : IMPACT ne
  s'applique qu'au chemin de jeu normal.

#### 2. Trois champs ajoutés au profil

| Champ | Type | Unité | Valeur initiale | Contrôle structurel |
|---|---|---|---|---|
| `impact_duration_us` | u32 | µs | 800 | `<= impact_max_us` ; 0 désactive |
| `impact_duty_pct` | u8 | % | 100 | `<= 100` et `<= duty_max_pct` |
| `impact_max_us` | u32 | µs | 2 000 | plafond de cohérence, pas de sécurité |

Les trois champs sont insérés dans l'ordre canonique **après `pwm_freq_hz` et
avant `pull_duration_us`**. `impact_duty_pct` n'a pas de plafond de phase
propre (contrairement à HOLD et PROBE) : le plafond global `duty_max_pct`
s'applique, comme pour PULL et RELEASE. Le couple
(`impact_duration_us`, `impact_max_us`) reproduit le motif déjà utilisé pour
PULL et PROBE ; le pilote applique en défense `min(duration, max)` et la
validation rejette un profil incohérent en bloc.

`impact_max_us` ne fixe **aucune** limite de sécurité : comme tous les maxima
depuis ADR-0014, c'est une donnée du profil, révisable dans les deux sens et
réglable à chaud. Sa valeur par défaut garde la phase dans le régime « très
court » demandé, tout en laissant la marge nécessaire au banc.

#### 3. Format de profil v2 et compatibilité

* `SOLENOID_PROFILE_FORMAT_VERSION` passe de 1 à 2.
* `SOLENOID_PROFILE_WIRE_SIZE` passe de 57 à **66 octets** (57 + 4 + 1 + 4),
  soit **11 chunks de 6 octets** exactement (contre 10 aujourd'hui).
* `SOLENOID_PROFILE_FLASH_VERSION` passe de 1 à 2 : un enregistrement v1 n'est
  jamais relu par un firmware v2, même si sa longueur devenait compatible par
  accident.
* `CAN_PROTO_VERSION` reste 1 : aucune trame existante n'est modifiée, aucune
  trame nouvelle n'est créée. Le code de résultat `UNSUPPORTED_VERSION` (7)
  existe déjà et suffit pour signaler un désaccord de version.
* Les contrôles de version doivent comparer aux constantes
  (`SOLENOID_PROFILE_FORMAT_VERSION`, `SOLENOID_PROFILE_WIRE_SIZE`,
  `SOLENOID_PROFILE_FLASH_VERSION`) et non à des littéraux `1` ou `57`.

Aucune migration n'est prévue : un Moteur reflashé en v2 repart sur ses valeurs
de repli tant qu'aucun profil v2 ne lui a été transféré, et le Clavier le
signale (`PROFILE_FALLBACK_IN_USE`, tag divergent). Une rétrogradation vers un
firmware v1 produit le même effet inverse : les slots v2 sont ignorés.

#### 4. Réglage à chaud par la console (ADR-0015 D1)

Les trois champs sont ajoutés à la table `g_fields[]` de
`firmware/clavier/src/profile_tuning.c` **sans raison de verrouillage** : ils
sont réglables comme `pull_*`. Leur effet est fiable à chaud, puisque le pilote
relit le profil actif à chaque transition. `p set`, `p show`, `p diff`,
`p fields` et le transfert `TUNING` les prennent en charge sans nouveau code de
transport.

Conséquences pratiques : `p set impact_max_us 5000 impact_duration_us 3000`
s'applique atomiquement si les deux valeurs sont cohérentes ; `p diff` imprime
les trois lignes à recopier dans `solenoid_profile_config.h`. La persistance
flash à chaque envoi reste celle d'ADR-0015 D1 (pas de transfert volatil).

#### 5. Valeurs de repli du Moteur : phase désactivée

`solenoid_profile_defaults.h` reçoit `impact_duration_us = 0`,
`impact_duty_pct = 100`, `impact_max_us = 2000`. Une carte reflashée mais pas
encore synchronisée joue donc exactement comme avant, sans improviser une
impulsion à 100 %. La divergence est signalée par le mécanisme de tag existant ;
le premier transfert explicite (auto-test ou `p send`) active la phase.

#### 6. Mise en service

Le Clavier et les cinq cartes Moteur doivent être reflashés **ensemble** : les
deux versions ne peuvent pas échanger de profil. Procédure : reflasher les
cartes, mettre sous tension, vérifier le journal (`PROFILE_FALLBACK_IN_USE` ou
tag divergent attendu), puis lancer un auto-test ou `p send` pour synchroniser
le profil v2. Le réglage fin d'IMPACT se fait ensuite uniquement depuis PuTTY.

### Consequences

#### Positive
* **POS-001**: Le profil de force est découplé : une impulsion courte fournit
  le départ, un duty réduit assure le maintien, sans augmenter la force
  entretenue ni le bruit associé.
* **POS-002**: Aucun reflash supplémentaire pour régler l'impulsion : les trois
  champs sont modifiables depuis PuTTY et prennent effet au Note On suivant,
  conformément à ADR-0015 D1.
* **POS-003**: La compatibilité est explicite dans les deux sens : version
  inconnue rejetée avec le code 7, slots flash incompatibles ignorés, source
  `DEFAULTS` signalée. Aucune lecture erronée d'un ancien payload.
* **POS-004**: Aucune nouvelle trame CAN ni nouveau code de résultat ; le
  transfert reste atomique (CRC, `COMMIT`), avec seulement 9 octets et un chunk
  de plus.
* **POS-005**: Le surcoût thermique de l'impulsion est marginal par rapport au
  régime PULL existant (au plus 2 ms à 100 % contre ~15 ms à 50–60 %).
* **POS-006**: `impact_duration_us = 0` offre un retour arrière immédiat, sans
  recompilation ni reflash.

#### Negative
* **NEG-001**: Une mise à jour unique des cinq cartes Moteur et du Clavier est
  nécessaire. C'est une exception au bénéfice d'ADR-0013 (« seul le Clavier à
  flasher »), justifiée par l'ajout d'un état dans la machine à états.
* **NEG-002**: Après reflash et avant la première synchronisation, la phase
  IMPACT est absente (repli désactivé) alors que le fichier Clavier la décrit :
  comportement volontairement conservateur, mais divergence visible et à
  expliquer à l'utilisateur.
* **NEG-003**: Un profil cohérent mais dangereux peut désormais maintenir 100 %
  pendant `impact_max_us + pull_max_us` : la fenêtre à pleine puissance
  s'allonge, sans nouvelle garantie firmware (ADR-0014).
* **NEG-004**: Le bénéfice mécanique n'est pas garanti : si la constante de
  temps `τ` de la bobine est grande devant `impact_duration_us`, l'impulsion
  n'apporte qu'un supplément de courant limité. Seule la mesure au banc
  tranchera, et le plafond `impact_max_us` devra peut-être être relevé (il est
  réglable à chaud).
* **NEG-005**: Le format v2 invalide les profils persistés en v1 : chaque carte
  subit une paire d'écritures flash au premier transfert, et une rétrogradation
  logicielle repasse par les valeurs de repli.
* **NEG-006**: Les tests hôte, l'outil d'injection SWD et la documentation de
  format doivent être mis à jour ; la machine à états n'étant pas couverte par
  les tests hôte, une procédure de banc reste indispensable.

### Alternatives Considered

#### Une seule constante de durée, duty IMPACT figé à 100 %
* **ALT-001**: **Description**: n'ajouter que `impact_duration_us`, le duty de
  la phase restant 100 % en dur.
* **ALT-002**: **Rejection Reason**: impossible d'explorer 80–100 % au banc
  sans recompiler, ce qui contredit le principe « tout le profil est une
  donnée » (ADR-0013/ADR-0014) et le réglage à chaud (ADR-0015). Le coût d'un
  octet de payload est négligeable.

#### IMPACT comme sous-segment de PULL, sans nouvel état
* **ALT-003**: **Description**: conserver l'état PULL et changer le duty en
  cours de route après `impact_duration_us`.
* **ALT-004**: **Rejection Reason**: la sémantique de `pull_duration_us`
  devient ambiguë (inclut-elle l'impulsion ?), le Note Off pendant la
  sous-phase et la remise à zéro du timer se compliquent, et l'état n'est plus
  observable au diagnostic. Un état distinct est plus simple à tester.

#### Augmenter le duty de PULL et raccourcir sa durée
* **ALT-005**: **Description**: régler PULL à 100 % sur 5 ms au lieu de 60 %
  sur 15 ms, sans phase séparée.
* **ALT-006**: **Rejection Reason**: couple le départ et le maintien ; ne permet
  pas d'obtenir une impulsion forte suivie d'une poussée douce, qui est
  précisément le comportement recherché.

#### Régulation de courant matérielle dans le DRV8874
* **ALT-007**: **Description**: utiliser la régulation interne du DRV8874
  (IPROPI) pour limiter le courant de crête.
* **ALT-008**: **Rejection Reason**: impose une modification de carte et des
  composants sur cinq cartes, n'est pas réglable par le profil, et ne remplace
  pas la maîtrise de la durée d'impulsion. Pourra être étudié comme complément
  de sécurité, hors de cette ADR.

#### Réutiliser PROBE ou MAINTENANCE
* **ALT-009**: **Description**: déclencher l'impulsion via le chemin PROBE
  (5 ms / 20 %) ou MAINTENANCE (polarité inverse).
* **ALT-010**: **Rejection Reason**: PROBE est réservé à la mesure IPROPI
  (fenêtre d'échantillonnage, un canal à la fois) et MAINTENANCE utilise la
  polarité inverse ; aucun ne correspond à un départ de note normal.

### Implementation Notes

* **IMP-001**: Fichiers à modifier : `shared/protocol/solenoid_profile.h/.c`
  (champs, ordre canonique, `encode`/`decode`, `validate`, taille 66),
  `shared/protocol/can_protocol.h` si une constante de taille est exposée,
  `firmware/moteur/src/solenoid_driver.c/.h` (état, transitions, helper de
  durée), `firmware/moteur/src/solenoid_profile_defaults.h`,
  `firmware/moteur/src/solenoid_profile_store.h` (version flash 2),
  `firmware/clavier/src/solenoid_profile_config.h`,
  `firmware/clavier/src/profile_tuning.c`, tests et outils.
* **IMP-002**: Remplacer les littéraux de version et de taille par les
  constantes : contrôle de version au `COMMIT` dans
  `firmware/moteur/src/can_link.c` (actuellement `:396-398`), décodage dans
  `solenoid_profile.c` (actuellement `:94-96`), `slot_parse` de
  `solenoid_profile_store.c` (actuellement `:74` et `:76`).
* **IMP-003**: Driver : ajouter l'état, un helper
  `impact_duration_us()` = `min(impact_duration_us, impact_max_us)`, et le
  traitement de `note_off_pending` pendant IMPACT. Conserver la comparaison de
  temps tolérante au débordement (`elapsed()`). Ne pas ajouter de délai
  d'éveil : IMPACT démarre là où PULL démarrait.
* **IMP-004**: Console : trois entrées `FIELD(...)` dans l'ordre de la structure,
  sans verrou. Mettre à jour la liste attendue du test
  (`tests/host/test_phase3_helpers.c`, liste `wanted[]`, actuellement `:827`)
  et les fixtures de `tests/host/test_console_tuning.c`.
* **IMP-005**: Repli Moteur : `impact_duration_us = 0`, avec un commentaire
  explicite « phase désactivée tant qu'aucun profil v2 n'a été synchronisé ».
  Vérifier la cohérence des trois champs entre
  `solenoid_profile_defaults.h` et le fichier Clavier ; la divergence
  préexistante `pull_duty_pct` (50 côté Clavier, 100 côté spec/repli) est à
  signaler au Documentaliste, pas à trancher ici.
* **IMP-006**: Tests hôte : round-trip 66 octets, refus des longueurs 57 et 65,
  vecteur CRC, règles `impact_duration_us <= impact_max_us`,
  `impact_duty_pct <= duty_max_pct`, désactivation par 0, ordre de la table,
  `p set`/`p show`/`p diff`/`p send`. Mettre à jour
  `tests/tools/gen_profile_inject.c` et `tests/tools/moteur_swd_profile.ps1`
  (taille, nombre de chunks, CRC).
* **IMP-007**: Test manuel : oscilloscope sur un canal, vérifier la marche de
  duty (100 % pendant la durée réglée, puis duty PULL), mesurer le courant
  IPROPI, balayer 0/500/800/1500/2000 µs via `p set`, vérifier un Note Off
  pendant IMPACT, un Note On refusé `BUSY`, la persistance après coupure, et
  qu'un auto-test remplace bien un profil réglé avec un message explicite
  (ADR-0015).
* **IMP-008**: Documentation : `docs/specs/profil-solenoides.md` (§2 champs,
  §3 validation, §5 version flash, diagramme), `docs/specs/CAN.md`
  (`payload_len` 66, 11 chunks), `docs/procedure-profil-solenoides.md`,
  `docs/specs/Roadmap.md` (diagramme de la Phase 2 et Phase 6) et la doc du
  module Moteur. Le Documentaliste fige les offsets exacts du payload v2.
* **IMP-009**: Déploiement : reflasher les cinq Moteurs et le Clavier dans la
  même opération, puis synchroniser. Si une carte reste en v1, le Clavier doit
  afficher un message explicite « version de profil non supportée — reflasher
  la carte » en s'appuyant sur le résultat 7, pas un échec générique.
* **IMP-010**: ADR-0015 D2 n'est pas concernée : si le transfert volatil est
  accepté plus tard, il transporte le payload v2 sans changement de champ.

### References
* **REF-001**: [ADR-0013 — Profil d'activation centralisé sur le Clavier](./adr-0013-profil-activation-solenoides-centralise-clavier.md) (§1 tableau des champs, §3 payload 57 octets)
* **REF-002**: [ADR-0014 — Limites d'activation pilotées par le profil](./adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md) (§3 contrôles structurels, aucun seuil de sécurité)
* **REF-003**: [ADR-0015 — Réglage à chaud du profil par la console CDC](./adr-0015-reglage-chaud-profil-console-cdc.md) (D1 implémentée, D2 non implémentée)
* **REF-004**: [ADR-0001 — HAL solénoïde DRV8874](./adr-0001-hal-solenoide-drv8874-phen.md) (mode PH/EN, polarités)
* **REF-005**: [ADR-0007 — Limite HOLD à 20 s](./adr-0007-hold-limite-20s.md) et [ADR-0012 — MAINTENANCE en polarité inverse](./adr-0012-mode-maintenance-force-inverse-duree-20s.md) (chemins non concernés par IMPACT)
* **REF-006**: `docs/specs/profil-solenoides.md` — format, validation, persistance
* **REF-007**: Datasheet DRV8874 (PWM, IPROPI, tWAKE)
