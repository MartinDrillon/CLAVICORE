---
title: "ADR-0023: Synchronisation du profil par reprises bornées et vérification de l'état détenu"
status: "Accepted"
date: "2026-10-24"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol", "diagnostic"]
supersedes: "0022 (décision 1 uniquement)"
superseded_by: ""
---

### Status
**Accepted**

Remplace la décision 1 d'ADR-0022, passée au statut `Superseded` avant toute
implémentation. Amende ADR-0013 §4, ADR-0016 §5, ADR-0017 décision 6 et
ADR-0020 décision 1 pour la conduite à tenir quand un acquittement manque.

Aucune trame, aucun identifiant, aucun DLC et aucun octet de `shared/protocol/`
ne changent. `CAN_PROTO_VERSION` reste 1. Aucun remapping, aucune poussée de
profil hors d'une demande explicite et aucune limite du pilote ne sont modifiés.

### Context

Le 2026-10-24, le premier `p send` après un changement de profil échoue après
2 000 ms avec un seul acquittement sur deux. La carte manquante détient pourtant
le tag attendu en NVM. Le second `p send`, sans nouvelle écriture flash, réussit
en 2 ms. Les échecs alternent entre les cartes. Le premier transfert est celui
qui exige une écriture flash de 40–70 ms avec interruptions masquées ; le second,
à tag identique, n'en exige aucune (ADR-0020, mesures et chronologie du banc).

Les faits suivants sont établis par le journal et le code courant :

* le transfert et l'écriture ont abouti sur la carte qui ne s'est pas acquittée ;
* `can2040_transmit()` réussi signifie seulement « trame mise en file », pas
  « trame reçue par le Clavier » (`firmware/common/can2040/src/can2040.c:1354-1415`) ;
* le Moteur efface `profile_status_pending` dès cet enfilage réussi et ne répète
  actuellement que sur refus de file (`firmware/moteur/src/can_link.c:465-489`) ;
* le Clavier tente un seul transfert par commande. Après le délai, il interroge
  les cartes mais utilise leur réponse uniquement comme diagnostic
  (`firmware/clavier/src/can_link.c:1054-1058`, `:1116-1122`, `:2450-2468`) ;
* un `PROFILE_STATUS` tardif ou hors synchronisation est actuellement ignoré sans
  ligne (`firmware/clavier/src/can_link.c:1139-1140`).

La cause racine **de l'échec opérationnel répétitif** est donc démontrée : une seule
perte transitoire d'acquittement, quel qu'en soit le point, devient un
`PROFILE_SYNC_FAILED` qui impose une seconde commande humaine, alors même que
l'état désiré est déjà présent sur la carte. La reprise conditionnelle d'ADR-0020
ne couvre que le refus d'enfilage ; la répétition fixe proposée par ADR-0022 ne
couvre ni la perte de BEGIN/CHUNK/COMMIT ni une perte persistante sur une
tentative.

La localisation physique de la trame perdue n'est **pas** démontrée. Une erreur de
réception Clavier, une perte d'émission Moteur, une erreur de bus pendant la
fenêtre flash et une collision entre réponses de même identifiant restent des
hypothèses à mesurer. Cette ADR ne déclare aucune de ces hypothèses prouvée et ne
conclut pas à un défaut matériel.

### Decision

#### 1. Une commande explicite pilote jusqu'à trois tentatives complètes

Un `p send` ou la synchronisation précédant un auto-test constitue une
**opération** comprenant au maximum **trois tentatives** du transfert complet
`PROFILE_BEGIN` + tous les `PROFILE_CHUNK` + `PROFILE_COMMIT`.

* Chaque tentative utilise un nouveau `transfer_id`, conformément à
  `docs/specs/CAN.md` (« incrémenté à chaque tentative »), mais exactement le même
  payload canonique et le même tag attendu.
* Les délais actuels restent inchangés : 1 000 ms avant auto-test, 2 000 ms pour
  le réglage. Ils ne sont ni raccourcis ni augmentés. Ils couvrent très
  largement les 40–70 ms observés, mais ne garantissent pas l'absence de
  chevauchement avec une écriture anormalement longue : le timeout flash Moteur
  est lui-même de 1 000 ms. Une reprise reste sans effet destructeur et un tel
  dépassement doit être mesuré et signalé, jamais masqué.
* Seules les cartes non confirmées restent attendues, mais les trames sont
  nécessairement diffusées à toutes. Une carte ayant déjà appliqué le tag ne
  réécrit pas la flash ; son traitement est idempotent.
* `NO_TRANSFER`, `BAD_SEQUENCE`, `BAD_CRC` et l'absence de réponse sont
  réessayables : aucun profil n'a été appliqué partiellement. `BUSY`,
  `INVALID_FIELD`, `FLASH_ERROR` et `UNSUPPORTED_VERSION` sont terminaux et
  restent explicitement signalés.
* Il n'y a toujours aucune diffusion spontanée au boot ou à la première note.
  Les reprises appartiennent toutes à la demande explicite déjà en cours.

Le pire chemin reste borné : trois fenêtres de 2 s, puis une sonde UID de 500 ms
au maximum en réglage, soit environ 6,5 s ; trois fenêtres de 1 s plus la même
sonde avant auto-test, soit environ 3,5 s, hors durée propre des trames. Le cas
nominal reste inchangé.

#### 2. Un acquittement confirme le contenu, pas seulement un numéro de transfert

Pendant toute l'opération, un acquittement direct n'est valide que si :

1. la carte est attendue et attribuée par les règles UID existantes ;
2. `result = OK` ;
3. `profile_tag32` est égal au CRC32 du payload de l'opération ;
4. son `transfer_id` appartient à l'ensemble des trois identifiants au maximum de
   l'opération.

Un acquittement d'une tentative antérieure de la **même opération** est donc
accepté s'il satisfait ces règles et si la carte n'est pas déjà confirmée ; il
est journalisé comme tardif. Les doublons restent sans effet sur les comptes.

Le contrôle de tag ne s'applique qu'aux réponses `result = OK` et aux réponses de
sonde. Un refus transporte le tag de l'ancien profil actif : le comparer au tag
attendu créerait une fausse divergence. Un refus réessayable (`NO_TRANSFER`,
`BAD_SEQUENCE`, `BAD_CRC`) est mémorisé par carte, mais ne positionne ni
`profile_sync_failed` ni la fin de l'opération ; la décision n'est prise qu'après
épuisement des tentatives. Seuls les refus terminaux positionnent l'échec
immédiat. Aucun refus de transport, même tardif, ne rétrograde une carte déjà
confirmée.

Une réponse **OK** dont le tag diffère est en revanche une contradiction d'état :
elle produit `PROFILE_DIVERGENT` et un échec terminal. Aucune confirmation
antérieure ne masque cette divergence et aucun `0x401` n'est émis.

#### 3. La sonde existante devient une réconciliation finale d'état

Après épuisement des tentatives, le Clavier utilise **une** requête UID existante
pour vérifier l'état réel des cartes encore non confirmées. Le retour de
`profile_request_probe()` doit être contrôlé : une sonde impossible à émettre est
un échec explicite, jamais une attente fictive.

L'opération reste active pendant toute la fenêtre de sonde et le verdict final
est différé jusqu'à son expiration ou à la confirmation de toutes les cartes.
`profile_sync_active` ne peut donc pas être effacé avant cette réconciliation.
Seules les réponses rattachées à `profile_probe_pending` et aux cartes encore
attendues peuvent réconcilier ; une annonce de boot spontanée ou une réponse à un
autre scan conserve son rôle actuel.

Une réponse `PROFILE_STATUS` de sonde (`transfer_id = 0`) réconcilie une carte si
et seulement si :

* elle provient d'une carte attendue, toujours en ligne ;
* `result = OK`, `source = NVM` et `profile_tag32` est le tag attendu ;
* elle arrive dans la fenêtre UID existante de 500 ms.

Cette réponse prouve que le profil canonique actif et persisté est celui demandé.
Elle ne prétend pas prouver la réception du COMMIT initial. Son motif est donc
distinct : `PROFILE_VERIFIED_NVM`, compté séparément des acquittements directs.

`source = DEFAULTS`, même à tag égal, ne réconcilie pas une opération persistante
et conserve `PROFILE_FALLBACK_IN_USE`. Une réponse de sonde à tag différent
conserve `PROFILE_DIVERGENT` ; une absence de réponse conserve `ACK_TIMEOUT`.
Dans tous ces cas, la carte reste en échec et n'est jamais réconciliée.

Cette décision amende ADR-0017 décision 6 : la réponse de sonde n'est toujours pas
un acquittement du transfert initial, mais elle peut clore l'opération comme
**vérification d'état**, uniquement après les reprises et avec un motif visible.

#### 4. Le succès et le lancement de l'auto-test exigent toutes les cartes

L'opération réussit lorsque chaque carte attendue est soit acquittée directement,
soit réconciliée. Le profil actif du Clavier et son `local_profile_tag` ne sont
promus qu'à ce moment. Le `0x401` d'auto-test n'est émis qu'à ce même moment.

Si une carte reste absente, erronée ou divergente, le résultat reste
`PROFILE_SYNC_FAILED` et aucun `0x401` n'est émis. Aucune divergence n'est effacée
par une reprise ou une réconciliation.

Le compte rendu normal est limité à :

* une ligne par reprise : essai, nombre de cartes restantes ;
* un verdict final unique : durée totale, tentatives, acquittements directs,
  réconciliations, tag ;
* le détail par carte seulement en échec ou en niveau de journal bring-up.

Les compteurs Clavier restent locaux, par opération ; aucune trame de
statistiques n'est ajoutée :

* `profile_sync_retries` compte les tentatives supplémentaires réellement lancées
  (0 à 2), incrémenté au départ de chaque reprise ;
* `profile_sync_reconciled` compte les cartes réconciliées par la sonde finale
  (0 au nombre de cartes attendues), incrémenté une seule fois par carte.

#### 5. Aucune modification Moteur n'est requise pour cette correction

La répétition fixe de trois `PROFILE_STATUS` proposée par ADR-0022 décision 1
n'est pas retenue. Elle est remplacée par la reprise complète et la
réconciliation ci-dessus, qui couvrent aussi les trames de requête perdues.

Le Moteur conserve son émission actuelle et son optimisation « tag identique en
NVM ⇒ pas d'écriture ». Aucun délai dérivé de l'UID, aucune nouvelle trame et
aucune modification de can2040 ne sont introduits sans mesure qui les justifie.
La reprise ne modifie donc pas le séquencement des réponses : si une collision
est la cause, elle peut se reproduire à chaque tentative. La sonde finale et la
campagne avec analyseur CAN restent nécessaires ; la reprise n'est pas présentée
comme la preuve ou la correction de cette hypothèse.

### Consequences

#### Positive
* **POS-001**: Une perte transitoire d'acquittement ne demande plus un second `p send` humain : la commande en cours reprend et conclut.
* **POS-002**: La perte de BEGIN, CHUNK ou COMMIT est également couverte, contrairement à une répétition du seul acquittement.
* **POS-003**: La réconciliation vérifie le but réel — le profil actif et persisté — au lieu d'exiger seulement la réception d'une trame de transfert.
* **POS-004**: Le tag est contrôlé sur chaque confirmation ; une divergence reste visible et bloquante pour l'auto-test.
* **POS-005**: Aucun changement de trame, de version ou de firmware Moteur n'est nécessaire ; la correction minimale est côté Clavier.
* **POS-006**: Les reprises et réconciliations restent comptées : la robustesse ne sert pas à masquer un bus dégradé.

#### Negative
* **NEG-001**: Une commande qui subit des pertes peut durer jusqu'à environ 6,5 s en réglage et 3,5 s avant auto-test, contre 2 s ou 1 s avant l'échec actuel.
* **NEG-002**: Une reprise diffuse à nouveau treize trames à toutes les cartes, y compris celles déjà confirmées.
* **NEG-003**: La machine d'état Clavier doit distinguer opération, tentatives, acquittements tardifs et réconciliation ; ses tests deviennent indispensables.
* **NEG-004**: `PROFILE_VERIFIED_NVM` prouve le contenu actif/persisté, pas le chemin de transfert ; le diagnostic doit conserver cette distinction.
* **NEG-005**: La cause électrique ou can2040 de la perte initiale reste à mesurer ; une collision éventuelle peut se répéter entre tentatives et la reprise ne constitue pas sa correction.
* **NEG-006**: Une écriture flash anormalement longue peut chevaucher la fenêtre de 1 s ; la marge réelle doit être mesurée et un dépassement reste visible.

### Alternatives Considered

#### Répéter seulement trois fois `PROFILE_STATUS` (ADR-0022 décision 1)
* **ALT-001**: **Description**: réémettre l'acquittement à 20 ms et 40 ms, même après un enfilage réussi.
* **ALT-002**: **Rejection Reason**: ne couvre ni BEGIN/CHUNK/COMMIT perdu ni une perte systématique sur la tentative, et ne vérifie pas l'état final ; ce peut être un complément mesuré, pas la correction principale.

#### Allonger le délai d'acquittement
* **ALT-003**: **Description**: porter les fenêtres à cinq secondes ou davantage.
* **ALT-004**: **Rejection Reason**: ne reprend aucune trame perdue et ralentit seulement le constat d'échec.

#### Réconcilier immédiatement après le premier timeout, sans reprise
* **ALT-005**: **Description**: accepter directement le tag NVM rapporté par la sonde existante.
* **ALT-006**: **Rejection Reason**: résout l'incident observé mais pas le cas où la carte n'a jamais reçu un transfert valide ; la reprise complète reste nécessaire.

#### Désynchroniser les acquittements par un slot dérivé de l'UID
* **ALT-007**: **Description**: appliquer à `0x423` le jitter des annonces UID afin de réduire les départs simultanés.
* **ALT-008**: **Rejection Reason**: risque structurel plausible mais non démontré comme cause ; des collisions de slots subsistent et cela impose un changement Moteur. À rouvrir seulement avec des trames d'erreur mesurées.

#### Ajouter un acquittement de l'acquittement ou des trames ciblées
* **ALT-009**: **Description**: confirmer chaque `PROFILE_STATUS` par une nouvelle trame Clavier, ou adresser les reprises à une carte.
* **ALT-010**: **Rejection Reason**: nouvelle interface et nouvel état sans nécessité ; les trames actuelles sont diffusées et la vérification d'état existe déjà.

#### Rendre le profil volatil pour éviter la flash
* **ALT-011**: **Description**: utiliser la décision D2 d'ADR-0015 afin de supprimer la fenêtre d'interruptions masquées pendant le réglage.
* **ALT-012**: **Rejection Reason**: change la persistance demandée et ne répare pas la perte d'un acquittement ; ce n'est pas un correctif de synchronisation.

### Implementation Notes

* **IMP-001**: Côté `firmware/clavier/src/can_link.c`, séparer l'état d'opération de l'état de tentative. Ne pas dupliquer la table UID ni modifier `uid_position.h`.
* **IMP-002**: Réutiliser le même buffer canonique et le même CRC pour toutes les tentatives. Incrémenter `transfer_id` à chaque tentative et traiter le rebouclage 1..255.
* **IMP-003**: Dans `handle_profile_status`, mémoriser les trois `transfer_id` de l'opération et tester l'appartenance à cet ensemble au lieu de la seule égalité avec la tentative courante. Ne contrôler le tag que sur `result = OK`, journaliser les statuts tardifs et ne jamais rétrograder une confirmation sur refus de transport. Un refus réessayable est mémorisé sans poser `profile_sync_failed` ; seuls les refus terminaux posent l'échec immédiat. Une divergence sur réponse OK reste toujours émise.
* **IMP-004**: Vérifier le retour de `start_uid_request()` dans la réconciliation. Conserver l'opération active et `profile_probe_pending` jusqu'à la fin de la fenêtre de sonde ; différer le verdict final jusqu'à cette échéance ou à la confirmation complète. Les réponses de sonde ne modifient ni les comptes d'une tentative déjà close ni ceux d'une autre opération.
* **IMP-005**: Ne lancer `0x401` qu'après confirmation/réconciliation de toutes les cartes attendues. Conserver tous les refus et gardes existants de jeu récent et de canaux actifs.
* **IMP-006**: Tests hôte : premier succès ; succès à la deuxième et troisième tentative ; perte de BEGIN/CHUNK/COMMIT ; chaque refus réessayable portant l'ancien tag sans fausse divergence ni échec immédiat ; acquittement tardif sur chacun des trois identifiants ; doublon ; mauvais tag sur OK ; refus terminal ; absence de sonde ; opération maintenue active pendant la sonde ; réconciliation NVM ; refus de DEFAULTS ; aucune émission de `0x401` sur échec ; rebouclage des échéances et des identifiants.
* **IMP-007**: Essai reproductible avec injection d'une perte : (a) un seul `0x423` après COMMIT réussi, (b) un BEGIN, (c) un CHUNK, (d) tous les acquittements de transfert mais pas ceux de sonde. Avant : échec ou intervention humaine. Après : une seule commande conclut, avec reprise/réconciliation visible.
* **IMP-008**: Banc deux cartes, au moins 200 alternances de deux tags pour imposer une écriture à chaque transfert : zéro échec final et zéro commande de resynchronisation manuelle. À tag identique : succès à la première tentative en moins de 5 ms hors charge. Toute reprise hors injection est relevée, pas ignorée. Mesurer aussi la latence maximale COMMIT→fin d'écriture→STATUS ; si elle ne laisse pas de marge dans la fenêtre de 1 s, ne jamais affirmer l'absence de chevauchement et traiter ce dépassement comme un résultat à part.
* **IMP-009**: Pour localiser la perte initiale, campagne séparée avec analyseur CAN : présence/absence du `0x423` manquant sur le fil, trames d'erreur et délai COMMIT→STATUS, complétée par `m stats`. Relever si la même perte se reproduit à chaque tentative, signature qui imposerait de rouvrir la piste de collision. Ne conclure à une cause matérielle que sur ces mesures chiffrées.
* **IMP-010**: Documentaliste après acceptation : `docs/specs/CAN.md`, `docs/specs/profil-solenoides.md`, procédures de banc et compte rendu console. Décrire l'opération à trois tentatives et `PROFILE_VERIFIED_NVM` sans changer les octets des trames.

### References
* **REF-001**: [ADR-0013 — Profil centralisé et persistant](./adr-0013-profil-activation-solenoides-centralise-clavier.md) (§4 et §5)
* **REF-002**: [ADR-0016 — Attribution et motifs de synchronisation](./adr-0016-attribution-diagnostic-synchronisation-profil.md)
* **REF-003**: [ADR-0017 — Sonde d'état après échec](./adr-0017-journal-diagnostic-auto-suffisant-horodatage-identite-redemarrages.md) (décision 6)
* **REF-004**: [ADR-0020 — Répétition conditionnelle et compteurs](./adr-0020-comptabilite-echecs-cartes-moteur.md)
* **REF-005**: [ADR-0022 — Décision 1 remplacée](./adr-0022-precisions-banc-acquittement-tag-appui-court.md)
* **REF-006**: `firmware/clavier/src/can_link.c` — `begin_profile_sync`, `handle_profile_status`, timeout et `profile_request_probe`
* **REF-007**: `firmware/moteur/src/can_link.c:419-489` — COMMIT, écriture et émission de `PROFILE_STATUS`
* **REF-008**: `firmware/moteur/src/solenoid_profile_store.c:162-192` — écriture, relecture et source NVM
* **REF-009**: `firmware/common/can2040/src/can2040.c:1354-1415` — succès de `can2040_transmit` = enfilage
* **REF-010**: `docs/specs/CAN.md` et `docs/specs/profil-solenoides.md` — contrat actuel des trames et du cycle de vie
