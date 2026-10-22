---
title: "ADR-0016: Attribution et diagnostic de la synchronisation du profil — correspondance UID→position unique et motifs d'échec distincts"
status: "Accepted"
date: "2026-10-21"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

Amende partiellement ADR-0013 (§4) : la condition d'arrêt de l'auto-test
(« aucun `0x401` sans acquittement de chaque carte présente ») est **conservée**,
mais la définition de « carte présente » au sens de la synchronisation du profil,
l'attribution des acquittements et le compte rendu d'échec sont précisés.
Aucune dérogation à un invariant n'est demandée : l'invariant 1 est renforcé
(plus aucun acquittement perdu en silence), l'invariant 5 n'est pas touché (les
trames `0x420`–`0x423` sont inchangées).

Mise à jour du **2026-10-22** : IMP-010 est remplacé par ADR-0017 décision 6
(l'instrumentation demandée côté Moteur est inobservable, la carte n'ayant ni USB
ni console UART). Aucune autre décision de cette ADR n'est modifiée.

### Context

Incident du 2026-10-21, journal CDC du Clavier après reflashage (une carte à la
position 0, une carte à la position 2) :

```text
[UID] pos=0 uid=A3FA4C99CA526F4B fw=0.1.0 proto=1 reply_to=0
[STATUS] code=UID_NOT_IN_TABLE pos=127
[PROFILE] sync debut but=auto-test tag=0x4A0B4C69 attendus=1
[STATUS] code=PROFILE_SYNC_FAILED
[PROFILE] sync echec (delai 1000 ms) acquittements=0/1
[PROFILE] sync carte pos=2 tag=0x9D5C MANQUANT
```

Conséquences observées : l'auto-test ne démarre jamais (ADR-0013 §4), la table de
commissioning n'est donc jamais réécrite, et `UID_NOT_IN_TABLE` reste affiché
indéfiniment. Le message se lit comme « la seconde carte n'est plus détectée »,
ce qui est faux : elle a annoncé son UID au boot (sinon elle ne serait pas
comptée dans `attendus`, voir plus bas). Le défaut d'actionnabilité est donc
double : mauvais diagnostic apparent, et aucune cause réelle indiquée.

**Cause immédiate identifiée après cet incident (analyse de code, 2026-10-21)** :
le chemin « bouton Moteur 3 s » arme l'échéance de synchronisation avec un
horodatage postérieur à celui de la passe en cours, ce qui déclenche le timeout
dans la même passe. Détail : `can_link_poll` capture `first_ms = now_ms()`
(`can_link.c:1914`), draine ensuite la trame `0x402` qui appelle
`handle_selftest_trigger` → `begin_profile_sync` (`can_link.c:1747`), laquelle
diffuse 12 trames (≈ 3,6 ms de `sleep_us`) puis pose
`profile_sync_start_ms = now_ms()` (`can_link.c:879`), donc
`profile_sync_start_ms > first_ms`. Le test d'échéance
`(uint32_t)(first_ms - profile_sync_start_ms) >= 1000` (`can_link.c:2009`) vaut
alors `0xFFFFFFFC`, soit 4 294 967 292 ms : l'échec est prononcé avant que le
premier acquittement ait pu être lu, puis `profile_sync_active` est remis à
faux, si bien que les acquittements arrivés ensuite sont ignorés
(`can_link.c:942`). Le chemin console (`t`, `can_link_request_selftest`,
`can_link.c:2059`) arme l'échéance entre deux passes et n'est pas affecté. Le
défaut est un bug de code (aucune ADR n'était nécessaire pour le corriger) ;
il est décrit ici parce qu'il est indissociable de l'incident et parce que
l'absence d'observabilité ci-dessous est ce qui a rendu son diagnostic long.
Cette ADR conserve donc son objet : rendre ces échecs attribuables et nommés,
et interdire la classe de bug temporelle décrite en décision 7.

Trois mécanismes distincts, aujourd'hui incohérents, décident de « quelle carte
est où » pendant la synchronisation du profil :

1. **Présence** (`commissioning_state_mark_uid_online`,
   `firmware/clavier/src/commissioning.c:152-174`) : par **UID complet**, dans la
   table **persistée en flash**.
2. **Nombre d'acquittements attendus** (`can_link.c:865-875`) : pour chaque
   entrée de `uid_position.h`, on teste si le **numéro de position** qu'elle porte
   est marqué en ligne. Aucun UID n'est comparé. Une entrée d'amorçage peut donc
   être « attendue » alors que l'UID réel de la carte à cette position est
   différent de celui inscrit dans `uid_position.h`.
3. **Attribution de l'acquittement** (`can_link.c:770-780`,
   `profile_position_for_tag`) : par `uid_tag` CRC16, dans la table
   **d'amorçage** uniquement. La position retournée est l'**index de boucle**, pas
   la position stockée dans l'entrée. Un `uid_tag` inconnu donne
   `UID_POSITION_UNKNOWN_REPORTED` et l'acquittement est **jeté sans aucune
   ligne** (`can_link.c:942-948` : `profile_sync_received` n'est incrémenté que
   si `position < UID_POSITION_COUNT`).

Deux des trois mécanismes se contredisent, et le troisième est muet : un
acquittement correctement émis par la carte peut être compté « 0/1 » sans que
rien ne permette de le distinguer d'une absence totale de réponse.

Second défaut : ADR-0013 IMP-011 prévoit explicitement le cas « Moteur ancien ne
répondant pas aux nouvelles trames » et demande un message « profil non
confirmé ». Le firmware l'implémente comme n'importe quel timeout
(`can_link.c:2009-2020`), et le compte rendu `MANQUANT` ne distingue pas :

* une carte qui ne parle pas le protocole de profil (firmware Moteur antérieur à
  ADR-0013 — aucune trame `PROFILE_STATUS` de boot reçue depuis le scan), cas où
  la seule action utile est de reflasher la carte ;
* une carte qui parle le protocole mais dont l'acquittement est perdu ou non
  attribué ;
* une carte qui refuse le transfert (`result != OK`), seul cas déjà nommé par
  `[PROFILE] refus` (`can_link.c:949-953`).

Pendant la mise au point, ces trois cas coexistent normalement (cartes à des
versions de firmware différentes). Les confondre bloque le commissioning sans
indiquer la sortie.

Le profil du Clavier est identique sur toutes les cartes (ADR-0013 §1) : rien
dans cette ADR ne concerne le contenu du profil, seulement l'identification des
cartes et le compte rendu.

### Decision

#### 1. Une seule correspondance UID→position pendant la synchronisation du profil

La table d'amorçage `firmware/clavier/src/uid_position.h` est l'unique référence
pour nommer les cartes pendant la synchronisation du profil, **interrogée par UID
complet** via `uid_position_lookup()` (`firmware/clavier/src/uid_position_format.h`),
et non par numéro de position ni par réimplémentation locale de la boucle.

Justification : c'est déjà la référence de l'auto-test lui-même
(`seed_lookup_position`, `can_link.c:609-616` et `can_link.c:1365`), et c'est
l'entrée à partir de laquelle la nouvelle table sera construite. La table
persistée (invariant 3) reste la source de vérité du **jeu** ; elle ne sert ici
qu'à établir la **présence** (une carte annoncée et connue de la table
persistée). Une procédure ne doit pas mélanger deux références.

#### 2. « Attendue » se compte sur les UID, jamais sur les numéros de position

Une entrée de `uid_position.h` est **attendue** par la synchronisation du profil
si et seulement si l'UID de cette entrée a été vu en ligne depuis le dernier scan
de boot (`UID_REQUEST` mode `BOOT_SCAN`, qui remet déjà toutes les cartes hors
ligne, `can_link.c:764-765`).

Conséquences directes :

* une carte présente sur le bus mais absente de `uid_position.h` n'est **jamais**
  attendue : elle est signalée `UID_NOT_IN_TABLE` (ADR-0009) et exclue, comme
  pour l'auto-test ; elle ne peut donc plus bloquer la procédure par un simple
  effet de numéro de position ;
* une entrée d'amorçage dont l'UID réel ne correspond pas à la carte présente à
  cette position n'est pas attendue, et l'écart est signalé par les lignes
  existantes (`[UID]` + `UID_NOT_IN_TABLE`), jamais corrigé (invariant 1) ;
* le nombre `attendus=` reste un simple compte, mais il porte désormais la même
  sémantique que l'attribution des acquittements (décision 3) : il ne peut plus
  valoir 1 quand zéro acquittement peut être attribué.

#### 3. Attribution par `uid_tag` sur la même correspondance, position stockée

Un `PROFILE_STATUS` de transfert est attribué à la position **stockée** dans
l'entrée de `uid_position.h` dont l'UID a pour CRC16 le `uid_tag` reçu. Une carte
est marquée « acquittée » dès qu'un `PROFILE_STATUS` portant son `uid_tag` arrive
avec le `transfer_id` courant, **quel que soit** son `result` ; un `result != OK`
est compté et signalé séparément (comportement actuel conservé,
`can_link.c:945-953`).

#### 4. Aucun acquittement n'est jeté en silence

Un `PROFILE_STATUS` dont le `uid_tag` ne correspond à aucune entrée attendue est
signalé explicitement :

* ligne `[PROFILE] ack inattendu tag=0x%04X transfer=%u result=%u` (tag reçu,
  jamais le tag attendu) ;
* nouveau `STATUS` `PROFILE_ACK_UNATTRIBUTED` (code `0x35`) ;
* compteur dédié dans les statistiques de diagnostic.

Ce cas ne doit pas être confondu avec l'absence de réponse : il est produit dès
réception, sans attendre la fin du délai.

#### 5. Trois motifs d'échec nommés, avec la cause et l'action

Le compte rendu par carte remplace la mention binaire `acquitte`/`MANQUANT` par
un motif :

| Motif | Critère | Ce que l'opérateur doit faire |
|---|---|---|
| `PROFILE_STATUS_SENT_OK` | acquittement reçu, `result = OK` | rien |
| `ACK_REJECTED` | acquittement reçu, `result != OK` | corriger la cause (code affiché : `BUSY`, `BAD_CRC`, `INVALID_FIELD`, `FLASH_ERROR`…) |
| `NO_PROFILE_STATUS` | aucun `PROFILE_STATUS` de boot (`transfer_id = 0`) reçu de cette carte depuis le dernier scan | **reflasher la carte Moteur** : son firmware est antérieur à ADR-0013 (IMP-011) |
| `ACK_TIMEOUT` | `PROFILE_STATUS` de boot reçu, mais aucun acquittement pour le `transfer_id` courant dans le délai | défaut de bus ou de firmware Clavier : à investiguer |

`NO_PROFILE_STATUS` produit en plus le nouveau `STATUS`
`PROFILE_UNSUPPORTED` (code `0x36`) au moment de l'échec, pour que le cas
« carte à reflasher » soit visible sans lire le détail par carte.

L'état « `PROFILE_STATUS` de boot reçu » est mémorisé **par position**, remis à
zéro avec les drapeaux de présence au scan de boot.

#### 6. Ce qui ne change pas

* Aucun `0x401` n'est émis tant qu'une carte attendue n'a pas acquitté `OK`
  (ADR-0013 §4). Aucun contournement automatique, aucun repli silencieux sur les
  valeurs compilées du Moteur.
* Aucune trame `0x420`–`0x423` n'est modifiée ; `CAN_PROTO_VERSION` reste 1.
* Aucun remappage : une divergence entre `uid_position.h`, la table persistée et
  les cartes réellement présentes reste **signalée**, jamais corrigée.

#### 7. Une seule base de temps par passe de `can_link_poll`

Toute échéance armée à l'intérieur d'une passe de `can_link_poll` (y compris
depuis `handle_frame`, donc depuis un déclenchement reçu en CAN) doit être
comparée à l'horodatage de cette passe de façon **signée**, ou être armée avec
l'horodatage de cette même passe. Un horodatage postérieur à `first_ms` ne doit
jamais produire une échéance « déjà dépassée » : `(int32_t)(first_ms - start_ms)`
est la forme admise, une soustraction `uint32_t` nue ne l'est pas.

Motif : c'est la cause de l'échec instantané décrit en contexte. Une procédure
de commissioning ne doit pas pouvoir échouer avant d'avoir laissé courir son
délai, quel que soit le point d'entrée (trame `0x402`, console, futur bouton).

### Consequences

#### Positive
* **POS-001**: Le message d'échec distingue « carte Moteur à reflasher » de
  « acquittement perdu » : l'incident du 2026-10-21 devient auto-explicatif, et
  l'action (reflasher la carte) est écrite dans le journal.
* **POS-002**: Un acquittement correctement émis ne peut plus être perdu par un
  numéro de position cohérent mais un UID différent : le compteur `attendus=`
  porte la même sémantique que son compteur d'acquittements.
* **POS-003**: La synchronisation du profil utilise la même référence que
  l'auto-test qu'elle précède, ce qui supprime une classe entière d'incohérences
  entre deux étapes de la même procédure.
* **POS-004**: La correction de `profile_position_for_tag` (position stockée au
  lieu de l'index de boucle) supprime un défaut latent qui se manifesterait dès
  que `uid_position.h` n'est plus trié par position croissante.
* **POS-005**: Invariant 1 renforcé : les trois cas d'échec sont nommés, et le cas
  « tag inattribuable » n'est plus silencieux.
* **POS-006**: La règle d'horodatage (décision 7) supprime une classe entière
  d'échecs instantanés, qui ne se distinguaient pas d'un défaut matériel dans le
  journal.

#### Negative
* **NEG-001**: Deux nouveaux codes `STATUS` (`0x35`, `0x36`) et un nouveau motif
  dans les lignes `[PROFILE]` : la documentation de diagnostic et les procédures
  de test doivent être mises à jour (Documentaliste).
* **NEG-002**: Le Clavier doit mémoriser un drapeau supplémentaire par position
  (acquittement de boot reçu), avec la même discipline de remise à zéro que
  `board_online`.
* **NEG-003**: Le blocage de l'auto-test est conservé : une carte à firmware
  ancien continue d'empêcher le commissioning tant qu'elle n'est pas reflashée.
  C'est assumé (ADR-0013 §4), mais cela reste une dépendance entre la version du
  firmware Moteur et la possibilité de commissionner.
* **NEG-004**: Le vocabulaire de diagnostic s'étend encore ; au-delà de
  `PROFILE_UNSUPPORTED`, toute nouvelle famille d'échec devra être nommée plutôt
  que regroupée sous un code générique.

### Alternatives Considered

#### Attribuer les acquittements par la table persistée (commissioning)
* **ALT-001**: **Description**: résoudre le `uid_tag` reçu contre les UID de la
  table de commissioning (invariant 3), en ignorant `uid_position.h`.
* **ALT-002**: **Rejection Reason**: l'auto-test qui suit résout les positions par
  `uid_position.h` et construit la table à partir d'elle ; attribuer les
  acquittements par une autre table reproduirait, en miroir, l'incohérence
  actuelle. La table persistée conserve son rôle de source de vérité du jeu, pas
  de référence de commissioning.

#### Conserver le comptage d'attente par numéro de position
* **ALT-003**: **Description**: garder `attendus` calculé sur les numéros de
  position, en ajoutant seulement un meilleur message d'échec.
* **ALT-004**: **Rejection Reason**: laisse subsister le cas « attendu = 1,
  attribuable = 0 » qui produit exactement l'incident ; le message resterait
  trompeur.

#### Exclure les cartes qui ne parlent pas le profil et poursuivre l'auto-test
* **ALT-005**: **Description**: émettre `0x401` vers les cartes ayant acquitté,
  ignorer les autres après un avertissement, et inscrire la table en état
  `PROVISIONAL`.
* **ALT-006**: **Rejection Reason**: contredit ADR-0013 §4 (aucune sonde lancée
  avec des paramètres PROBE divergents d'une carte à l'autre) et rendrait
  silencieusement inopérante une partie du clavier. Rouvrable si le besoin de
  commissionner avant reflashage de toutes les cartes devient impératif ; il
  faudrait alors une action explicite de l'opérateur, jamais un automatisme.

#### Une procédure de commissioning distincte, sans synchronisation de profil
* **ALT-007**: **Description**: offrir une commande console « auto-test sans
  profil » (mesure IPROPI avec les valeurs de repli de chaque carte, tag de
  profil enregistré et signalé) pour permettre le bring-up avec des cartes
  hétérogènes.
* **ALT-008**: **Rejection Reason**: non retenue à ce stade. Elle crée deux
  chemins de commissioning aux garanties différentes, et l'estimation ohmique
  (ADR-0009) dépend des paramètres PROBE appliqués. Une seule réponse pour
  l'instant : reflasher la carte, ce que `NO_PROFILE_STATUS` rend explicite.

#### Attribuer les acquittements par une trame plus riche (UID 64 bits)
* **ALT-009**: **Description**: faire porter les 8 octets de l'UID ou un
  identifiant de transfert plus long dans `PROFILE_STATUS` pour supprimer
  l'ambiguïté des tags CRC16.
* **ALT-010**: **Rejection Reason**: modification d'une trame partagée
  (`shared/protocol/`) pour un bénéfice nul ici : la collision de CRC16 entre
  UID réellement présents n'est pas le problème observé, et l'attribution doit de
  toute façon suivre la même table que l'attente.

### Implementation Notes
* **IMP-001**: Remplacer le corps de `profile_position_for_tag`
  (`can_link.c:770-780`) par un appel à `uid_position_lookup()` et retourner la
  **position stockée** dans l'entrée, jamais l'index de boucle. Ne pas dupliquer
  la comparaison d'UID.
* **IMP-002**: Calculer `profile_sync_expected` (`can_link.c:865-875`) sur les
  entrées dont l'UID a été vu en ligne ; une même fonction doit servir à décider
  l'attente, à attribuer l'acquittement et à imprimer le compte rendu par carte,
  pour qu'ils ne puissent plus diverger.
* **IMP-003**: Mémoriser `profile_boot_status_seen[position]`, alimenté par
  `handle_profile_status` quand `transfer_id == CAN_PROFILE_TRANSFER_ID_BOOT`,
  remis à zéro en même temps que `board_online`
  (`commissioning_state_set_all_boards_offline`, `can_link.c:764-765`).
* **IMP-004**: La ligne `[PROFILE] ack inattendu` doit imprimer le **tag reçu** et
  le `transfer_id` ; la ligne `[PROFILE] sync carte` le tag attendu (calculé
  depuis `uid_position.h`) et le motif nommé. Ne jamais fusionner les deux
  lignes.
* **IMP-005**: Ajouter les codes `0x35` `PROFILE_ACK_UNATTRIBUTED` et `0x36`
  `PROFILE_UNSUPPORTED` dans `firmware/clavier/src/diag_log.h` et leur libellé
  dans `diag_log.c`, sans toucher au format `[TAG] clé=valeur` d'ADR-0005.
* **IMP-006**: Tests hôte à ajouter dans `tests/host/` : attribution par UID avec
  `uid_position.h` volontairement non triée ; entrée d'amorçage dont l'UID ne
  correspond pas à la carte annoncée (attendu = 0, pas de blocage) ; acquittement
  d'un tag inconnu (ligne + `0x35`) ; carte n'ayant jamais envoyé de
  `PROFILE_STATUS` de boot (`NO_PROFILE_STATUS` + `0x36`) ; carte refusant
  (`ACK_REJECTED`).
* **IMP-007**: Documentaliste : mettre à jour `docs/specs/CAN.md` (codes `0x35`,
  `0x36`, sémantique de `attendus`), la procédure `tests/phase3b_bout_en_bout.md`
  (cas « carte Moteur non reflashée ») et la fiche d'aide du journal CDC.
* **IMP-008**: Ne pas modifier les trames `0x420`–`0x423` ni le format du profil ;
  ce chantier est strictement côté Clavier et côté diagnostic.
* **IMP-009**: Corriger `can_link.c:2009` en comparaison signée
  (`(int32_t)(first_ms - profile_sync_start_ms) >= (int32_t)timeout_ms`) et
  auditer les autres échéances du fichier (`can_link.c:1946`, `can_link.c:1971`)
  au regard de la décision 7. Un test hôte reproduit le chemin « déclenchement
  reçu pendant la passe » : la synchronisation doit rester active et consommer
  son délai complet.
* **IMP-010**: **Remplacé par ADR-0017 décision 6 (2026-10-22).** Le raisonnement
  d'origine — localiser un échec entre « le Moteur n'a rien reçu », « le Moteur a
  refusé » et « le Moteur a acquitté mais l'acquittement est perdu » — reste
  valable, mais la solution demandée ici (une ligne de journal au
  `PROFILE_COMMIT`) est **inobservable** : la carte Moteur n'a ni port USB ni
  console UART (`tests/build_moteur_windows.md` §6), et n'imprime rien sur
  réception de `0x420`–`0x422`. L'observabilité passe donc par le Clavier :
  re-requête `UID_REQUEST` après un échec, compte rendu `tag`/`source` par carte.
  **Ne pas implémenter de journal côté Moteur pour ce besoin.**

### References
* **REF-001**: [ADR-0013 — Profil d'activation centralisé sur le Clavier](./adr-0013-profil-activation-solenoides-centralise-clavier.md) (§4 séquence de l'auto-test, IMP-011 carte Moteur ancienne)
* **REF-002**: [ADR-0009 — Table de commissioning provisoire](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) (carte présente absente de la table : exclusion et `UID_NOT_IN_TABLE`)
* **REF-003**: [ADR-0010 — Ancrage du mapping de commissioning](./adr-0010-ancrage-grave-mapping-commissioning.md) (position 0 = la plus grave)
* **REF-004**: [ADR-0005 — Affichage diagnostic sur journal série CDC](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) (format `[TAG] clé=valeur`, extension additive des codes)
* **REF-005**: [ADR-0003 — Bring-up CAN Phase 3](./adr-0003-bringup-can-phase3-lecture-uid-sysex.md) (table d'amorçage `uid_position.h`, non source de vérité)
* **REF-006**: `firmware/clavier/src/can_link.c` — `profile_position_for_tag` (770-780), `begin_profile_sync` (799-891), `handle_profile_status` (923-954), `print_profile_sync_cards` (907-921), échec par timeout (2009-2020)
* **REF-007**: `firmware/moteur/src/can_link.c` — `PROFILE_STATUS` de boot (209-224), `arm_profile_status` (324-331), `handle_profile_commit` (385-429)
* **REF-008**: `firmware/clavier/src/commissioning.c` — `commissioning_state_mark_uid_online` (152-174), remise à zéro de `board_online` au chargement (47-95)
