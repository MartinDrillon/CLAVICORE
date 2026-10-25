---
title: "ADR-0014: Limites d'activation des solénoïdes entièrement pilotées par le profil Clavier — dérogation à l'invariant 4"
status: "Accepted"
date: "2026-10-21"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "hardware", "protocol"]
supersedes: ""
superseded_by: ""
---

### Status
**Dérogation à un invariant — VALIDÉ**

L'invariant 4 de `AGENTS.md` est dérogé : les limites électriques restent
appliquées par la couche pilote du Moteur, mais **leurs valeurs ne sont plus
bornées par le firmware**. Elles proviennent intégralement du profil compilé
dans le firmware Clavier (ADR-0013), dans les deux sens, sans plafond ni
plancher validé. Cette dérogation est demandée explicitement par l'utilisateur
le 2026-10-21 ; elle ne devient effective qu'au passage de cette ADR en
`Accepted`, et l'invariant 4 d'`AGENTS.md` devra alors être reformulé pour
renvoyer ici.

**Accepted**

**Amendé le 2026-10-25** — voir « Amendement 2026-10-25 » en fin de document :
`impact_max_us`, `pull_max_us` et `probe_max_us` sont supprimés du profil ; le
format passe en v3 (54 octets). Les autres limites de l'ADR sont inchangées.

### Context

ADR-0013 organise la migration de tous les paramètres d'activation vers un
fichier unique côté Clavier, transmis aux cartes Moteur à l'auto-test IPROPI et
persisté en flash. Dans sa première rédaction, elle conservait côté Moteur une
**enveloppe de sécurité fixe** : HOLD limité à 20 s et 25 % de duty, PULL à
50 ms, PROBE à 10 ms et 20 %, MAINTENANCE à 20 s et à un budget thermique de
1 000 000 ms·%. Le fichier Clavier ne pouvait que réduire ces valeurs ou
augmenter les temps de repos.

L'utilisateur rejette cette restriction : **toutes** les valeurs, y compris les
maxima et les valeurs validées au banc, doivent être révisables depuis le
fichier Clavier, dans un sens comme dans l'autre, sans reflasher les cartes
Moteur.

C'est une dérogation réelle à l'invariant 4, qui exige qu'un solénoïde ne
puisse jamais être activé hors de sa fenêtre de courant/durée (2–3 A, bobine
sur mesure) sans coupure matérielle ou logicielle explicite, et qui ne prévoit
que deux dérogations bornées (ADR-0006/0007). Le mécanisme de coupure reste :
le pilote coupe toujours la phase HOLD à `hold_max_ms`, la MAINTENANCE à
`maint_max_ms`, applique les plafonds de duty du profil et les temps de repos.
Mais **les points de consigne de ces coupures deviennent des données** : le
firmware ne garantit plus qu'ils restent dans une fenêtre thermiquement sûre.

Ce qui est conservé :

* les coupures logicielles explicites (HOLD, MAINTENANCE, plafonds de duty,
  repos) et la lecture du pin FAULT du DRV8874 ;
* l'application des limites dans la couche pilote, jamais dans la couche métier ;
* l'atomicité du profil (CRC, version, `COMMIT`), sa persistance A/B, ses
  valeurs de repli et le signalement des divergences (ADR-0013) ;
* les valeurs actuellement validées au banc comme **valeurs initiales** du
  fichier Clavier, et comme valeurs de repli du Moteur.

Ce qui est perdu :

* la garantie firmware que le profil reste dans la fenêtre 2–3 A / durée
  validée. Un profil structurellement valide mais thermiquement dangereux
  (par exemple HOLD à 100 % pendant dix minutes) sera obéi ;
* les `_Static_assert` et contrôles d'enveloppe qui empêchaient une régression
  de réglage de compiler ou d'être appliquée.

La sécurité devient **procédurale** : elle repose sur la validation humaine du
fichier de réglage, sur les essais au banc et sur la trace du profil actif
(tag CRC32, journal CDC), plus sur les protections matérielles existantes
(alimentation 24 V/20 A, pin FAULT). Elle n'est plus garantie par une borne
logicielle fixe.

### Decision

#### 1. Aucune valeur limite n'est figée dans le firmware Moteur

Le fichier `solenoid_profile_limits.h` prévu par ADR-0013 est supprimé du
concept. Le Moteur ne conserve **aucun** seuil numérique de sécurité : ni
durée maximale, ni duty maximal, ni budget thermique, ni repos minimal.

Tous les paramètres deviennent des champs du profil, révisables sans limite
directionnelle :

* `pull_max_us`, `hold_max_ms`, `hold_duty_max_pct`, `probe_max_us`,
  `probe_duty_max_pct`, `maint_max_ms`, `maint_duty_max_pct`,
  `maint_min_rest_ms`, `min_rest_ms`, `duty_max_pct` ;
* les valeurs opératoires correspondantes (`pull_duration_us`,
  `pull_duty_pct`, etc.) ;
* `dir_safety_delay_us`, `wake_delay_us`, `pwm_freq_hz` et les niveaux de
  polarité.

Le budget thermique dérivé `maint_max_ms * maint_duty_pct <= 1 000 000` est
**supprimé** : il serait auto-référentiel puisque ses deux termes sont
désormais pilotés par le même fichier. `maint_max_ms` et `maint_duty_pct` font
foi directement.

#### 2. Élargissement des types pour supprimer les plafonds arbitraires

Les durées passent de `u16` à `u32` (µs pour PULL/RELEASE/PROBE, ms pour
HOLD/MAINTENANCE/repos) afin qu'aucun plafond de 65 ms ou 65 s ne subsiste par
le seul choix du type. Le payload canonique v1 passe de 39 à **57 octets** ;
il est découpé en **10 chunks** de 6 octets (9×6 + 3). Le `format_version`
reste 1 : ADR-0013 n'est pas encore implémentée, sa table est révisée avant
tout codage.

Seule contrainte de représentabilité conservée : les durées en ms doivent
rester convertibles en µs sur 32 bits, soit `<= 4 000 000 ms` (environ 66 min),
parce que le driver compare des instants en µs `u32`. Ce n'est pas une limite
thermique, c'est une limite de type.

#### 3. Contrôles structurels uniquement

Le Moteur rejette un profil (sans jamais l'écrêter) lorsque les contrôles
suivants échouent. Aucun ne fixe un seuil de sécurité :

* duties `0..100 %` ; `hold_duty_pct <= hold_duty_max_pct`,
  `maint_duty_pct <= maint_duty_max_pct`,
  `probe_duty_pct <= probe_duty_max_pct`, et
  `duty_max_pct >=` chaque duty de phase ;
* `pull_duration_us <= pull_max_us`, `probe_duration_us <= probe_max_us` ;
* durées PULL/RELEASE `>= 0` (0 ignore la phase) ; PROBE strictement positive
  et conforme au contrôle de fenêtre de mesure ;
* `dir_level_pull != dir_level_release`, niveaux ∈ {0,1} ;
* `hold_max_ms`, `maint_max_ms`, `maint_min_rest_ms`, `min_rest_ms`
  convertibles en µs `u32` (voir §2) ;
* `pwm_freq_hz` dans la plage réalisable par le DRV8874 (`1..100 000`), la
  fenêtre d'échantillonnage ADC couvrant au moins une période PWM, et
  `probe_duration_us > fenêtre ADC + marge` (contrôle fonctionnel de mesure,
  pas de sécurité) ;
* longueur, version et CRC du payload (ADR-0013).

Un profil qui échoue est refusé en bloc ; l'ancien profil reste actif. Un
profil qui passe est appliqué **tel quel**, y compris s'il est thermiquement
dangereux.

#### 4. Gouvernance de la sécurité

* Les valeurs initiales du fichier Clavier restent celles validées au banc
  (HOLD 20 s / 25 %, PULL 15 ms / 100 %, PROBE 5 ms / 20 %, MAINTENANCE 20 s /
  50 %). Elles ne sont plus des bornes.
* Toute modification du fichier est un acte humain, tracé par le CRC32 du
  profil et le journal CDC. Le Clavier signale toujours une divergence ou
  l'usage des valeurs de repli (invariant 1).
* L'auto-test IPROPI reste le seul moment de propagation : un profil modifié
  n'est actif qu'après un test explicite.
* Une protection matérielle de dernier recours (fusible, limitation de courant
  du DRV8874 si elle est caractérisée) est **recommandée** mais n'est pas
  exigée par cette ADR ; elle ne remplacerait pas les coupures de durée.

### Consequences

#### Positive
* **POS-001**: Le fichier Clavier contrôle réellement **toutes** les valeurs,
  dans les deux sens, sans reflash des cartes Moteur ni plafond arbitraire.
* **POS-002**: Les valeurs validées au banc restent les valeurs par défaut, ce
  qui préserve le comportement connu tant que l'utilisateur ne les change pas.
* **POS-003**: Les mécanismes d'atomicité, de persistance, de version et de
  signalement d'ADR-0013 sont inchangés : la liberté de réglage n'ajoute aucune
  correction silencieuse.
* **POS-004**: Un besoin mécanique ou électrique légitime découvert au banc
  (durée de PULL plus longue, HOLD à duty plus élevé, PWM plus basse) n'est
  plus bloqué par une constante compilée.

#### Negative
* **NEG-001**: Perte de la garantie thermique firmware. Un profil cohérent mais
  dangereux (HOLD 10 min à 100 %, MAINTENANCE 1 h à 100 %) est obéi et peut
  détruire une bobine, endommager un DRV8874 ou créer un risque incendie.
* **NEG-002**: La protection devient procédurale : elle dépend de la relecture
  humaine du fichier et des essais au banc, pas d'une borne logicielle.
* **NEG-003**: La fenêtre 2–3 A de l'invariant 4 n'est plus vérifiée par le
  firmware ; elle n'est qu'une intention documentée dans les valeurs initiales.
* **NEG-004**: Les tests et `_Static_assert` qui garantissaient l'enveloppe
  disparaissent ; une régression de réglage ne sera plus arrêtée à la
  compilation.
* **NEG-005**: Les validations de ADR-0007 (HOLD 20 s / 25 %) et ADR-0012
  (MAINTENANCE 20 s / budget) ne sont plus des garanties : leurs valeurs
  deviennent des défauts, et toute documentation qui les présente comme des
  limites doit être corrigée.
* **NEG-006**: Le payload passe de 39 à 57 octets et de 7 à 10 chunks ; le
  temps de synchronisation et la surface de test augmentent légèrement.

### Alternatives Considered

#### Conserver une enveloppe de sécurité fixe côté Moteur
* **ALT-001**: **Description**: garder les maxima validés (20 s, 25 %, 50 ms,
  10 ms, budget 1 000 000) comme bornes non contournables, le fichier Clavier
  ne pouvant que les réduire.
* **ALT-002**: **Rejection Reason**: contredit directement la demande
  (« toutes les valeurs », « sans limite dans un sens ou dans l'autre ») et
  empêcherait tout essai au-delà des valeurs actuelles sans reflasher cinq
  cartes. Le risque est accepté via la présente dérogation.

#### Ne borner que le haut (plafond global) et laisser le bas libre
* **ALT-003**: **Description**: conserver un plafond absolu de sécurité, par
  exemple 60 s de HOLD ou 100 ms de PULL, tout en laissant le fichier évoluer
  en dessous.
* **ALT-004**: **Rejection Reason**: reste une limite arbitraire non demandée ;
  l'utilisateur veut pouvoir réviser aussi les maxima. Un plafond choisi sans
  données thermiques nouvelles serait aussi arbitraire que l'ancien.

#### Protection uniquement matérielle, sans coupure logicielle configurable
* **ALT-005**: **Description**: supprimer les coupures HOLD/MAINTENANCE et
  compter sur un fusible ou la limitation de courant du driver.
* **ALT-006**: **Rejection Reason**: une limitation de courant ne protège pas
  contre une surchauffe à courant moyen élevé sur la durée, et un fusible est
  trop lent pour une bobine. Les coupures logicielles restent nécessaires ;
  seule la valeur de leur point de consigne est libérée.

#### Déverrouillage conditionné par un cavalier ou un bouton physique
* **ALT-007**: **Description**: conserver l'enveloppe par défaut et exiger une
  action matérielle (cavalier, appui long) pour la dépasser.
* **ALT-008**: **Rejection Reason**: ajoute du matériel et une procédure sur
  cinq cartes, alors que l'objectif est un unique fichier et un seul firmware
  à flasher. La validation humaine du fichier est le point de contrôle retenu.

### Implementation Notes

* **IMP-001**: Supprimer `solenoid_profile_limits.h` et les `_Static_assert`
  d'enveloppe de `solenoid_driver.c:24-33`. Conserver les asserts de
  représentabilité et de cohérence structurelle.
* **IMP-002**: Déplacer les contrôles structurels dans
  `shared/protocol/solenoid_profile.c` pour que le Clavier les applique avant
  l'envoi et le Moteur à la réception, avec les mêmes règles. Aucun écrêtage :
  rejet et code d'erreur.
* **IMP-003**: Passer les durées à `u32` dans la structure et le codec ;
  mettre à jour la taille du payload (57 octets), le nombre de chunks (10) et
  les tests hôte d'ADR-0013.
* **IMP-004**: Supprimer le contrôle `maint_max_ms * maint_duty_pct <= budget`
  et la constante `SOLENOID_MAINT_ENERGY_BUDGET_MS_PCT`. Vérifier qu'aucune
  documentation ni test ne le référence encore.
* **IMP-005**: Le driver continue de couper à `hold_max_ms` et `maint_max_ms`
  et d'appliquer `duty_max_pct` ; ces valeurs sont lues dans le profil actif,
  jamais dans une constante compilée.
* **IMP-006**: Journaliser au boot et à chaque synchronisation le profil actif
  complet (ou au minimum ses champs critiques et son CRC32) en CDC, pour rendre
  une valeur dangereuse traçable après incident.
* **IMP-007**: Mettre à jour `AGENTS.md` (invariant 4) pour renvoyer à cette
  ADR. Sans cette mise à jour, les agents continueront d'appliquer l'ancienne
  règle et refuseront les profils hors enveloppe.
* **IMP-008**: Corriger `docs/specs/CAN.md`, les docs de module et ADR-0007 /
  ADR-0012 pour présenter leurs valeurs comme des défauts, plus comme des
  limites.
* **IMP-009**: Tests hôte : valeurs extrêmes (0, 1, `UINT32_MAX`),
  incohérences structurelles, absence d'écrêtage, rejet en bloc, conservation
  de l'ancien profil. Test manuel : un profil volontairement long mais sûr
  (par exemple HOLD 30 s à 25 %) doit être appliqué et coupé à 30 s.
* **IMP-010**: Étudier, hors de cette ADR, une protection matérielle de dernier
  recours (fusible thermique ou limitation de courant caractérisée). Elle est
  recommandée, pas exigée.

### References
* **REF-001**: [ADR-0013 — Profil d'activation centralisé sur le Clavier](./adr-0013-profil-activation-solenoides-centralise-clavier.md)
* **REF-002**: [ADR-0006 — Mode maintenance push-only](./adr-0006-mode-maintenance-push-only.md)
* **REF-003**: [ADR-0007 — Limite HOLD à 20 s](./adr-0007-hold-limite-20s.md)
* **REF-004**: [ADR-0012 — Maintenance en polarité inverse](./adr-0012-mode-maintenance-force-inverse-duree-20s.md)
* **REF-005**: `AGENTS.md`, invariant 4 — limites électriques dans la couche pilote
* **REF-006**: Datasheet DRV8874 — courant, FAULT, R_DS(on), tWAKE
* **REF-007**: `docs/specs/CAN.md` — trames 0x420–0x423

---

## Amendement 2026-10-25 — Suppression des maxima de durée redondants (`*_max_us`)

### Status de l'amendement

**Accepted** — demandé explicitement par l'utilisateur le 2026-10-25
(« ne supprime que les vrais doublons »). Amendement d'une décision déjà actée :
la dérogation de l'invariant 4 décrite ci-dessus n'est ni élargie ni retirée,
elle s'applique à un profil qui contient simplement trois champs de moins.

### Context

Trois champs du profil ne font que doubler un autre champ du même profil :

| Champ borné par un doublon | Durée qui le rend redondant | Usage |
|---|---|---|
| `impact_max_us` (2000 µs) | `impact_duration_us` | écrêtage + rejet `ERR_CONSISTENCY` |
| `pull_max_us` (50 000 µs) | `pull_duration_us` | écrêtage + rejet `ERR_CONSISTENCY` |
| `probe_max_us` | `probe_duration_us` | écrêtage + rejet `ERR_CONSISTENCY` |

Depuis ADR-0014, ces maxima sont eux-mêmes des données du fichier Clavier et
ne bornent plus rien que l'utilisateur ne puisse déjà changer. Conséquence
pratique : pour allonger une impulsion, l'utilisateur doit modifier **deux**
valeurs du même fichier, et une durée réglée seule est refusée en bloc au
décodage. Le champ de durée est déjà l'unique point de consigne ; le maximum
n'apporte aucune sécurité supplémentaire, seulement une seconde occasion de se
tromper.

Ne sont **pas** concernés, car ils ne doublent aucun champ et sont l'unique
point de coupure d'une phase sans durée propre : `hold_max_ms`,
`maint_max_ms` (et le `MAINTENANCE_TIMEOUT_MS` du Clavier, qui reste un
doublon à traiter séparément), ainsi que les temps de repos
`maint_min_rest_ms` et `min_rest_ms`, qui ne sont pas des temps d'activation.

### Decision

#### 1. Trois champs supprimés du profil

`impact_max_us`, `pull_max_us` et `probe_max_us` disparaissent de
`shared/protocol/solenoid_profile.h` (struct, table d'offsets, codec,
validation), des valeurs par défaut du Moteur et du Clavier, de la table de
réglage console (`g_fields[]`) et des tests hôte.

#### 2. La durée devient seule autorité

* Plus aucun écrêtage : `impact_duration_us`, `pull_duration_us` et
  `probe_duration_us` sont appliquées telles que reçues.
* Plus aucun rejet de cohérence sur ces trois durées : le profil qui porte une
  durée longue est accepté et obéi, exactement comme un `hold_max_ms` long
  l'est déjà depuis ADR-0014.
* Subsistent les seuls contrôles de **représentabilité** et de cohérence qui ne
  dépendent pas des champs supprimés : conversion µs `u32`, `probe_duration_us`
  strictement positive et couvrant la fenêtre ADC (§3 ci-dessus), niveaux de
  polarité, largeur de payload, CRC.

#### 3. Format du profil : version 3, 54 octets

Les trois champs retirés font 12 octets : la taille du payload canonique passe
de 66 à **54 octets** (9 chunks de 6 octets, au lieu de 10+). `format_version`
passe à **3** et `SOLENOID_PROFILE_FLASH_VERSION` à **3** avec `payload_len`
attendu à 54.

Un Moteur encore en v2 refuse le nouveau profil avec
`CAN_PROFILE_RESULT_UNSUPPORTED_VERSION` (7) ; le Clavier le signale déjà en
CDC. Un clavier encore en v2 est refusé de la même manière par le Moteur v3.
Les deux firmwares doivent donc être reflashés ensemble.

#### 4. Ce qui ne change pas

`hold_max_ms`, `maint_max_ms`, `maint_min_rest_ms`, `min_rest_ms`,
`*_duty_max_pct`, `duty_max_pct`, `dir_safety_delay_us`, `wake_delay_us`,
`pwm_freq_hz`, les niveaux de polarité, l'atomicité (CRC, `COMMIT`),
la persistance A/B et le repli sur défauts restent identiques.

### Consequences

#### Positive

* **POS-005**: Un seul nombre à changer pour allonger ou raccourcir une
  impulsion IMPACT/PULL/PROBE ; le fichier de réglage cesse de porter des
  valeurs qui doivent rester cohérentes entre elles.
* **POS-006**: Le refus « durée > max » disparaît : un réglage légitime au banc
  n'est plus bloqué par une valeur que l'utilisateur avait lui-même posée.
* **POS-007**: Le payload perd 12 octets et un chunk ; la table de réglage
  console passe de 27 à 24 champs.

#### Negative

* **NEG-007**: Une durée PULL/IMPACT/PROBE réglée trop longue n'est plus
  arrêtée par le firmware ; seule la conversion en µs `u32` la borne
  (durées en µs : pas de borne de type pratique). C'est la protection
  procédurale d'ADR-0014 qui s'applique, sans second filet.
* **NEG-008**: Rupture de compatibilité du profil : cartes Moteur à reflasher
  en même temps que le Clavier. Un profil v2 déjà en flash n'est plus relu et
  le Moteur repart sur ses valeurs de repli (`source=DEFAULTS`, signalé en CDC)
  jusqu'au prochain transfert de profil.
* **NEG-009**: `p set impact_max_us|pull_max_us|probe_max_us` disparaît de la
  console ; toute procédure de test qui les cite doit être corrigée.

### Implementation Notes

* **IMP-011**: Retirer les trois champs de la struct `solenoid_profile_t`, de
  la table d'offsets documentée et des deux sens du codec
  (`shared/protocol/solenoid_profile.c`), puis renuméroter les offsets
  (54 octets, LE, un octet `format_version` en tête).
* **IMP-012**: `SOLENOID_PROFILE_FORMAT_VERSION` → 3 ; `SOLENOID_PROFILE_WIRE_SIZE`
  → 54 ; `SOLENOID_PROFILE_FLASH_VERSION` → 3 et contrôle `payload_len == 54`
  dans `solenoid_profile_store.c`.
* **IMP-013**: Supprimer les trois écrêtages du pilote
  (`solenoid_driver.c`, fonctions `impact_duration_us`/`pull_duration_us`/
  `probe`), en conservant l'écrêtage des duties.
* **IMP-014**: Supprimer les trois comparaisons `*_duration <= *_max` de
  `solenoid_profile_validate` ; conserver tous les autres contrôles et ne rien
  remplacer par un nouveau seuil.
* **IMP-015**: Retirer les trois entrées de `g_fields[]` côté Clavier et les
  trois valeurs des défauts (`solenoid_profile_config.h`,
  `solenoid_profile_defaults.h`).
* **IMP-016**: Mettre à jour le message de refus de version côté Clavier pour
  annoncer « profil v3 requis » au lieu de v2.
* **IMP-017**: Journaliser `format_version` dans la ligne de profil actif au
  boot Moteur et au boot Clavier, pour que la cause d'un refus de version soit
  lisible en CDC sans reflash à l'aveugle.
* **IMP-018**: Adapter les tests hôte : supprimer les cas « dépassement de
  max » et les entrées de console associées, ajouter un cas « durée longue
  acceptée et appliquée telle quelle », vérifier la taille 54 et le rejet d'un
  payload v2.
* **IMP-019**: Mettre à jour `docs/specs/profil-solenoides.md`,
  `docs/specs/CAN.md` (table des offsets, taille, version) et les procédures de
  test qui citent `impact_max_us`/`pull_max_us`.

### References (amendement)

* **REF-008**: [ADR-0013 — Profil d'activation centralisé sur le Clavier](./adr-0013-profil-activation-solenoides-centralise-clavier.md)
* **REF-009**: [ADR-0019 — Phase IMPACT, impulsion courte avant PULL](./adr-0019-phase-impact-impulsion-courte-avant-pull.md)
* **REF-010**: `AGENTS.md`, invariant 5 — tout changement d'interface partagée passe par une ADR
