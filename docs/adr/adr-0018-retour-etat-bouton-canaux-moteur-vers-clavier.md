---
title: "ADR-0018: Retour d'état du bouton et des canaux Moteur vers le Clavier — trame CARD_EVENT (0x424)"
status: "Accepted"
date: "2026-10-22"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

> **Interface partagée modifiée — validation humaine requise.** Cette ADR ajoute
> une trame à `shared/protocol/` (`0x424`, DLC 8). L'invariant 5 est respecté en
> écrivant cette ADR **avant** l'implémentation, mais l'interface partagée change :
> aucune implémentation ne doit démarrer avant passage en `Accepted`.

Aucune dérogation aux invariants 1 à 4 et 6 : la trame **informe**, elle ne
commande rien, ne remappe rien et ne relâche aucune limite.

### Context

Trois sessions de banc ont été perdues sur la même question : « le test IPROPI
échoue et le journal ne dit rien ». Le dernier protocole d'essai, fourni par
l'opérateur le 2026-10-22, est décisif parce qu'il est reproductible :

1. mise hors tension des cartes, puis remise sous tension ;
2. test IPROPI par **appui sur le bouton de test** → échec, **aucune ligne** dans
   le journal du Clavier ;
3. appui sur le bouton **RUN** de la carte de position 2 ;
4. nouveau test par appui sur le bouton de test → **réussite**
   (`[PROFILE] sync ok duree=2 ms acquittements=2/2`, auto-test complet).

Le journal correspondant ne contient, entre la remise sous tension
(`[CARD] pos=2 … state=REBOOT`, `[CARD] pos=0 … state=REBOOT`, `CARD_REBOOTED`)
et le test réussi, **aucune trace de tentative**. Le Clavier n'a donc rien reçu à
l'étape 2 — pas un échec de synchronisation : **aucun déclenchement `0x402`**.

La cause est dans le firmware Moteur (`firmware/moteur/src/main.c:101-103`) : le
bouton a **trois** comportements, et deux d'entre eux sont muets côté Clavier.

| Geste | Ce que fait la carte | Visible côté Clavier |
|---|---|---|
| appui **court** (< 3 s) | `note_on` sur le **test manuel** du canal 0 ou 11, en alternance (`main.c:135-143`) — un solénoïde est réellement alimenté | **rien** |
| appui **long** ≥ 3 s | `note_off` du canal de test, puis `selftest_trigger_pending = true` (`ipropi_selftest.h:11`, `main.c:146-152`) | rien tant que `0x402` n'est pas émis |
| `0x402` émis | seulement quand `all_channels_idle(drivers)` (`main.c:168-170`) | `[PROFILE] sync debut … source=bouton` |

Or `all_channels_idle()` (`main.c:50-57`) exige l'état `IDLE` sur **les douze**
canaux. Un seul canal en `FAULT` (NFAULT verrouillé) ou resté en `HOLD` rend donc
l'auto-test **définitivement impossible depuis cette carte**, en silence. Le
chemin de sortie existe — l'appui court remet à zéro un défaut — mais uniquement
pour les canaux 0 et 11, et sans que personne ne le sache.

Ainsi, trois causes très différentes produisent **le même observable vide** :
l'opérateur a appuyé trop brièvement ; la carte attend un retour `IDLE` qui ne
viendra pas ; la carte est figée. L'opérateur, lui, ne voit qu'« échec ».

Ce que les décisions existantes ne couvrent pas :

* ADR-0017 décision 4 détecte les **redémarrages** (annonce de boot spontanée) —
  une carte vivante mais bloquée n'annonce rien ;
* ADR-0009 / ADR-0003 ne couvrent que la **présence au scan de boot** — l'état
  interne d'une carte après le scan est invisible ;
* ADR-0013 §4 conserve le déclenchement conditionné à l'acquittement du profil,
  mais ne dit rien de l'état local des canaux qui bloque `0x402` en amont ;
* la carte Moteur n'a **ni USB ni console UART** (ADR-0017) : ses
  `printf` (`main.c:170`, `:177`) ne sont lisibles par personne.

L'invariant 1 est en cause : des actions physiques (alimentation d'un solénoïde
par le test manuel, défaut verrouillé) se produisent **sans que le Clavier, seule
source de vérité du jeu, n'en soit informé**.

### Decision

#### 1. Une trame de compte rendu d'événements, Moteur → Clavier

Nouvelle trame standard `CAN_ID_CARD_EVENT = 0x424`, DLC 8, émise par une carte
de sa propre initiative, sur le modèle de `PROFILE_STATUS` (`0x423`) :

```
[0]   proto_version
[1]   event
[2]   channel     (0..11, ou 0xFF si sans objet)
[3]   state       (valeurs SOLENOID_STATE_*)
[4]   fault       (valeurs SOLENOID_FAULT_*)
[5-6] uid_tag     (CRC16 de l'UID de la carte, little-endian)
[7]   reserved    = 0
```

Priorité : famille `0x4xx` (contrôle), au repos seulement — les événements sont
rares (sur geste ou transition), jamais périodiques.

#### 2. Vocabulaire des événements (v1)

| Valeur | Event | Sens | Attendu côté Clavier |
|---|---|---|---|
| 0 | `BUTTON_SHORT_PRESS` | appui court : test manuel du canal `channel` démarré (solénoïde alimenté) | ligne `[CARD] … event=APPUI_COURT_TEST_MANUEL ch=N` |
| 1 | `TRIGGER_PENDING` | appui long consommé, `0x402` en attente d'un retour `IDLE` | ligne `[CARD] … event=DECLENCHEMENT_EN_ATTENTE` |
| 2 | `TRIGGER_SENT` | `0x402` transmis par la carte | ligne `[CARD] … event=DECLENCHEMENT_EMIS` |
| 3 | `TRIGGER_BLOCKED` | après un délai borné (≥ 200 ms), un canal empêche toujours l'émission : `channel`/`state`/`fault` désignent le bloqueur | ligne + `[STATUS] code=TRIGGER_BLOCKED pos=N` (`0x38`) |
| 4 | `CHANNEL_FAULT` | un driver a verrouillé un défaut **hors** auto-test | ligne + `[STATUS] code=CHANNEL_FAULT_IDLE pos=N` (`0x39`) |
| 5 | `CHANNEL_FAULT_CLEARED` | défaut levé par une action explicite | ligne |

Chaque valeur est émise **une fois par transition** ; aucun événement n'est
répété tant que l'état ne change pas.

#### 3. Attribution par UID, comme le reste du profil

Le Clavier attribue l'événement à une position via `uid_tag` et
`uid_position.h` — même règle qu'ADR-0016 décisions 1 et 3 (position **stockée**,
jamais un index de boucle, jamais un numéro de position supposé). Un `uid_tag`
inattribuable produit une ligne `[CARD] uid=<uid> state=NOT_OK` et
`[STATUS] code=UID_NOT_IN_TABLE` (`0x29`, comme pour une annonce inconnue) : il
n'est **jamais** confondu avec un acquittement de profil inattribuable
(`PROFILE_ACK_UNATTRIBUTED`, `0x35`), qui reste réservé à `0x423`.

#### 4. Aucun événement n'est un acquittement

Un `CARD_EVENT` ne compte **jamais** dans `profile_sync_received`, ne remplace
aucun `PROFILE_STATUS` et ne vaut pas réponse à une requête d'UID. ADR-0016
décision 4 (acquittement inattribuable) reste inchangée et ne s'applique qu'à
`0x423`.

#### 5. Les autres cartes ignorent la trame

`0x424` n'est pas une commande : le firmware Moteur la compte en
`rx_unknown_id` et ne fait rien. Aucun effet de bord entre cartes.

#### 6. Aucun changement de comportement

* Le déclenchement reste conditionné à `all_channels_idle()` : ADR-0013 §4 est
  intacte. On rend le blocage **visible**, on ne le supprime pas.
* L'appui court conserve son test manuel (utile pour une carte seule au banc) :
  il devient seulement **rapporté**.
* Le Clavier ne remet aucun défaut à zéro, ne relance rien, ne remappe rien
  (invariant 1). Il nomme, il n'agit pas.

#### 7. Hors périmètre : la carte morte

Une carte figée n'émet **rien**, donc cette trame ne la voit pas. Si ce cas se
reproduit (aucun événement, aucune réponse, aucun `CARD_REBOOTED`), une décision
distincte sera nécessaire — supervision de vivacité par interrogation périodique
au repos, ou chien de garde côté Moteur. À trancher avec des preuves, pas par
anticipation.

### Consequences

#### Positive
* **POS-001**: Les trois causes d'un « test qui ne démarre pas » deviennent
  distinguables dans le journal du Clavier : appui trop court, déclenchement
  bloqué (avec le canal fautif), carte muette.
* **POS-002**: Une action physique jusqu'ici invisible — l'alimentation d'un
  solénoïde par le test manuel — est consignée : invariant 1 renforcé, le Clavier
  reste la source de vérité du jeu.
* **POS-003**: Le cas `FAULT` verrouillé, qui rendait l'auto-test définitivement
  impossible depuis une carte sans le moindre message, est nommé.
* **POS-004**: L'attribution réutilise la mécanique d'ADR-0016 (UID → position
  stockée) : aucun second système de nommage n'apparaît.
* **POS-005**: Aucun risque fonctionnel : la trame est purement informative,
  émise sur transition, ignorée par les autres cartes.

#### Negative
* **NEG-001**: L'interface partagée s'étend définitivement : une trame et un
  vocabulaire d'événements à maintenir des deux côtés (Moteur, Clavier, docs,
  tests).
* **NEG-002**: Deux nouveaux codes `STATUS` (`0x38`, `0x39`) après `0x35`–`0x37`,
  dans une vocabulaire déjà dense (ADR-0017 NEG-004).
* **NEG-003**: Le firmware Moteur doit être reflashé pour que la trame existe :
  les cartes non reflashées restent muettes, et le Clavier ne doit pas présenter
  leur silence comme une preuve d'absence d'événement.
* **NEG-004**: Un appui court alimente brièvement un solénoïde : le journal le
  rapporte, mais ne l'empêche pas. Si cela se révèle gênant à l'usage, la
  suppression du test manuel fera l'objet d'une décision ultérieure.

### Alternatives Considered

#### Étendre `0x402` avec un octet de raison
* **ALT-001**: **Description**: la carte émet toujours `0x402`, avec un champ
  « prêt / bloqué par le canal N / défaut » ; le Clavier journalise la raison.
* **ALT-002**: **Rejection Reason**: ne couvre pas l'appui trop court — cas le
  plus probable de l'incident — puisqu'aucun déclenchement n'a lieu ; et modifie
  la sémantique d'une trame existante (ADR-0004/0009) pour un gain inférieur.

#### Supervision de vivacité côté Clavier (interrogation périodique)
* **ALT-003**: **Description**: le Clavier interroge les cartes toutes les 5 s au
  repos et signale celles qui ne répondent plus.
* **ALT-004**: **Rejection Reason**: ne distingue pas un appui trop court d'une
  carte vivante mais bloquée : précisément les deux cas observés. Reste un
  complément possible pour la carte figée (décision 7), pas un substitut.

#### Retirer le test manuel de l'appui court
* **ALT-005**: **Description**: le bouton ne fait plus qu'une chose : appui long
  3 s = auto-test ; l'appui court n'a plus d'effet.
* **ALT-006**: **Rejection Reason**: supprime une capacité de banc utile pour une
  carte seule, et ne règle pas le blocage silencieux (`TRIGGER_BLOCKED`) ni le
  défaut verrouillé. Compatible avec cette ADR si l'usage le justifie plus tard.

#### Retour local par LED ou buzzer sur la carte
* **ALT-007**: **Description**: la carte signale elle-même l'appui court et le
  blocage par un signal lumineux ou sonore.
* **ALT-008**: **Rejection Reason**: aucun journal, donc aucune trace
  exploitable après coup ni par un agent ; et révision matérielle.

#### Ne rien faire
* **ALT-009**: **Description**: documenter la durée d'appui de 3 s et renvoyer
  l'opérateur à la procédure.
* **ALT-010**: **Rejection Reason**: c'est l'état actuel, et il a coûté trois
  sessions de banc. Le silence reste indistinguishable entre erreur de geste,
  blocage et panne.

### Implementation Notes
* **IMP-001**: `shared/protocol/can_protocol.h` : `CAN_ID_CARD_EVENT 0x424u`,
  `CAN_DLC_CARD_EVENT 8u`, énumération `can_card_event_t`, structure
  `can_card_event_msg_t`. `can_protocol.c` : encodeur/décodeur avec les contrôles
  d'usage (event connu, `channel < CAN_CHANNEL_COUNT` ou `0xFF`, `uid_tag` non
  nul, `reserved == 0`), et tests hôte d'aller-retour (y compris rejets).
* **IMP-002**: Moteur — émission aux transitions : `main.c:135-143` (appui
  court → `BUTTON_SHORT_PRESS` avec le canal réellement sollicité), `:146-152`
  (`TRIGGER_PENDING`), `:168-170` (`TRIGGER_SENT`), et un détecteur borné
  (≥ 200 ms après `TRIGGER_PENDING` sans retour `IDLE`) qui émet `TRIGGER_BLOCKED`
  avec le premier canal non `IDLE` (état + défaut). `CHANNEL_FAULT` sur
  apparition d'un `SOLENOID_FAULT_*` hors auto-test, `CHANNEL_FAULT_CLEARED` sur
  sortie explicite. Un seul événement par transition, jamais en boucle.
* **IMP-003**: Clavier — `handle_frame` route `0x424` vers un
  `handle_card_event()` qui réutilise `profile_seed_index_for_tag()` (ADR-0016,
  déjà en place) et journalise via `diag_out_printf`. Ne toucher ni à
  `profile_sync_received`, ni à `uid_inflight_responses`.
* **IMP-004**: Codes `DIAG_STATUS_TRIGGER_BLOCKED = 0x38` et
  `DIAG_STATUS_CHANNEL_FAULT_IDLE = 0x39`, avec leurs libellés, après `0x37`.
* **IMP-005**: Ne pas oublier la compatibilité descendante : une carte qui
  n'émet pas `0x424` (firmware antérieur) n'est **pas** une preuve d'absence
  d'événement. Le journal doit rester interprétable sans elle (le silence garde
  son sens actuel : « rien reçu »), et la procédure doit dire de reflasher les
  cartes après acceptation.
* **IMP-006**: Ordre de mise en œuvre : attendre le correctif du double saut de
  ligne (`diag_out_printf`, défaut relevé par le Validateur) pour ne pas figer des
  exemples faux, puis implémenter cette trame, puis la passe documentaire.
* **IMP-007**: Documentaliste : `docs/specs/CAN.md` (trame `0x424`, événements,
  codes `0x38`/`0x39`), `tests/phase3b_bout_en_bout.md` (test « appui court » et
  test « déclenchement bloqué »), `tests/bringup_commissioning_provisoire.md`
  (ce que voit l'opérateur quand un canal est en défaut).

### References
* **REF-001**: [ADR-0004 — Auto-test IPROPI et SOLENOID_COMMAND](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) (trame de déclenchement `0x402`)
* **REF-002**: [ADR-0009 — Table de commissioning provisoire](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) (auto-test demandé, bouton maintenu 3 s)
* **REF-003**: [ADR-0013 — Profil centralisé sur le Clavier](./adr-0013-profil-activation-solenoides-centralise-clavier.md) (§4 déclenchement conditionné à l'acquittement du profil)
* **REF-004**: [ADR-0016 — Attribution et diagnostic de la synchronisation du profil](./adr-0016-attribution-diagnostic-synchronisation-profil.md) (UID → position, codes `0x35`–`0x37`)
* **REF-005**: [ADR-0017 — Journal de diagnostic auto-suffisant](./adr-0017-journal-diagnostic-auto-suffisant-horodatage-identite-redemarrages.md) (horodatage, `CARD_REBOOTED`, absence de console côté Moteur)
* **REF-006**: [ADR-0005 — Journal série CDC](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) (format `[TAG] clé=valeur`)
* **REF-007**: `firmware/moteur/src/main.c` — `all_channels_idle` (50-57), comportements du bouton (101-103), appui court → test manuel (135-143), appui long (146-152), émission `0x402` conditionnée (168-179)
* **REF-008**: `firmware/moteur/src/ipropi_selftest.h:11` — `IPROPI_SELFTEST_BUTTON_HOLD_US = 3 000 000`
* **REF-009**: `shared/protocol/can_protocol.h:26-49` — plan d'identifiants (0x420–0x423 occupés, 0x424 libre)
