---
title: "ADR-0020: Rendre les cartes Moteur comptables de leurs échecs — acquittement répété, compteurs lisibles, commandes ignorées signalées"
status: "Accepted"
date: "2026-10-22"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

Renumérotée de 0019 à **0020** le 2026-10-22 : le numéro 0019 était déjà pris
par « Phase IMPACT » (`adr-0019-phase-impact-impulsion-courte-avant-pull.md`,
`Accepted`), créée entre-temps. Aucun contenu n'a changé.

> **Interface partagée modifiée — validation humaine requise.** Décision 2 ajoute
> une paire requête/réponse au plan d'identifiants (`0x403` / `0x425`) ;
> décision 3 étend le vocabulaire de `CARD_EVENT` (`0x424`) proposé par ADR-0018.
> Aucune implémentation avant passage en `Accepted`, et ADR-0018 doit être
> acceptée avant la décision 3.

Aucune dérogation aux invariants 1 à 6 : aucune limite d'activation, aucun
délai, aucun mapping n'est touché. L'invariant 1 est renforcé (une carte ne peut
plus ignorer une commande en silence).

### Context

Incident du 2026-10-22, journal du Clavier (extraits, préfixe d'horodatage omis) :

```text
[PROFILE] sync debut but=reglage tag=0x42708A1E attendus=2 source=console
[PROFILE] sync echec duree=2000 ms delai=2000 ms acquittements=1/2
[PROFILE] sync carte pos=0 tag=0x456B motif=PROFILE_STATUS_SENT_OK
[PROFILE] sync carte pos=2 tag=0x9D5C motif=ACK_TIMEOUT
[PROFILE] pos=2 detient tag=0x42708A1E source=NVM     <- la carte A APPLIQUÉ
...
p send  -> 1/2, pos=2 ACK_TIMEOUT, puis detient tag=0x94C32F66 NVM
p send  -> 2/2, duree=2 ms
```

Quatre transferts échouent ainsi, en alternance sur l'une **ou** l'autre carte
(pos=2 deux fois, puis pos=0 deux fois). Deux faits sont décisifs :

1. Les échecs n'arrivent **que** sur un transfert qui exige une **écriture
   flash** (tag différent de celui détenu) ; les transferts à tag identique
   acquittent en **1 à 2 ms**. Le seul transfert avec écriture qui a réussi a
   pris **41 ms** — durée cohérente avec l'effacement + programmation d'un
   secteur de 4 Kio.
2. Après chaque échec, la sonde de re-requête (ADR-0017 décision 6) montre que
   la carte **détient le nouveau profil en NVM** : elle a donc reçu le transfert,
   l'a validé et l'a écrit. **Seul l'acquittement manque.**

Le Clavier, lui, est sain sur ce chemin : l'échéance a réellement couru
(`duree=delai`), l'attribution par UID est correcte (`motif=` nomme la bonne
carte), et aucune ligne `[PROFILE] refus` n'apparaît — donc aucun acquittement
non-OK n'a été reçu.

Second symptôme, rapporté par l'opérateur : après plusieurs minutes d'inactivité,
**les commandes de note ne sont plus exécutées** par les cartes ; il faut
plusieurs cycles « RUN + auto-test » pour que le jeu revienne. Là encore, le
journal ne dit rien.

**Complément du 2026-10-23 — l'angle mort est démontré.** L'opérateur précise
que, pendant la panne, une touche commissionnée (DO2, MIDI 36) **joue bien en
MIDI** mais n'actionne aucun solénoïde, et que **aucune ligne `[NOTE_DROPPED]`
n'apparaît** — ni en panne, ni lorsque la note est correctement jouée
(`CLAVIER_NOTE_LOG` vaut 0 par défaut). Le code permet de conclure sans
hypothèse :

* `send_note_command` (`firmware/clavier/src/can_link.c:2340-2382`) renseigne un
  motif d'abandon sur **toutes** ses sorties en échec (auto-test actif, table
  invalide, note non assignée, carte hors ligne, protocole, file TX pleine) ;
  `can_link_note_on` émet alors systématiquement `[NOTE_DROPPED]` (`:2407-2411`).
  **L'absence de cette ligne prouve que la trame a été acceptée par can2040**,
  donc émise vers le bus ;
* côté carte, `handle_solenoid_command` (`firmware/moteur/src/can_link.c:123-167`)
  refuse **silencieusement** cinq cas : tag différent, canal hors bornes,
  auto-test en cours, canal non occupé dans le bitmap **local**, et refus du
  pilote (BUSY/FAULT). Chacun n'incrémente qu'un compteur que personne ne lit.

Ainsi « la note est jouée en MIDI mais rien ne bouge et le journal est vide »
est **exactement** l'empreinte d'un refus côté carte : la commande est partie,
la carte l'a reçue, et l'a ignorée sans le dire. C'est l'angle mort que la
décision 3 comble — et il fallait aussi compter ce qui est **exécuté**, sinon
« transmis » et « exécuté » restent indistinguables.

Une piste envisagée a été **écartée par le code** : le bitmap local de la carte
n'est pas vide après un redémarrage — `can_link_init` l'arme à tous les canaux
(`:525-527`). Un redémarrage de carte ne désactive donc pas les notes par ce
biais ; les cinq refus silencieux ci-dessus restent les seules causes possibles.

Trois mécanismes logiciels, tous dans le firmware Moteur, expliquent ces deux
symptômes. Aucun n'est prouvé isolément — et **c'est précisément le problème** :

**(a) L'acquittement est émis une seule fois, sans répétition.**
`send_profile_status_if_due` (`firmware/moteur/src/can_link.c:431-444`) libère
`profile_status_pending` **avant** l'unique tentative :

```c
profile_status_pending = false;
if (can_encode_profile_status(...) != CAN_PROTO_OK ||
    !send_frame(CAN_ID_PROFILE_STATUS, ...)) {
    ++stats.tx_fail;
    return;              // perdu définitivement
}
```

`send_frame` échoue si `can2040_transmit()` refuse (file TX pleine : can2040
bufferise **quatre** trames, `docs/API.md:213`). Un refus momentané suffit à
perdre l'acquittement pour toujours, alors que la carte a bien appliqué le
profil. C'est exactement la trace observée.

**(b) L'écriture flash coupe les interruptions 40 à 70 ms.**
`profile_flash_write_cb` (`firmware/moteur/src/solenoid_profile_store.c:141-146`,
appelé via `flash_safe_execute` ligne 178) efface **et** programme un secteur
entier, interruptions coupées. Pendant ce temps la carte ne draine ni son IRQ
CAN, ni sa file RX (`RX_RING_SIZE 16`, `POLL_MAX_RX_ENTRIES 4`,
`can_link.c:15-17`). Toute trame qui arrive est perdue — et can2040 le signale
par `CAN2040_NOTIFY_ERROR`, que le firmware **compte et n'affiche jamais**
(`can_link.c:578-580`) :

```c
if ((entry.notify & CAN2040_NOTIFY_ERROR) != 0u) {
    ++stats.can_errors;
}
```

Point capital pour le diagnostic : d'après `firmware/common/can2040/docs/API.md:133-136`,
`CAN2040_NOTIFY_ERROR` ne signifie **pas** une erreur électrique de bus mais un
**débordement des tampons de réception internes** de can2040, donc des trames
logicielles perdues. Une valeur non nulle de `can_errors` côté carte
discriminerait immédiatement « bus électriquement mauvais » de « logiciel trop
lent ». Or cette valeur est aujourd'hui inobservable : la carte n'a **ni USB ni
console UART** (ADR-0017) et aucun compteur n'est transmis.

**(c) Une commande reçue et ignorée est indistinguable d'une commande perdue.**
`handle_solenoid_command` (`can_link.c:137-147`) refuse silencieusement sur tag
différent, canal invalide, ou **auto-test en cours** :

```c
if (ipropi_selftest_is_running()) {
    ++stats.solenoid_selftest_blocked;
    return;                     // aucune réponse, aucun journal
}
```

Or `ipropi_selftest_is_running()` vaut `g_state != SELFTEST_IDLE`
(`ipropi_selftest.c:391-393`). Si un auto-test ne revient jamais à `IDLE`
(rapport non émis, séquence interrompue), la carte **ignore toutes les notes**
jusqu'au prochain reset : c'est littéralement « je dois appuyer sur RUN ». Le
Clavier, lui, a bien transmis la commande (aucun `[NOTE_DROPPED]`), donc le
journal est muet et le symptôme paraît électrique alors qu'il peut être
logiciel.

À l'inverse, un mécanisme souvent soupçonné est **à écarter** : il n'y a pas de
« bus-off » possible. can2040 ne l'implémente pas (`docs/Features.md:63-69` :
« will not automatically enter a bus off state »), et `can2040_recover()`
n'existe pas dans la version embarquée. Une carte ne peut donc pas être muette
« à cause d'un bus-off » : si elle est muette, c'est un tampon, une file, ou un
état applicatif.

Enfin, l'asymétrie est structurelle : le Clavier publie ses propres échecs
d'émission (`[NOTE_DROPPED] reason=tx_full`), la carte ne publie **rien** de ses
refus ni de ses débordements. Tout incident dont la cause est côté carte est donc
indiagnosticable par construction.

### Decision

#### 1. L'acquittement de profil est répété, borné, dans la fenêtre

Le Moteur réémet son `PROFILE_STATUS` jusqu'à **3 fois**, espacées de 20 ms, tant
que la fenêtre d'acquittement du Clavier court (1 000 ms auto-test / 2 000 ms
réglage). Le contenu est identique (même `transfer_id`, même `result`), seule
l'émission est répétée ; le Clavier ignore les doublons (déjà idempotent :
`profile_card_ack` et `profile_sync_received` ne comptent qu'une fois par carte).

Un échec définitif des 3 tentatives est compté (`stats.profile_status_lost`) et
devient lisible par la décision 2.

Aucune trame n'est modifiée : c'est un changement de politique d'émission côté
Moteur.

#### 2. Les compteurs d'échec de la carte sont lisibles à la demande

Nouvelle paire, sur le modèle des requêtes existantes :

* `CAN_ID_CARD_STATS_REQUEST = 0x403` (Clavier → toutes les cartes), DLC 4 :
  `[0] proto_version`, `[1] counter_id`, `[2..3] reply_to` (LE).
* `CAN_ID_CARD_STATS = 0x425` (carte → Clavier), DLC 8 :
  `[0] proto_version`, `[1] counter_id`, `[2..3] uid_tag` (LE),
  `[4..7] valeur` (LE, 32 bits).

`counter_id` (v1) : `0` `can_errors` (débordements RX can2040), `1` `tx_fail`,
`2` `rx_overrun` (file applicative), `3` `solenoid_selftest_blocked`,
`4` `solenoid_ignored_tag`, `5` `solenoid_bad_channel`, `6`
`profile_status_lost`, `7` `profile_commits_rejected`, `8`
`profile_commits_ok`, `9` `rx_ignored`, `10` `solenoid_unoccupied`, `11`
`solenoid_rejected` (pilote BUSY/FAULT), `12` `solenoid_commands_ok`.

Les compteurs `10`, `11` et `12` sont ajoutés après l'observation du
2026-10-23 (voir « Complément » ci-dessous) : sans eux, une commande de note
transmise puis refusée par la carte reste invisible.

La console `m` du Clavier, ou une sous-commande `m stats`, interroge ces
compteurs pour les cartes présentes et les journalise. Interrogation **à la
demande uniquement**, jamais périodique.

#### 3. Une commande reçue et non exécutée est signalée

Toute `SOLENOID_COMMAND` reçue par une carte et **non exécutée** produit un
`CARD_EVENT` (ADR-0018, `0x424`) de type `COMMAND_IGNORED`, avec la raison et le
canal. La liste des raisons est **exhaustive** et sans exception : auto-test en
cours, tag différent, canal hors bornes, canal non occupé dans le bitmap local,
et refus du pilote (BUSY, FAULT). Une raison qui n'aurait pas encore d'événement
dédié doit être ajoutée, jamais laissée muette.

Symétriquement, une commande **exécutée** incrémente `solenoid_commands_ok`
(déjà le cas) et doit rester comptable : « transmis par le Clavier » et
« exécuté par la carte » sont deux faits distincts, et le diagnostic doit
pouvoir les comparer (IMP-009).

Limitation de débit : **au plus un événement par seconde et par raison**, pour
ne pas noyer le bus pendant une rafale de notes. Aucune commande n'est exécutée
« autrement » : on ne fait que rendre le refus visible (invariant 1).

#### 4. L'écriture flash n'est pas modifiée, mais son coût est documenté

L'effacement/programmation reste tel quel (ADR-0013 §5). Est actée la règle :
**aucune autre émission ne doit être lancée pendant une écriture flash** côté
carte, et le Clavier n'émet rien entre le COMMIT et l'acquittement. Les 40 à
70 ms de cécité sont une propriété connue, bornée par la fenêtre d'acquittement.

#### 5. Ce qui ne change pas

* Aucune limite d'activation, aucun délai de sécurité, aucun mapping.
* `all_channels_idle` continue de conditionner le déclenchement (ADR-0013 §4).
* Le Clavier ne relance rien, ne remet aucun défaut à zéro (invariant 1).
* Aucun remappage, aucun changement de table (invariants 2 et 3).

### Consequences

#### Positive
* **POS-001**: La classe « acquittement perdu alors que la carte a appliqué »
  disparaît : une répétition bornée absorbe un refus momentané de
  `can2040_transmit`, et l'opérateur n'a plus à relancer plusieurs `p send`.
* **POS-002**: `can_errors` rend la distinction « bus électriquement mauvais » /
  « logiciel trop lent » **mesurable** sans démonter la machine : c'est la
  réponse que l'opérateur demande avant d'investir au banc.
* **POS-003**: Une carte qui ignore une commande ne peut plus être confondue avec
  une carte morte ou un bus défaillant : le refus est nommé, avec sa cause.
* **POS-004**: L'asymétrie d'observabilité entre Clavier et cartes est corrigée
  sur les chemins critiques (profil, commandes), sans changer la philosophie
  (aucune décision prise par la carte).

#### Negative
* **NEG-001**: Deux nouvelles trames (`0x403`, `0x425`) et un vocabulaire de
  compteurs à maintenir des deux côtés.
* **NEG-002**: Jusqu'à 3 `PROFILE_STATUS` par transfert au lieu d'un : trafic
  marginal (3 × 8 octets) mais réel en cas de perte.
* **NEG-003**: Les cartes non reflashées n'exposent rien : le diagnostic reste
  partiel tant que les cinq cartes ne sont pas à jour (même réserve qu'ADR-0018).
* **NEG-004**: La décision 3 dépend d'ADR-0018 (`0x424`) : ordre d'implémentation
  imposé, et deux ADR à accepter au lieu d'une.

### Alternatives Considered

#### Démontage et vérification au banc d'abord
* **ALT-001**: **Description**: sortir les cartes du clavecin, contrôler
  câblage, transceiver, alimentation, refaire les essais au banc.
* **ALT-002**: **Rejection Reason**: coût élevé pour l'opérateur, et **le journal
  actuel ne permet pas de distinguer un bus mauvais d'un tampon logiciel
  débordé** : le banc pourrait ne rien révéler. Les décisions 1 à 3 rendent le
  diagnostic possible **sur la machine en place**, pour un reflash.

#### Augmenter les fenêtres d'acquittement (1 000 / 2 000 ms)
* **ALT-003**: **Description**: laisser 5 s aux cartes pour écrire et acquitter.
* **ALT-004**: **Rejection Reason**: masque le symptôme sans le comprendre, et
  allonge chaque échec. Une écriture de 4 Kio doit être bornée à quelques
  dizaines de ms (41 ms mesurées) : si elle dépasse la fenêtre, c'est un défaut
  à nommer, pas à absorber.

#### Réessayer indéfiniment l'acquittement jusqu'au prochain transfert
* **ALT-005**: **Description**: conserver le `PROFILE_STATUS` en attente et le
  réémettre jusqu'à ce qu'un nouveau transfert l'écrase.
* **ALT-006**: **Rejection Reason**: un acquittement tardif n'a plus de sens pour
  le Clavier (fenêtre close) et produirait du trafic fantôme ; 3 tentatives dans
  la fenêtre suffisent.

#### Battement de cœur périodique des cartes
* **ALT-007**: **Description**: chaque carte émet un état toutes les secondes.
* **ALT-008**: **Rejection Reason**: trafic permanent sur un bus déjà chargé
  pendant les auto-tests, pour une information que la requête à la demande
  fournit mieux.

#### Récupération « bus-off » explicite
* **ALT-009**: **Description**: appeler une fonction de récupération après
  erreur de bus.
* **ALT-010**: **Rejection Reason**: sans objet : can2040 n'implémente pas les
  états error-passive / bus-off et n'expose aucune fonction de récupération
  (`docs/Features.md:63-69`). Le problème n'est pas là, et cette alternative
  aurait envoyé l'opérateur vers une fausse piste matérielle.

### Implementation Notes
* **IMP-001**: Décision 1 dans `firmware/moteur/src/can_link.c` : remplacer
  l'émission unique par un compteur de tentatives (`profile_status_attempts`,
  intervalle `PROFILE_STATUS_RETRY_US` = 20 000) décrémenté dans
  `send_profile_status_if_due` ; ne pas réarmer sur un `send_frame` réussi ;
  compter `profile_status_lost` après la 3e. Ne pas changer le contenu de la
  trame ni le `transfer_id`.
* **IMP-002**: Vérifier côté Clavier que 3 acquittements identiques ne faussent
  rien : `profile_card_ack[]` et `profile_sync_received` sont déjà idempotents
  (ADR-0016) ; ajouter un test hôte sur un ack reçu deux fois.
* **IMP-003**: Décision 2 : `can_protocol.h` (`0x403`, `0x425`, DLC, enum des
  compteurs) + encodeurs/décodeurs + tests d'aller-retour ; côté Moteur, un
  `handle_card_stats_request` qui lit `stats` et répond avec le `reply_to` reçu ;
  côté Clavier, une sous-commande console (`m stats` ou `s`) qui émet une requête
  par compteur et journalise `[CARD] pos=N stats <nom>=<valeur>`.
* **IMP-004**: Décision 3 : émettre `COMMAND_IGNORED` depuis
  `handle_solenoid_command` **après** l'incrément du compteur correspondant, avec
  un limiteur à 1/s par raison (horodatage `now_us`). Ne pas émettre si
  `can2040_transmit` refuse : le compteur reste la trace de dernier recours.
* **IMP-005**: Ne pas toucher `flash_safe_execute` ni l'ordre
  `flash_range_erase` / `flash_range_program`. Documenter la cécité de 40-70 ms
  dans `docs/specs/`, et rappeler que le Clavier ne doit rien émettre entre
  COMMIT et acquittement (déjà le cas).
* **IMP-006**: Tests hôte : codecs `0x403`/`0x425` (bornes, `counter_id`
  inconnu), idempotence d'un double `PROFILE_STATUS`, et le limiteur 1/s de
  `COMMAND_IGNORED` (fonction pure si possible).
* **IMP-007**: Documentaliste : `docs/specs/CAN.md` (deux trames, tableau des
  compteurs, `COMMAND_IGNORED`), procédure de diagnostic « notes muettes » (lire
  les compteurs avant tout démontage), et mise à jour de
  `tests/phase3b_profil_tuning.md` §7.
* **IMP-008**: Rendre la corrélation possible côté Clavier : commande console
  pour basculer `CLAVIER_NOTE_LOG` **à l'exécution** (les lignes `[NOTE]`
  existent déjà, mais la constante vaut 0 par défaut). Sans cela, une note
  exécutée et une note refusée produisent le même journal vide — démontré le
  2026-10-23. Basculer ne doit rien changer d'autre que le journal.
* **IMP-009**: Dans `m stats`, comparer `note_sent` (cumul Clavier) à la somme
  des `solenoid_commands_ok` des cartes présentes. Un écart croissant pendant
  qu'on joue signifie « la carte refuse » ; afficher alors les compteurs de
  refus (`solenoid_unoccupied`, `solenoid_ignored_tag`, `solenoid_bad_channel`,
  `solenoid_selftest_blocked`, `solenoid_rejected`). C'est un **rapport**, jamais
  une correction automatique.

### References
* **REF-001**: [ADR-0013 — Profil d'activation centralisé sur le Clavier](./adr-0013-profil-activation-solenoides-centralise-clavier.md) (§4 acquittement obligatoire, §5 persistance flash)
* **REF-002**: [ADR-0015 — Réglage à chaud du profil](./adr-0015-reglage-chaud-profil-console-cdc.md) (NEG-002 : acquittement manquant, chemin `COMMIT`/`PROFILE_STATUS`)
* **REF-003**: [ADR-0016 — Attribution et diagnostic de la synchronisation du profil](./adr-0016-attribution-diagnostic-synchronisation-profil.md) (motifs `ACK_TIMEOUT`, attribution par UID)
* **REF-004**: [ADR-0017 — Journal de diagnostic auto-suffisant](./adr-0017-journal-diagnostic-auto-suffisant-horodatage-identite-redemarrages.md) (re-requête `detient`, absence de console côté Moteur)
* **REF-005**: [ADR-0018 — Trame CARD_EVENT (0x424)](./adr-0018-retour-etat-bouton-canaux-moteur-vers-clavier.md) (support de `COMMAND_IGNORED`, à accepter avant la décision 3)
* **REF-006**: `firmware/moteur/src/can_link.c` — `send_profile_status_if_due` (431-444), `arm_profile_status` (324-332), `handle_profile_commit` (385-429), `handle_solenoid_command` (137-147), compteur `can_errors` (578-580), tailles de file (15-17)
* **REF-007**: `firmware/moteur/src/solenoid_profile_store.c` — `profile_flash_write_cb` (141-146), `flash_safe_execute` (178), vérification après écriture (181-184)
* **REF-008**: `firmware/moteur/src/ipropi_selftest.c:391-393` — `ipropi_selftest_is_running`
* **REF-009**: `firmware/common/can2040/docs/API.md:133-136` — `CAN2040_NOTIFY_ERROR` = débordement des tampons RX internes, messages perdus
* **REF-010**: `firmware/common/can2040/docs/Features.md:63-69` — pas d'état bus-off, pas de récupération automatique
* **REF-011**: `firmware/common/can2040/docs/API.md:213` — file d'émission de quatre messages
