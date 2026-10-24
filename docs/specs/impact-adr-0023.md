# Impact ADR-0023 — synchronisation du profil par reprises bornées et vérification d'état

Note de référence pour le **Codeur** et le **Validateur**.
[ADR-0023](../adr/adr-0023-synchronisation-profil-reprise-verification-etat.md)
est **`Accepted`** depuis le 2026-10-24.

> **État de cette note (2026-10-24).** ADR-0023 est **implémentée** et
> **validée par compilation et tests hôte** (harnais `test_profile_sync`, cas
> H-01 à H-20). La **validation matérielle** (injections CAN, 200 alternances)
> n'est pas exécutée. Voir
> [`validation-adr-0023-25-2026-10-24.md`](./validation-adr-0023-25-2026-10-24.md) ;
> les sections ci-dessous décrivent le comportement réellement implémenté.

## 0. Portée exacte

| Élément | Décision ADR-0023 |
|---|---|
| Firmware modifié | **Clavier uniquement** |
| Firmware Moteur | **inchangé** |
| `shared/protocol/` | **inchangé** |
| `CAN_PROTO_VERSION` | reste **1** |
| Trames et DLC | inchangés (`0x420`–`0x423`) |
| Mapping / `uid_position.h` | inchangés |
| Poussée du profil | uniquement sur `p send`, `p reset` ou auto-test explicite |
| Limites du pilote | inchangées |

La trame `PROFILE_STATUS` continue d'utiliser `transfer_id = 0` pour une annonce
de boot ou une réponse de sonde. La réconciliation finale réutilise cette trame :
aucune nouvelle trame ni aucun nouveau champ ne sont ajoutés.

## 1. Défaut actuel à corriger

Le comportement du code au 2026-10-24 ne satisfait pas l'ADR :

1. `begin_profile_sync()` (`firmware/clavier/src/can_link.c:903-999`) émet **un
   seul** transfert et réinitialise toutes les confirmations ;
2. un refus `NO_TRANSFER`, `BAD_SEQUENCE` ou `BAD_CRC` pose immédiatement
   `profile_sync_failed` (`:1157-1159`), puis la boucle termine l'opération au
   tour suivant (`:2421-2430`) ;
3. `handle_profile_status()` compare strictement `transfer_id` à celui de la
   tentative courante (`:1139-1141`) et jette donc un acquittement tardif ;
4. un refus écrase une confirmation `ACK_OK` (`:1158`) ;
5. la branche sync ne contrôle pas le `profile_tag32` d'une réponse OK
   (`:1152-1156`) ;
6. à l'expiration, l'opération est désactivée **avant** la sonde (`:2450-2453`) ;
7. `profile_request_probe()` ignore le retour de `start_uid_request()`
   (`:1054-1058`) ;
8. aucun compteur de reprise ou de réconciliation n'existe.

Ces huit points doivent être traités ensemble : corriger uniquement le délai ou
uniquement la répétition Moteur ne ferme pas le défaut opérationnel décrit par
l'ADR.

## 2. Modèle d'état Clavier

Les noms ci-dessous décrivent le comportement attendu. Le Codeur peut conserver
les structures existantes ou les regrouper, à condition que chaque invariant soit
observable et testable sur hôte.

### 2.1 Champs d'opération

| Champ | Type / domaine | Règle |
|---|---|---|
| `profile_sync_active` | bool | vrai du premier BEGIN jusqu'au verdict final, **sonde comprise** |
| `profile_sync_expected` | bitmask de positions | calculé **une seule fois** au début depuis les cartes en ligne, règle ADR-0016 inchangée |
| `profile_sync_attempt` | 1..3 | tentative courante |
| `profile_sync_transfer_ids[3]` | u8, 1..255 | identifiants de toutes les tentatives de l'opération |
| `profile_sync_start_ms` | u32 | début de l'opération, pour `duree=` |
| `profile_sync_deadline_ms` | u32 | échéance de la tentative courante, délai actuel inchangé |
| `profile_sync_goal` | selftest / reglage | but de l'opération |
| `profile_sync_source` | console / bouton | source existante |
| `profile_sync_expected_tag` | u32 | CRC32 du payload identique pour toutes les tentatives |
| `profile_sync_failed` | bool | **uniquement** un échec terminal ou l'échec final |
| `profile_sync_retries` | 0..2 | nombre de tentatives supplémentaires lancées |
| `profile_sync_received` | 0..N | cartes confirmées par `PROFILE_STATUS` direct |
| `profile_sync_reconciled` | 0..N | cartes confirmées par la sonde NVM |
| `profile_sync_late` | >=0 | acquittements tardifs acceptés, comptés une fois |
| `profile_probe_pending` | bool | sonde UID finale en cours |

`profile_sync_start_ms` sert au verdict global. L'échéance d'une tentative continue
de courir **après l'émission des treize trames**, comme aujourd'hui ; elle n'est
ni raccourcie ni augmentée.

### 2.2 État par carte

Le tableau `profile_card_ack[]` existant peut être étendu avec les états
équivalents suivants :

| État | Sens |
|---|---|
| `WAITING` | carte attendue, pas encore confirmée |
| `CONFIRMED_DIRECT` | réponse OK au tag attendu |
| `CONFIRMED_NVM` | sonde OK, NVM, tag attendu |
| `RETRYABLE` | dernier refus réessayable, aucune application partielle |
| `TERMINAL` | refus terminal ou divergence, aucun `0x401` |

Règles :

* `CONFIRMED_DIRECT` et `CONFIRMED_NVM` sont **définitifs** pour l'opération ;
* un refus non-OK ultérieur ne rétrograde pas une carte confirmée ;
* une réponse **OK** avec un tag différent produit toujours
  `PROFILE_DIVERGENT` et `TERMINAL`, même après une confirmation antérieure ;
* une carte déjà confirmée reste comptée dans `attendus` et satisfait la
  condition de succès ; elle n'est plus réémise ni réattendue.

## 3. Machine d'état de l'opération

### 3.1 Début d'opération

1. Valider le profil et les gardes existantes (`jeu en cours`, opération CAN en
   cours, carte attendue) : **ne pas les modifier**.
2. Calculer une seule fois `profile_sync_expected` et le tag attendu.
3. Initialiser les compteurs d'opération à zéro, toutes les cartes attendues à
   `WAITING`, `profile_sync_failed = false`.
4. Armer `profile_sync_active = true` et `profile_sync_start_ms`.
5. Lancer la tentative 1.

La règle actuelle quand `attendus = 0` n'est pas modifiée par ADR-0023.

### 3.2 Une tentative

Pour `profile_sync_attempt = 1..3` :

1. Allouer un `transfer_id` neuf dans `1..255`, différent des identifiants déjà
   utilisés dans l'opération, et l'ajouter à `profile_sync_transfer_ids`.
2. Émettre **le transfert complet**, dans l'ordre :
   `PROFILE_BEGIN`, 11 × `PROFILE_CHUNK`, `PROFILE_COMMIT`.
3. Le payload, le CRC32 et le tag sont identiques à chaque tentative. Seul
   `transfer_id` change.
4. Armer `profile_sync_deadline_ms` avec le délai existant :
   **1 000 ms** avant auto-test, **2 000 ms** pour le réglage.
5. Ne pas réinitialiser les confirmations déjà acquises.

Si l'émission locale d'une trame échoue (`send_profile_frame()` faux) :

* l'essai courant est abandonné et classé comme échec de transport ;
* si `profile_sync_attempt < 3`, passer à l'essai suivant avec un nouvel id ;
* après le troisième échec, tenter quand même la sonde finale : le Clavier peut
  encore recevoir alors qu'il n'émet plus ;
* si la sonde elle-même ne peut pas être émise, produire
  `[STATUS] code=CAN_TX_FAIL` puis l'échec final `PROFILE_SYNC_FAILED`.

### 3.3 Réception d'un `PROFILE_STATUS`

#### Branche sonde

Si `profile_probe_pending` est vrai et `transfer_id = 0`, traiter la réponse
comme une annonce d'état :

| Condition | Effet |
|---|---|
| carte attendue + en ligne + `result=OK` + `source=NVM` + tag attendu | `CONFIRMED_NVM`, `profile_sync_reconciled++` |
| même carte mais `source=DEFAULTS` | `PROFILE_FALLBACK_IN_USE`, reste non confirmée |
| tag différent | `PROFILE_DIVERGENT`, carte `TERMINAL` |
| carte non attendue, non attribuable ou hors fenêtre | comportement de diagnostic existant, aucune réconciliation |

#### Branche acquittement de transfert

1. Si l'opération est inactive, conserver le comportement existant.
2. Si `transfer_id` n'appartient pas à `profile_sync_transfer_ids`, ne pas
   l'utiliser pour confirmer ; conserver le diagnostic « acquittement inattendu »
   existant lorsqu'il est attribuable.
3. Attribuer la carte par `uid_tag` → position avec la règle ADR-0016.
4. Si la carte est déjà `CONFIRMED_*`, ignorer le doublon ou le refus.
5. Si `result = OK` :
   * tag différent → `PROFILE_DIVERGENT` + `TERMINAL` + échec terminal ;
   * tag attendu → `CONFIRMED_DIRECT`, incrémenter `profile_sync_received` ;
   * si l'id n'est pas celui de la tentative courante mais un id antérieur de la
     même opération, incrémenter `profile_sync_late` **une seule fois**.
6. Si `result != OK` alors que la carte n'est pas confirmée :
   * **ne jamais comparer le tag** : un refus porte le tag de l'ancien profil ;
   * `NO_TRANSFER`, `BAD_SEQUENCE`, `BAD_CRC` → état `RETRYABLE`, aucun
     `profile_sync_failed` ;
   * `BUSY`, `INVALID_FIELD`, `FLASH_ERROR`, `UNSUPPORTED_VERSION` → état
     `TERMINAL` et `profile_sync_failed = true` ;
   * tout autre code inconnu est traité comme terminal ;
   * journaliser le refus comme aujourd'hui, avec la position et le code.

### 3.4 Fin d'une tentative

À l'échéance ou dès qu'un état terminal est connu :

1. Si `profile_sync_failed` est vrai : aller au verdict d'échec, sans lancer
   d'essai ni réconcilier.
2. Si toutes les cartes attendues sont `CONFIRMED_DIRECT` : succès immédiat.
3. Si des cartes restent non confirmées et `profile_sync_attempt < 3` :
   * incrémenter `profile_sync_retries` ;
   * journaliser **une** ligne de reprise ;
   * lancer la tentative suivante avec un nouvel id.
4. Sinon : passer à la sonde finale.

Aucune attente supplémentaire n'est ajoutée entre les tentatives : le délai
actuel de la tentative reste le seul temps d'attente.

### 3.5 Sonde finale et verdict

1. Garder `profile_sync_active = true` pendant toute la sonde.
2. Appeler la requête UID existante via `profile_request_probe()` et **contrôler
   son retour**.
3. Si la requête ne peut pas être émise : `CAN_TX_FAIL` + échec final.
4. Laisser `profile_probe_pending` vrai pendant `CAN_UID_REQUEST_TIMEOUT_MS`
   (**500 ms**).
5. Traiter les réponses selon §3.3. Si toutes les cartes attendues sont
   confirmées avant l'échéance, le verdict peut être rendu sans attendre.
6. À l'échéance :
   * toutes confirmées → succès : promouvoir le profil actif et
     `local_profile_tag`, puis émettre `0x401` si le but est l'auto-test ;
   * au moins une non confirmée → échec : conserver l'ancien profil actif,
     `PROFILE_SYNC_FAILED`, aucun `0x401`, détail par carte ;
   * effacer `profile_probe_pending` et `profile_sync_active` après le verdict.

Une divergence `PROFILE_DIVERGENT` interdit toujours le `0x401`, même si les
autres cartes sont confirmées.

## 4. Journal attendu

### 4.1 Succès sans reprise

```text
[PROFILE] sync ok but=reglage duree=2 ms tentatives=1/3 acquittements=2/2 reconciliees=0 tardifs=0 source=console tag=0x4C961A02
```

Une seule ligne de verdict. Ne pas ajouter une seconde ligne `reglage: OK` ou
`sync debut`. Le champ `source=console|bouton` reste porté par cette ligne, comme
l'exige ADR-0017 ; la suppression de `sync debut` ne doit pas le faire perdre.

### 4.2 Succès après reprise

`restantes` est le nombre de cartes non confirmées à l'ouverture de la reprise.

```text
[PROFILE] sync reprise essai=2/3 restantes=1 tag=0x4C961A02
[PROFILE] sync ok but=reglage duree=2003 ms tentatives=2/3 acquittements=2/2 reconciliees=0 tardifs=0 source=console tag=0x4C961A02
```

### 4.3 Succès par réconciliation NVM

```text
[PROFILE] sync ok but=selftest duree=3503 ms tentatives=3/3 acquittements=1/2 reconciliees=1 tardifs=0 source=bouton tag=0x4C961A02
```

### 4.4 Échec

```text
[STATUS] code=PROFILE_SYNC_FAILED
[PROFILE] sync echec but=reglage duree=6502 ms tentatives=3/3 acquittements=1/2 reconciliees=0 source=console tag=0x4C961A02
[PROFILE] sync carte pos=0 tag=0x456B motif=ACK_TIMEOUT
```

Le détail par carte n'est émis qu'en échec. En cas de succès, `tardifs` et
`reconciliees` suffisent ; aucun détail par carte ne doit être ajouté.

`PROFILE_VERIFIED_NVM` reste un motif interne distinct, utilisable dans le
détail par carte si une réconciliation coexiste avec un échec ou si le journal de
bring-up est activé. Il ne doit jamais être compté comme un acquittement direct.

## 5. Points d'intégration dans le code actuel

| Fichier / fonction | Modification attendue |
|---|---|
| `firmware/clavier/src/can_link.c` — constantes et état `:28-35,141-174` | ajouter les champs §2, aucun changement de délai |
| `begin_profile_sync` `:903-999` | séparer début d'opération et émission d'une tentative ; ne plus faire `memset(profile_card_ack)` à chaque tentative |
| `handle_profile_status` `:1103-1170` | ensemble d'ids, états par carte, tag sur OK, pas de tag sur refus, pas de rétrogradation |
| `profile_request_probe` `:1054-1058` | retour explicite ; conserver l'opération active |
| `can_link_poll` `:2337-2468` | reprises, sonde différée, verdict final, compteurs |
| logs `[PROFILE]` `:989-1160,2421-2468` | formats §4, suppression des doublons de verdict |
| `firmware/clavier/src/bringup_console.c` `p send` | commande inchangée ; aucune nouvelle commande |
| `shared/protocol/*` | **aucune modification** |
| `firmware/moteur/*` | **aucune modification** |

Les gardes de `can_link_request_selftest`, `can_link_profile_send` et du bouton
restent actives pendant toute l'opération, sonde comprise.

## 6. Tests hôte

Le harnais `tests/host/test_profile_sync.c` est intégré à
`run_host_tests.ps1` et couvre les décisions pures (`profile_sync_logic.h`).
Les signatures et le branchement complet `can_link.c` restent couverts par la
campagne de banc : aucun test de laboratoire ne peut remplacer les injections de
trames réelles.

Cas minimaux :

| Id | Injection | Attendu |
|---|---|---|
| H-01 | 2 cartes, deux réponses OK au premier essai | succès, `tentatives=1/3`, pas de reprise, `0x401` si selftest |
| H-02 | perte totale du premier acquittement, OK au deuxième essai | `tentatives=2/3`, `profile_sync_retries=1`, succès |
| H-03 | refus réessayable `NO_TRANSFER` portant l'ancien tag, puis OK | pas de `PROFILE_DIVERGENT`, pas d'échec immédiat, succès |
| H-04 | acquittement du premier id reçu pendant le deuxième essai | accepté, `tardifs=1`, compté une fois |
| H-05 | doublon du même OK | une seule confirmation |
| H-06 | réponse OK avec tag différent | `PROFILE_DIVERGENT`, échec terminal, aucun `0x401` |
| H-07 | `BUSY`, `INVALID_FIELD`, `FLASH_ERROR`, `UNSUPPORTED_VERSION` | échec terminal immédiat, pas de reprise |
| H-08 | OK puis refus non-OK de la même carte | carte reste confirmée |
| H-09 | perte d'un BEGIN, d'un CHUNK ou du COMMIT | nouvelle tentative complète avec nouvel id, même CRC |
| H-10 | trois échecs d'acquittement, sonde NVM correcte | succès, `reconciliees=N`, `0x401` si selftest |
| H-11 | sonde avec tag différent | échec, `PROFILE_DIVERGENT`, aucun `0x401` |
| H-12 | sonde `source=DEFAULTS` à tag égal | non réconciliée, `PROFILE_FALLBACK_IN_USE`, échec |
| H-13 | aucune réponse de sonde | échec après 500 ms, `ACK_TIMEOUT` |
| H-14 | sonde impossible à émettre | `CAN_TX_FAIL`, échec explicite |
| H-15 | trois émissions locales en échec, sonde possible | sonde exécutée, pas de blocage |
| H-16 | `attendus=0` | comportement actuel préservé, aucun crash |
| H-17 | 255 → 1 sur les ids | trois ids distincts, rebouclage correct |
| H-18 | opération en cours pendant la sonde | `p send` et auto-test refusés par les gardes existantes |
| H-19 | durée et échéances | aucune reprise avant le délai, borne 3 × délai + 500 ms |
| H-20 | succès nominal | aucune ligne `sync debut` ni doublon `reglage: OK` |

Les tests doivent vérifier que les compteurs `profile_sync_retries` et
`profile_sync_reconciled` sont **par opération**, et que `profile_sync_received`
ne compte jamais un doublon.

## 7. Fichiers à revoir

| Fichier | Revue |
|---|---|
| `firmware/clavier/src/can_link.c` | **modifié** : machine d'état, reprises, sonde, compteurs, logs |
| `firmware/clavier/src/can_link.h` | **modifié si** les compteurs ou états internes y sont exposés |
| `firmware/clavier/src/profile_sync_logic.h` | **créé** : décisions pures reprises/tag/sonde, utilisées par `can_link.c` et testées sur hôte |
| `tests/host/test_profile_sync.c` (nouveau) | **créé** : harnais et cas H-01 à H-20 |
| `tests/host/run_host_tests.ps1` | **modifié** : compilation/exécution du nouveau harnais |
| `tests/tools/gen_profile_inject.c` | **inchangé**, réutilisable pour injecter un profil au banc |
| `firmware/moteur/src/can_link.c` | **aucune modification** |
| `shared/protocol/can_protocol.[ch]` | **aucune modification** |
| `docs/specs/CAN.md` | mis à jour par cette note |
| `docs/specs/profil-solenoides.md` | mis à jour par cette note |
| `docs/specs/Detail_architecture.md` | mis à jour par cette note |
| `tests/phase3b_profil_tuning.md` | mis à jour par cette note |
| `tests/procedure_adr0023_sync_profil.md` | **créé** : procédure de validation hôte et banc |

## 8. Critères d'acceptation

* une seule commande `p send` ou un seul auto-test suffit après une perte
  transitoire, sans intervention humaine ;
* une perte de BEGIN, CHUNK, COMMIT ou STATUS est couverte par une reprise
  complète ou une réconciliation NVM ;
* une carte qui détient le bon tag NVM peut clore l'opération même si tous les
  acquittements de transfert ont été perdus ;
* toute divergence de tag, source `DEFAULTS`, carte absente ou refus terminal
  reste visible et interdit `0x401` ;
* les délais et les trames existants sont inchangés ;
* zéro échec sur les tests hôte et sur la campagne de 200 alternances décrite
  dans `tests/procedure_adr0023_sync_profil.md` ;
* aucune régression des gardes de jeu en cours, d'opération CAN en cours ou de
  table absente.

## 9. Ce qui reste à démontrer au banc

La correction prouve la **récupération** d'une perte, pas son point d'origine.
Restent à mesurer :

* la présence ou l'absence du `0x423` manquant sur le fil, avec un analyseur CAN ;
* les trames d'erreur éventuelles autour des réponses `0x423` simultanées ;
* la latence réelle COMMIT → écriture flash → STATUS, par rapport à la fenêtre de
  1 s ;
* la répétition éventuelle d'une perte à chaque tentative, signature d'une
  collision ou d'un défaut de séquencement.

Aucune de ces mesures ne doit être remplacée par une conclusion matérielle
anticipée. Le protocole détaillé est dans
[`tests/procedure_adr0023_sync_profil.md`](../../tests/procedure_adr0023_sync_profil.md).

## 10. Questions ouvertes pour l'Architecte

Aucune. Les choix laissés au Codeur sont internes (noms des champs, découpage des
fonctions, harnais de test) et ne changent ni les trames, ni les seuils, ni les
règles de sécurité.
