---
title: "ADR-0022: Précisions après le premier banc — acquittement répété inconditionnellement, refus de tag non signalé, appui court renommé"
status: "Superseded"
date: "2026-10-24"
authors: "Agent Architecte"
tags: ["architecture", "diagnostic", "firmware"]
supersedes: ""
superseded_by: "0023, 0025"
---

### Status
**Superseded**

ADR non implémentée, remplacée avant acceptation : sa décision 1 est remplacée
par ADR-0023 (reprise complète et réconciliation d'état) ; ses décisions 2 et 3
sont reprises par ADR-0025 (politique de journal). Ce remplacement documentaire
est immédiat ; l'implémentation des ADR remplaçantes attend leur propre passage à
`Accepted`. Le contenu ci-dessous est conservé pour l'historique et ne doit pas
être implémenté isolément.

### Context

Premier essai au banc du lot du 2026-10-23 (Clavier et cartes reflashés),
journal du Clavier, extraits (préfixe `[T+… ms]`) :

```text
[PROFILE] sync debut but=reglage tag=0x3E00A733 attendus=2 source=console
[PROFILE] sync echec duree=2000 ms delai=2000 ms acquittements=1/2
[PROFILE] sync carte pos=0 tag=0x456B motif=ACK_TIMEOUT
[PROFILE] sync carte pos=2 tag=0x9D5C motif=PROFILE_STATUS_SENT_OK
[PROFILE] pos=0 detient tag=0x3E00A733 source=NVM
[PROFILE] pos=2 detient tag=0x3E00A733 source=NVM
...
[CARD] pos=0 event=BUTTON_SHORT_PRESS ch=0 state=1 fault=0x000
[CARD] pos=0 event=TRIGGER_PENDING ch=- state=0 fault=0x000    (3 s plus tard)
[CARD] pos=0 event=TRIGGER_SENT ch=- state=0 fault=0x000
[PROFILE] sync ok duree=2 ms acquittements=2/2 tag=0x3E00A733
...
[CARD] pos=2 event=COMMAND_IGNORED ch=0 raison=TAG            (DO2 joué par pos=0)
```

Trois constats, tous vérifiables dans le code :

**(1) L'acquittement perdu n'est pas couvert par la répétition actuelle.**
Les deux cartes détiennent le profil en NVM (`detient … source=NVM`) : le
transfert a été reçu, validé et écrit par **les deux**. Seul l'acquittement de
pos=0 manque. Or `send_profile_status_if_due`
(`firmware/moteur/src/can_link.c:431-458`) ne réémet **que** si `send_frame`
échoue (`profile_status_attempts`) : dès qu'une émission est **acceptée par
can2040**, la carte considère l'acquittement émis et s'arrête
(`profile_status_pending = false`). Une trame acceptée puis **perdue sur le bus**
(ou non reçue par le Clavier) n'est donc jamais réémise — exactement la trace
observée, sur le premier transfert après reflash (celui qui exige une écriture
flash de 40 à 70 ms).

**(2) Un refus de tag n'est pas une anomalie, c'est le routage normal.**
`SOLENOID_COMMAND` est diffusée ; chaque carte la reçoit. Le Clavier l'adresse à
une carte précise par `uid_tag`. Les **autres** cartes la refusent légitimement
(`command.uid_tag != board_tag`) — et l'implémentation actuelle en émet un
`CARD_EVENT` `COMMAND_IGNORED` (`can_link.c:137-141`, `report_ignored_command`).
Conséquence : **chaque note produit N−1 fausses lignes** « commande ignorée ».
Sur le banc à deux cartes, une seule ligne parasite par note ; à cinq cartes,
quatre. Le journal devient inutilisable précisément parce qu'il fonctionne.
Le compteur `solenoid_ignored_tag`, lui, reste utile et sans bruit.

**(3) « Appui court » est un nom faux pour un appui long.**
L'événement 0 est émis au **front montant** de l'appui (`main.c:135-148`), au
moment où la carte démarre réellement son test manuel. Un appui maintenu 3 s
produit donc `BUTTON_SHORT_PRESS` **puis** `TRIGGER_PENDING` : le journal donne
l'impression de deux gestes contradictoires alors qu'il décrit correctement deux
actions successives (test manuel démarré, puis déclenchement).

Le reste du journal est conforme et sans surprise : `attendus=2`, `source=`,
`duree=`, événements de bouton, `SELFTEST` OK des deux cartes, table
`PROVISIONAL` `keys=1-2`, `[SUMMARY]` cohérent.

### Decision

#### 1. L'acquittement de profil est répété **inconditionnellement**

Le Moteur émet son `PROFILE_STATUS` **3 fois**, espacées de 20 ms, dans la
fenêtre d'acquittement du Clavier (1 000 / 2 000 ms), **que l'émission ait été
acceptée ou non** par `can2040_transmit`. Le Clavier est déjà idempotent
(ADR-0016 : `profile_card_ack[]`, `profile_sync_received` comptent une fois par
carte).

* contenu identique à chaque émission (même `transfer_id`, même `result`) ;
* `stats.profile_status_sent` compte les émissions **acceptées** ;
* `stats.profile_status_lost` n'est incrémenté que si **aucune** des trois n'a pu
  être transmise (refus de `can2040_transmit`) — la carte ne peut pas savoir si
  le Clavier a reçu.

Motif : la politique actuelle (« réessayer seulement si la file refuse ») ne
protège que des refus de file, pas des pertes de trame, alors que c'est la perte
de trame que le banc a mise en évidence. Coût : 2 × 8 octets par carte et par
synchronisation, négligeable.

#### 2. Un `uid_tag` étranger ne produit aucun événement

Une `SOLENOID_COMMAND` dont l'`uid_tag` n'est pas celui de la carte est **le
routage normal en diffusion** : elle est ignorée, `solenoid_ignored_tag` est
incrémenté, et **aucun `CARD_EVENT` n'est émis**.

Restent signalés (inchangé) les refus d'une commande **adressée à la carte** :
auto-test en cours, canal hors bornes, canal non occupé, refus du pilote. Le
principe est simple : on signale ce que la carte **aurait dû** exécuter, jamais
ce qui ne lui était pas destiné.

#### 3. L'événement 0 est renommé `MANUAL_TEST_STARTED`

La valeur numérique `0` et l'octet `event` ne changent pas ; seul le **nom**
change (énumération, libellé du journal, documentation) :
`event=MANUAL_TEST_STARTED`. Il décrit l'action réellement effectuée au front
montant. Un appui long produit donc `MANUAL_TEST_STARTED` puis `TRIGGER_PENDING`
puis `TRIGGER_SENT` : c'est la séquence réelle, pas une anomalie.

#### 4. Ce qui ne change pas

* Aucune trame de `shared/protocol/` n'est modifiée : IDs, DLC et octets de
  `0x424`/`0x423` inchangés ; `CAN_PROTO_VERSION` reste 1.
* Aucune limite d'activation, aucun délai de sécurité, aucun mapping (invariants
  1 à 6).
* Les motifs `COMMAND_IGNORED` restants (`AUTOTEST`, `NON_OCCUPE`, `PILOTE`,
  `CANAL`) et leur limiteur 1/s par raison restent en vigueur.
* `m stats` expose toujours `ignored_tag` : une **vraie** erreur d'adressage
  reste mesurable, même si elle ne produit plus de ligne.

### Consequences

#### Positive
* **POS-001**: La classe « la carte a appliqué mais l'acquittement s'est perdu »
  est couverte pour les pertes de trame, pas seulement pour les refus de file :
  le premier transfert après reflash (celui qui écrit la flash) ne devrait plus
  échouer.
* **POS-002**: Le journal redevient exploitable : plus de fausses lignes
  `COMMAND_IGNORED` proportionnelles au nombre de cartes, donc les vraies
  (auto-test, canal non occupé, pilote) ressortent.
* **POS-003**: Les noms d'événements décrivent les actions réelles ; un appui
  long ne se lit plus comme deux gestes contradictoires.
* **POS-004**: Aucun changement d'interface : ni les cartes ni le Clavier ne
  changent de format, seul le firmware est reflashé.

#### Negative
* **NEG-001**: Chaque synchronisation de profil émet 3 acquittements au lieu
  d'un (trafic marginal, mais réel).
* **NEG-002**: `ignored_tag` n'est plus visible en ligne : une erreur réelle
  d'adressage ne se voit que par `m stats` (compteur) ou par l'absence de jeu.
  Acceptable : c'est précisément le bruit qui masquait le reste.
* **NEG-003**: Le renommage impose de mettre à jour la documentation et les
  procédures de banc (Documentaliste) et le test 7.1 de
  `tests/phase3b_bout_en_bout.md`.
* **NEG-004**: Les cartes doivent être reflashées pour bénéficier de (1) et (2) ;
  un mélange ancien/nouveau reste compatible sur le bus mais conserve le bruit.

### Alternatives Considered

#### Répéter l'acquittement seulement si la file TX refuse (comportement actuel)
* **ALT-001**: **Description**: conserver `send_profile_status_if_due` tel quel.
* **ALT-002**: **Rejection Reason**: c'est ce que le banc vient d'invalider :
  l'émission est acceptée, la trame se perd, aucune répétition n'a lieu.

#### Filtrer le bruit de tag côté Clavier plutôt que côté carte
* **ALT-003**: **Description**: le Clavier ignore les `COMMAND_IGNORED` dont le
  `uid_tag` diffère de la cible de la dernière commande.
* **ALT-004**: **Rejection Reason**: il faudrait que le Clavier mémorise la cible
  courante et corrèle des événements asynchrones ; le bruit resterait sur le bus
  et le critère serait fragile (plusieurs notes simultanées vers des cartes
  différentes). La carte sait, elle, à qui la commande s'adressait.

#### Renommer l'événement sans changer son moment d'émission
* **ALT-005**: **Description**: garder l'émission au front montant sous le nom
  `BUTTON_SHORT_PRESS`, en documentant que l'appui long l'émet aussi.
* **ALT-006**: **Rejection Reason**: un nom qui contredit le geste observé est un
  piège de diagnostic ; c'est le nom qui doit changer, pas la documentation pour
  l'excuser.

#### Acquittement confirmé par une trame Clavier → carte
* **ALT-007**: **Description**: ajouter un accusé de réception de l'accusé de
  réception.
* **ALT-008**: **Rejection Reason**: nouvelle trame et nouvel état pour un besoin
  couvert par l'idempotence du Clavier et une répétition bornée ; contraire à
  l'économie d'interface.

### Implementation Notes
* **IMP-001**: Moteur `send_profile_status_if_due` : remplacer l'arrêt sur succès
  par un compteur d'émissions ; réémettre à `PROFILE_STATUS_RETRY_US` (20 ms)
  tant que le compteur n'a pas atteint 3, indépendamment du résultat de
  `send_frame`. Comptabiliser `profile_status_sent` par émission acceptée et
  `profile_status_lost` uniquement si zéro émission acceptée.
* **IMP-002**: Moteur `handle_solenoid_command` : sur `uid_tag` étranger,
  incrémenter `solenoid_ignored_tag` et **retourner sans** appeler
  `report_ignored_command`. Ne pas toucher aux autres refus.
* **IMP-003**: Clavier `diag_log.c` : renommer la chaîne de l'événement 0 en
  `MANUAL_TEST_STARTED` ; mettre à jour le test hôte qui vérifie les libellés.
* **IMP-004**: Vérifier qu'aucun autre code ne dépend du libellé
  `BUTTON_SHORT_PRESS` (recherche globale, y compris tests et docs).
* **IMP-005**: Documentaliste : `docs/specs/CAN.md` (tableau des événements),
  `docs/specs/impact-adr-0018.md` (§2), `tests/phase3b_bout_en_bout.md` test 7.1
  et 7.3 (retirer l'attente de `COMMAND_IGNORED` sur tag étranger, ajouter la
  note « une commande adressée à une autre carte n'est pas signalée »).
* **IMP-006**: Banc de contrôle : reflasher les cartes, provoquer une écriture de
  profil (`p set` puis `p send`) et vérifier qu'aucun `PROFILE_SYNC_FAILED`
  n'apparaît ; jouer toutes les touches et vérifier qu'aucune ligne
  `COMMAND_IGNORED raison=TAG` n'apparaît ; vérifier `m stats` → `ignored_tag`
  augmente encore (routage normal), sans ligne.

### References
* **REF-001**: [ADR-0020 — Comptabilité des échecs des cartes](./adr-0020-comptabilite-echecs-cartes-moteur.md) (décision 1 répétition, §3 commande ignorée)
* **REF-002**: [ADR-0018 — Trame CARD_EVENT](./adr-0018-retour-etat-bouton-canaux-moteur-vers-clavier.md) (§2 événement 0, §5 réception Clavier)
* **REF-003**: [ADR-0016 — Attribution et diagnostic de la synchronisation du profil](./adr-0016-attribution-diagnostic-synchronisation-profil.md) (idempotence des acquittements)
* **REF-004**: `firmware/moteur/src/can_link.c` — `send_profile_status_if_due` (431-458), `handle_solenoid_command` (137-141), `report_ignored_command`
* **REF-005**: `firmware/moteur/src/main.c` — événement 0 au front montant (135-148)
* **REF-006**: `docs/specs/impact-adr-0018.md` §2 (vocabulaire des événements), `docs/specs/CAN.md` (trame `0x424`)
