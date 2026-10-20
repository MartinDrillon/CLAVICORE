---
title: "ADR-0005: Affichage diagnostic sur journal série CDC, en remplacement du SysEx"
status: "Accepted"
date: "2026-10-18"
authors: "Agent Architecte (rédigé sur décision humaine)"
tags: ["architecture", "firmware", "usb", "diagnostic"]
supersedes: "ADR-0003 §6 (partiel), addendum SysEx d'ADR-0004 (partiel)"
superseded_by: ""
---

### Status
**Accepted**

Décision humaine du 2026-10-18 : **plus aucun SysEx de diagnostic**. Les trames CAN décidées par ADR-0003 et ADR-0004 ne changent pas.

### Context

Le dépôt dispose déjà d'un périphérique USB composite **MIDI + CDC** et d'un journal texte `usb_log` lisible avec un terminal série. Les messages SysEx de diagnostic avaient ajouté un encodage spécifique, un état d'émission dans `midi.c`, des tests dédiés et une procédure hôte dépendante de MIDI-OX, alors que le besoin réel est un affichage lisible par humain pour :

* relever les UID ;
* afficher les `STATUS` symboliques ;
* afficher les rapports d'auto-test ;
* signaler explicitement les divergences et anomalies ;
* conserver le MIDI USB pour le jeu, sans faire transiter le diagnostic par ce canal.

Les invariants d'`AGENTS.md` restent inchangés : pas de remappage silencieux, cartes Moteur anonymes, table de commissioning côté Clavier, limites électriques au niveau pilote, pas de changement d'interface partagée CAN sans ADR.

### Decision

#### 1. Canal d'affichage

Tout l'affichage diagnostic du Clavier passe désormais par le **port série virtuel USB CDC existant**.

* Lecture côté PC : **PuTTY** (ou terminal équivalent) en mode **Serial**.
* Le débit configuré dans PuTTY est **sans importance fonctionnelle** : le CDC ACM ne dépend pas d'un baudrate matériel.
* Le périphérique USB reste **composite MIDI + CDC**.
* **Aucun changement de descripteur USB** n'est requis par cette décision : l'interface composite existe déjà et reste telle quelle.

#### 2. Format des lignes

Le diagnostic est un **journal texte ligne par ligne**. Une ligne suit le format :

`[TAG] cle=valeur cle=valeur ...`

Formats figés :

* UID :
  * `[UID] pos=4 uid=A3FA4C99CA526F4B fw=0.1.0 proto=1 reply_to=0`
* STATUS sans bitmap :
  * `[STATUS] code=BOARD_MISSING pos=2`
  * `[STATUS] code=NO_RESPONSE`
  * `[STATUS] code=SELFTEST_STARTED`
* STATUS avec bitmap :
  * `[STATUS] code=BOARD_REFUSED_BUSY pos=4 bitmap=0x007`
* Rapport d'auto-test :
  * `[SELFTEST] req=5 pos=4 status=OK bitmap=0x007 faults=0x000 tag=29B1`
* Rapport brut de calibration :
  * `[SELFTEST_RAW] req=5 pos=4 ch=2 idle=18 probe=79`
  * `[SELFTEST_RAW] req=5 pos=4 ch=2 delta=61`
* Mode maintenance :
  * `[MODE] maintenance=ON`
  * `[PUSH] midi=84 key=49 pos=4 ch=3 start`

Règles de format :

* `code=` reprend **à l'identique** les noms symboliques des codes `STATUS` d'ADR-0003 / ADR-0004.
* `uid=` est l'UID sur **16 chiffres hexadécimaux majuscules**, directement, **sans encodage par nibbles**.
* `bitmap=` et `faults=` sont affichés en hexadécimal sur **3 chiffres** (`0x000` à `0xFFF`).
* `tag=` est affiché en hexadécimal majuscule sur **4 chiffres**.
* `reply_to=0` désigne une annonce de boot ; `reply_to=1..127` désigne une réponse à requête.
* `SELFTEST_STARTED` est un code **sans carte** : il ne porte plus `pos=127`.
* Les lignes `[SELFTEST_RAW]` sont réservées au mode calibration demandé par
  `IPROPI_SELFTEST_REQUEST` avec son bit 7 actif.
* Les codes STATUS s'étendent aussi à `MAINTENANCE_BUSY` et
  `MAINTENANCE_NO_TABLE` (référence sémantique : ADR-0004, ADR-0006).

#### 3. Contraintes d'exécution

* Le journal CDC est **strictement non bloquant**.
* Les écritures de diagnostic ne partent **jamais depuis une IRQ**.
* Le formatage des lignes est réalisé dans un module **pur et testable sur PC**.
* L'émission effective passe par `usb_log` / `printf()` dans la boucle principale.
* Si l'anneau du journal CDC est plein, la perte est **autorisée mais comptée**.
* Les compteurs de diagnostic existants restent conservés.

#### 4. Périmètre retiré

Sont retirés du chemin de diagnostic :

* `UID_REPORT` SysEx ;
* `STATUS` SysEx ;
* `SELFTEST_REPORT` SysEx ;
* `midi_send_sysex` et l'état « SysEx en cours » dans `midi.c`.

Le **MIDI USB notes** reste inchangé pour le jeu.

### Consequences

#### Positive
* Diagnostic lisible directement dans un terminal texte, sans décodage hexadécimal.
* Plus d'interaction entre messages de diagnostic et émission MIDI de notes.
* Helpers de formatage plus simples à tester sur PC.

#### Negative
* Les procédures de test et les habitudes d'outillage doivent migrer de MIDI-OX vers PuTTY.
* Le diagnostic CDC dépend d'un hôte ouvrant le port COM du périphérique composite.

### Alternatives Considered

#### Conserver SysEx pour le diagnostic
* **Description**: garder le canal MIDI pour l'affichage UID/STATUS/SELFTEST.
* **Rejection Reason**: plus complexe à lire, à tester et à faire coexister avec les notes ; décision humaine contraire.

#### Basculer tout l'USB en CDC et supprimer le MIDI
* **Description**: n'exposer qu'un port série.
* **Rejection Reason**: le MIDI USB reste nécessaire pour le jeu ; seule la partie diagnostic change.

### Implementation Notes
* `usb_log` reste la seule sortie diagnostic USB texte.
* `CFG_TUD_MIDI_TX_BUFSIZE` peut revenir à **64** si aucune autre contrainte ne dépend de l'ancien surdimensionnement pour SysEx.
* Les documents opératoires doivent parler du **port COM du périphérique composite** (à vérifier dans le Gestionnaire de périphériques Windows).

### Addendum — suivi pas-à-pas du commissioning

Pour le workflow utilisateur « ajout d'une carte / ajout d'un canal puis bouton
Moteur 3 s », le journal CDC ajoute des lignes **supplémentaires** sans changer
les formats existants `[UID]`, `[STATUS]`, `[SELFTEST]`, `[SELFTEST_RAW]` et
`[TABLE]` :

* Boot :
  * `[BOOT] table=loaded|absent|invalid cards=N`
  * `[HELP] bouton Moteur 3 s = auto-test ; table rechargee depuis la flash`
* Déclenchement :
  * `[TRIGGER] bouton Moteur recu -> auto-test lance (req=R)`
  * ou `[TRIGGER] bouton Moteur recu -> refus: ...`
* Maintenance :
  * `[MODE] maintenance=ON|OFF`
  * `[PUSH] midi=M key=K pos=P ch=C start|stop|timeout`
* Fin de session d'auto-test :
  * une ligne `[CARD] pos=P node=N uid=... state=OK|MISSING|NOT_OK bitmap=0x... channels=... keys=... notes=...`
  * une ligne `[CARD_UNKNOWN] uid=...` par carte hors `uid_position.h`
  * une ligne `[DIFF] pos=P ancien=0x... nouveau=0x...` par divergence de bitmap
  * une ligne `[SUMMARY] cards_ok=X/Y channels=Z keys=Z/49 missing_keys=... table=WRITTEN|NOT_WRITTEN(...)|UNCHANGED|DIFFERS_WRITTEN`

Les noms de notes sont formatés côté Clavier par une fonction pure testée sur
PC, selon ADR-0004 (`DO1 = MIDI 36` ... `DO5 = MIDI 84`).

### Questions ouvertes
Aucune pour cette décision.

### References
* `AGENTS.md`
* `firmware/clavier/src/usb_log.c`
* `firmware/clavier/src/usb_log.h`
* `docs/adr/adr-0003-bringup-can-phase3-lecture-uid-sysex.md`
* `docs/adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md`
