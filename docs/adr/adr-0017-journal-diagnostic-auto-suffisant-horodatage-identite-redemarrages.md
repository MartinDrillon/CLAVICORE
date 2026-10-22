---
title: "ADR-0017: Journal de diagnostic auto-suffisant — horodatage, identité du firmware, redémarrages de carte et durée mesurée"
status: "Accepted"
date: "2026-10-21"
authors: "Agent Architecte"
tags: ["architecture", "diagnostic", "firmware"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

Amende ADR-0016 (IMP-010, dont la formulation est inobservable) et étend le
format de journal fixé par ADR-0005. Aucune dérogation à un invariant : les
décisions ci-dessous **renforcent** l'invariant 1 (un redémarrage de carte n'est
jamais toléré en silence). Aucune trame CAN de `shared/protocol/` n'est modifiée :
tout se passe dans le journal du Clavier.

### Context

Trois incidents successifs, le même jour (2026-10-21), ont produit des journaux
**identiques pour des causes opposées**. À chaque fois, l'analyse a dû deviner ce
que le journal ne disait pas, et une première conclusion s'est révélée fausse :
la piste « firmware Moteur ancien » a été écartée à la main par l'opérateur, la
piste « attribution par tag » a été invalidée par un test, et le correctif
d'horodatage livré n'a pas pu être confirmé comme effectivement flashé.

Quatre manques, tous constatés sur des journaux réels :

1. **Aucun horodatage.** Rien ne permet de distinguer un échec immédiat d'un
   échec après expiration du délai. Or ce sont deux causes opposées : le défaut
   d'armement d'échéance (corrigé) échoue dans la milliseconde, un vrai timeout
   échoue après 1 000 ms. Le message
   `[PROFILE] sync echec (delai %lu ms)` (`can_link.c:2022`) imprime le délai
   **configuré**, jamais le temps écoulé : il ne permet pas de trancher.
2. **Aucune identité du firmware du Clavier.** La ligne de boot ne porte que
   l'état de la table (`emit_boot_line` → `diag_format_boot_line`,
   `can_link.c:331-339`), jamais la version ni la date de compilation. Seules
   les **cartes** annoncent `fw=0.1.0` dans `[UID]` (`diag_log.c:122-125`). Il
   est donc impossible de savoir, depuis le journal, si le firmware qui tourne
   contient un correctif donné — situation vécue le 2026-10-21, où la
   persistance du défaut pouvait signifier « correctif absent du binaire flashé »
   ou « correctif présent, autre cause ».
3. **Un redémarrage de carte est invisible.** Le journal fourni contient
   `[UID] pos=2 uid=D3FEC94E4E95A1BB fw=0.1.0 proto=1 reply_to=0` **entre deux
   synchronisations échouées**. Comme `CAN_REPLY_TO_BOOT = 0` et
   `CAN_REQUEST_ID_MIN = 1` (`shared/protocol/can_protocol.h:84-86`), une réponse
   à une requête porte toujours un identifiant ≥ 1 : `reply_to=0` ne peut être
   qu'une **annonce de boot spontanée**, c'est-à-dire **le redémarrage de la
   carte de position 2**. Rien dans le journal ne le nomme : la ligne est
   indiscernable d'une annonce ordinaire, et l'opérateur comme l'agent y ont lu
   une simple détection. Une carte qui redémarre pendant une séquence de
   commissioning est un événement majeur (état des solénoïdes, profil, table) et
   doit être signalé (invariant 1).
4. **La source d'un déclenchement est mal nommée.**
   `emit_trigger_line` (`can_link.c:351`) imprime
   `[TRIGGER] bouton Moteur recu -> auto-test lance` sur le chemin **console**
   aussi : le journal de l'opérateur montre cette ligne juste après `[CONSOLE] t`.
   Le texte affirme un fait faux, et masque le chemin réellement emprunté — celui
   qui est précisément en cause dans le défaut d'armement d'échéance.

Cinquième point, structurel : **la carte Moteur n'a ni USB ni console UART**
(`tests/build_moteur_windows.md` §6). ADR-0016 IMP-010 demandait « une ligne au
`PROFILE_COMMIT` côté Moteur » : elle ne serait lisible par personne. Toute
observabilité du Moteur doit passer par le Clavier, soit par ce qu'il reçoit,
soit par les trames de compte rendu déjà existantes.

Le besoin n'est pas « plus de messages », c'est que **le journal suffise à
distinguer les causes sans hypothèse** : ce qui tourne, quand, et qui a
redémarré.

### Decision

#### 1. Horodatage monotone de chaque ligne

Chaque ligne du journal CDC est préfixée par un horodatage monotone en
millisecondes depuis le démarrage, de forme `[T+123456 ms] [PROFILE] …` :

* source unique : `to_ms_since_boot(get_absolute_time())` (la même que
  `now_ms()`), résolution 1 ms ;
* préfixe **uniforme**, placé à un seul point d'émission, jamais dans chaque
  formateur ;
* le contenu après le préfixe reste **exactement** le format `[TAG] clé=valeur`
  d'ADR-0005 : les procédures et les tests qui reconnaissent `[PROFILE]`,
  `[SELFTEST]`, `[STATUS]` continuent de fonctionner ;
* le rebouclage du compteur 32 bits (≈ 49,7 jours) est documenté, pas corrigé.

#### 2. Identité du firmware du Clavier au boot

Une ligne unique, émise avant toute autre ligne de diagnostic :

```
[BOOT] fw=MAJOR.MINOR.PATCH proto=N build=<identifiant> table=<etat> cards=<n>
```

`build` identifie le binaire effectivement flashé (horodatage de compilation ou
identifiant fourni par le build ; voir IMP-002). Ce champ sert **uniquement** à
vérifier ce qui tourne : le firmware ne l'interprète jamais, et il n'entre dans
aucune décision.

#### 3. Durée mesurée des opérations de contrôle

Le compte rendu de synchronisation du profil porte la durée **mesurée**, en plus
du délai configuré :

```
[PROFILE] sync echec duree=<ms> delai=<ms> acquittements=k/N
[PROFILE] sync ok duree=<ms> tag=0x........
```

Le délai configuré est conservé pour la lisibilité, mais il ne peut plus être
confondu avec le temps écoulé.

#### 4. Un redémarrage de carte est nommé

Une annonce de boot spontanée (`reply_to = CAN_REPLY_TO_BOOT`) reçue **hors d'un
scan de boot en cours** est un redémarrage de carte. Elle produit :

* `[CARD] pos=N uid=<uid> state=REBOOT` ;
* `[STATUS] code=CARD_REBOOTED pos=N` (nouveau code `0x37`) ;
* un compteur de diagnostic dédié.

Pendant le scan de boot initial du Clavier, `reply_to = CAN_REPLY_TO_BOOT` est
normal et ne produit aucune alarme. Une carte qui redémarre **pendant** ce scan
n'est pas distinguable d'une carte présente au démarrage : limite assumée et
documentée, pas de détection opportuniste.

#### 5. La source du déclenchement est nommée

Le déclenchement d'auto-test nomme sa provenance réelle :

```
[TRIGGER] source=console req=R -> auto-test lance
[TRIGGER] source=bouton req=R -> auto-test lance
```

Les lignes de refus conservent leur cause, et précisent la source. Une commande
console ne s'annonce jamais comme un bouton Moteur.

#### 6. Après un échec de synchronisation, le Clavier dit ce que chaque carte détient

En cas d'échec (acquittement manquant), le Clavier émet **une** requête d'UID
(trame `UID_REQUEST` existante, ADR-0003), puis rend compte, par carte, du profil
**réellement actif** annoncé par son `PROFILE_STATUS` de boot : `tag` et
`source`. Ces réponses servent uniquement au compte rendu : elles ne sont
**jamais** comptées comme acquittements du transfert qui vient d'échouer.

Cela remplace ADR-0016 IMP-010 (journalisation côté Moteur, inobservable) et
répond à la seule question qui restait ouverte dans les journaux : « la carte
a-t-elle appliqué le profil malgré l'acquittement perdu ? » — sans nouvelle
trame, sans flash, sans hypothèse.

#### 7. Ce qui ne change pas

* Aucune trame de `shared/protocol/` n'est modifiée ; `CAN_PROTO_VERSION` reste 1.
* Aucune décision de sécurité, de courant ou de durée d'activation n'est touchée
  (invariants 4 et 6 d'ADR-0014 intacts).
* Aucun élargissement des délais : le comportement d'échec reste celui décidé par
  ADR-0013 §4 et ADR-0015. Cette ADR rend les échecs **lisibles**, elle ne les
  supprime pas.
* Aucune correction silencieuse : un redémarrage, une divergence de profil ou un
  acquittement manquant restent signalés (invariant 1).

### Consequences

#### Positive
* **POS-001**: Les deux causes opposées du 2026-10-21 (échec immédiat d'échéance
  vs timeout réel) deviennent distinguables **à la lecture du journal**, sans
  instrument et sans hypothèse.
* **POS-002**: On peut vérifier, depuis le journal seul, quel firmware tourne — ce
  qui manquait pour valider un correctif livré et flashé.
* **POS-003**: Un redémarrage de carte cesse d'être confondu avec une détection
  normale : le fait est nommé, daté et compté.
* **POS-004**: La question « la carte a-t-elle appliqué le profil ? » reçoit une
  réponse mesurée chez le Moteur, alors que celui-ci n'a aucune console.
* **POS-005**: Le journal devient exploitable par un agent sans dialogue : les
  procédures `tests/` peuvent exiger des critères temporels (`duree=`), et non
  plus « ça a l'air immédiat ».

#### Negative
* **NEG-001**: Toutes les procédures et exemples de journaux de `tests/` et
  `docs/specs/` devront être mis à jour : le préfixe `[T+… ms]` apparaît sur
  **chaque** ligne (Documentaliste).
* **NEG-002**: Les formateurs de `diag_log.c` sont purs et testés sur hôte : le
  préfixe ne doit pas y être ajouté sous peine de casser ces tests et de dupliquer
  l'horodatage. Le point d'injection doit être unique et non contourné.
* **NEG-003**: Le journal grossit d'environ 14 octets par ligne ; à 115 200 bauds
  équivalents sur CDC, l'impact est négligeable, sauf pendant `[SELFTEST_RAW]`
  (24 lignes par carte) où il faut vérifier l'absence de perte.
* **NEG-004**: La re-requête après échec ajoute des trames sur le bus au moment
  précis où quelque chose va mal ; elle est bornée (une requête, après échec
  seulement) et doit rester sans effet sur l'état.
* **NEG-005**: Un identifiant de build fondé sur `__DATE__`/`__TIME__` rend le
  binaire non reproductible octet pour octet ; c'est le prix de la traçabilité si
  aucune définition de build n'est fournie.

### Alternatives Considered

#### Console série (UART) de debug sur la carte Moteur
* **ALT-001**: **Description**: ajouter un UART ou un second CDC sur les cartes
  Moteur pour journaliser localement `PROFILE_COMMIT`, l'écriture flash et les
  redémarrages.
* **ALT-002**: **Rejection Reason**: la carte a été conçue sans port USB et ses
  broches sont affectées ; cela imposerait une révision matérielle et un
  déplacement d'un agent sur le banc pour chaque incident. Le Clavier possède
  déjà le CDC et voit tout ce qui compte.

#### Horodatage transporté dans les trames CAN
* **ALT-003**: **Description**: ajouter un champ d'horodatage aux trames de
  diagnostic pour corréler les deux extrémités.
* **ALT-004**: **Rejection Reason**: modification d'une interface partagée pour un
  besoin purement local, sans horloge commune fiable entre cartes (les bases de
  temps sont indépendantes). L'ordre et la date des trames **vues par le Clavier**
  suffisent aux décisions.

#### Horodatage absolu (`time_us_64`, date/heure)
* **ALT-005**: **Description**: imprimer une date et une heure réelles.
* **ALT-006**: **Rejection Reason**: le Clavier n'a pas d'horloge persistante et
  le formatage 64 bits coûte cher sur ce chemin d'émission. Un compteur monotone
  en millisecondes répond à la seule question posée : « combien de temps entre
  deux lignes ».

#### Détection du redémarrage par le compteur d'annonces de la carte
* **ALT-007**: **Description**: faire envoyer par la carte un numéro de
  démarrage (boot count) dans son annonce, pour repérer un redémarrage même
  pendant le scan initial.
* **ALT-008**: **Rejection Reason**: ajout d'un champ dans une trame partagée
  (`0x101`) et dans la persistance Moteur pour couvrir un cas marginal (reset
  pendant la fenêtre de scan). Rouvrable si un redémarrage pendant le scan est
  réellement observé.

#### Laisser l'opérateur noter l'heure et le firmware flashé
* **ALT-009**: **Description**: consigner dans un cahier de banc la version
  flashée et l'heure de chaque essai.
* **ALT-010**: **Rejection Reason**: c'est précisément ce qui a échoué : la
  chronologie du 2026-10-21 n'a pas pu être reconstituée, et un agent ne peut pas
  s'appuyer sur un carnet.

### Implementation Notes
* **IMP-001**: Point d'injection unique de l'horodatage. `emit_line`
  (`can_link.c:187-191`) est le bon endroit, mais il n'est **pas** le seul chemin :
  plusieurs lignes sont émises par `printf` direct (`can_link.c:891`, `:960`,
  `:2022`, plus les `[SELFTEST_RAW]`). Introduire un `diag_emit(format, …)`
  unique, y router **toutes** les émissions, et vérifier qu'aucun `printf` de
  diagnostic ne subsiste dans `can_link.c`.
* **IMP-002**: `diag_log.c` reste **pur** (aucune horloge, aucun accès matériel) :
  les tests hôte compilent ce fichier tel quel. L'identité de build est une
  chaîne fournie par le build (`CLAVIER_BUILD_ID` via CMake, à défaut
  `__DATE__ " " __TIME__`), exposée à `diag_format_boot_line` comme paramètre
  supplémentaire.
* **IMP-003**: Réserver le code `STATUS` `0x37` (`CARD_REBOOTED`) après `0x35` et
  `0x36` d'ADR-0016, et les ajouter au même tableau de libellés.
* **IMP-004**: La fenêtre « scan de boot en cours » s'appuie sur l'état existant
  (`uid_inflight && uid_request_mode == UID_REQUEST_MODE_BOOT_SCAN`). Vérifier
  qu'une annonce spontanée **pendant** un auto-test (carte qui redémarre au milieu
  d'une mesure) produit bien `CARD_REBOOTED` et non une simple ligne `[UID]`.
* **IMP-005**: `duree=` se calcule avec `deadline_reached`/`now_ms()` et le même
  `first_ms` de passe que les échéances (ADR-0016 décision 7) : ne pas
  réintroduire de soustraction non signée.
* **IMP-006**: La re-requête après échec ne doit ni ouvrir une fenêtre
  d'acquittement, ni modifier `profile_sync_received_position`, ni retarder le
  compte rendu. Elle est émise après l'impression de l'échec, et ses réponses ont
  `transfer_id = CAN_PROFILE_TRANSFER_ID_BOOT` : les router vers une ligne de
  compte rendu distincte (`[PROFILE] pos=N detient tag=0x… source=…`).
* **IMP-007**: Mettre à jour les exemples de `tests/phase3b_bout_en_bout.md`,
  `tests/phase3b_profil_tuning.md`, `tests/ipropi_selftest_phase3b.md` et
  `docs/specs/CAN.md` avec le préfixe `[T+… ms]` et les nouvelles lignes
  (Documentaliste), puis vérifier que `run_host_tests.ps1` ne dépend pas du
  texte exact des lignes formatées.
* **IMP-008**: Mesurer au banc le débit du CDC avec `[SELFTEST_RAW]` horodaté
  (24 lignes × cartes) : si des lignes manquent, l'horodatage doit rester mais
  l'émission doit être regroupée.

### References
* **REF-001**: [ADR-0005 — Affichage diagnostic sur journal série CDC](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) (format `[TAG] clé=valeur` étendu ici par un préfixe)
* **REF-002**: [ADR-0016 — Attribution et diagnostic de la synchronisation du profil](./adr-0016-attribution-diagnostic-synchronisation-profil.md) (IMP-010 reformulé par la décision 6 ; codes `0x35`/`0x36`)
* **REF-003**: [ADR-0013 — Profil d'activation centralisé sur le Clavier](./adr-0013-profil-activation-solenoides-centralise-clavier.md) (§4 arrêt de l'auto-test sans acquittement, §6 `PROFILE_STATUS` de boot)
* **REF-004**: [ADR-0015 — Réglage à chaud du profil par la console CDC](./adr-0015-reglage-chaud-profil-console-cdc.md) (chemin console, NEG-002)
* **REF-005**: [ADR-0003 — Bring-up CAN Phase 3](./adr-0003-bringup-can-phase3-lecture-uid-sysex.md) (`UID_REQUEST`, touche GET_UID)
* **REF-006**: `shared/protocol/can_protocol.h:84-86` — `CAN_REPLY_TO_BOOT = 0`, `CAN_REQUEST_ID_MIN = 1` (preuve qu'un `reply_to=0` est une annonce de boot spontanée)
* **REF-007**: `firmware/clavier/src/can_link.c` — `emit_line` (187-191), `emit_trigger_line` (351), `emit_uid_report` (639), `emit_boot_line` (331-339), `[PROFILE] sync debut` (891), `[PROFILE] refus` (960), `[PROFILE] sync echec` (2022)
* **REF-008**: `firmware/clavier/src/diag_log.c:122-125` — ligne `[UID]` (seule identité de firmware existante, côté carte)
* **REF-009**: `tests/build_moteur_windows.md` §6 — la carte Moteur n'a ni USB ni console UART
