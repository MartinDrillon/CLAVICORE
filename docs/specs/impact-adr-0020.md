# Impact ADR-0020 — comptabilité des échecs des cartes Moteur

Note de référence pour le **Codeur** et le **Validateur**.
[ADR-0020](../adr/adr-0020-comptabilite-echecs-cartes-moteur.md)
(`Accepted`, 2026-10-23) rend les cartes **comptables** de leurs échecs :
acquittement de profil répété, compteurs lisibles à la demande, refus de commande
signalé. Elle dépend d'ADR-0018 pour la trame `CARD_EVENT` (`0x424`).

> **État (2026-10-24) : exécutée et validée en compilation + tests hôte.**
> Acquittement répété, `0x403`/`0x425`, 13 compteurs, `m stats`, `COMMAND_IGNORED`
> et bascule `n` sont en place ; 413 vérifications hôte, 0 échec. Trois gardes
> d'exécution ont été ajoutées après revue du Validateur : refus de `m stats` si
> une opération est en cours ou si aucune carte n'est attendue, et
> `[STATUS] code=NO_RESPONSE` nommé pour une carte muette (une fois par
> balayage). Validation matérielle à faire.

## 0. Pourquoi (rappel du constat, pas une hypothèse)

Le 2026-10-22 puis le 2026-10-23, l'instrument est devenu muet alors que les
cartes répondaient (`p send` : `2/2` en 1-2 ms) : les commandes de note étaient
**émises** puis **refusées** par la carte, sans aucune ligne de journal. Les cinq
refus silencieux sont dans `firmware/moteur/src/can_link.c:129-166`. Cette note
les rend comptables.

## 1. Acquittement de profil répété (décision 1)

`send_profile_status_if_due` (`firmware/moteur/src/can_link.c:431-444`) émet
**une seule fois** et libère le drapeau *avant* l'émission : toute erreur de
`can2040_transmit` perd l'acquittement définitivement.

Règle :

| Élément | Valeur |
|---|---|
| Tentatives | **3** |
| Intervalle entre tentatives | **20 000 µs** |
| Contenu | identique (même `transfer_id`, même `result`) |
| Arrêt | dès qu'un `send_frame` **réussit** (un acquittement suffit) |
| Épuisement | `stats.profile_status_lost++`, compteur `6` |
| Fenêtre | 3 × 20 ms ≪ 1 000/2 000 ms : aucune modification du délai Clavier |

Ne pas modifier la trame ni `arm_profile_status`. Le Clavier est déjà idempotent
(`profile_card_ack[]`, `profile_sync_received` — ADR-0016) : ajouter un test hôte
sur un double acquittement.

## 2. Compteurs lisibles à la demande (décision 2)

### 2.1 Requête `CARD_STATS_REQUEST` — ID `0x403`, DLC 4

Émetteur : **Clavier** vers diffusion. Aucune réponse n'est acquittée par une
carte qui ne connaît pas l'identifiant (firmware antérieur) : simple silence.

| Octet | Nom | Valeurs |
|---|---|---|
| 0 | `proto_version` | `CAN_PROTO_VERSION` |
| 1 | `counter_id` | `0..12` (table §2.3) |
| 2..3 | `reply_to` | `CAN_REQUEST_ID_MIN..MAX` (LE), corrèle la réponse |

### 2.2 Réponse `CARD_STATS` — ID `0x425`, DLC 8

Émetteur : **Moteur** vers le Clavier, une trame par requête reçue.

| Octet | Nom | Valeurs |
|---|---|---|
| 0 | `proto_version` | `CAN_PROTO_VERSION` |
| 1 | `counter_id` | recopie |
| 2..3 | `uid_tag` | CRC16 LE de la carte émettrice |
| 4..7 | `valeur` | u32 LE |

Corrélation : par `uid_tag` + `counter_id` ; le `reply_to` de la requête n'est
**pas** renvoyé (8 octets pleins). Le Clavier **sérialise** donc les requêtes :
une seule en vol, réponse ou timeout court (100 ms) avant la suivante.

### 2.3 Identifiants de compteurs (v1)

| Id | Nom Moteur | Sens |
|---|---|---|
| 0 | `can_errors` | débordements des tampons RX can2040 (trames perdues) |
| 1 | `tx_fail` | refus de `can2040_transmit` |
| 2 | `rx_overrun` | débordement de la file applicative (16 entrées) |
| 3 | `solenoid_selftest_blocked` | commande refusée : auto-test en cours |
| 4 | `solenoid_ignored_tag` | commande refusée : tag inconnu |
| 5 | `solenoid_bad_channel` | commande refusée : canal hors bornes |
| 6 | `profile_status_lost` | acquittement perdu après 3 tentatives |
| 7 | `profile_commits_rejected` | commit de profil refusé |
| 8 | `profile_commits_ok` | commit de profil accepté |
| 9 | `rx_ignored` | trames reçues et ignorées |
| 10 | `solenoid_unoccupied` | commande refusée : canal non occupé (bitmap local) |
| 11 | `solenoid_rejected` | commande refusée par le pilote (BUSY/FAULT) |
| 12 | `solenoid_commands_ok` | commandes **exécutées** |

Les compteurs 3 à 12 existent déjà dans `can_link_stats_t`
(`firmware/moteur/src/can_link.h:22-38`) ; seuls `profile_status_lost` et
`can_errors`, `tx_fail`, `rx_overrun` sont à exposer tels quels.

### 2.4 Console `m stats`

Nouvelle sous-commande du Clavier (`m stats`), **à la demande uniquement** :

```text
[CARD] pos=0 stats can_errors=0 tx_fail=0 ...
[STATS] note_sent=123 cartes_ok=123 ecart=0
```

`note_sent` est le cumul Clavier (`stats.note_sent`) ; `cartes_ok` la somme des
`solenoid_commands_ok` reçus. Un écart croissant pendant qu'on joue signifie
« la carte refuse » : afficher alors les compteurs 3, 4, 5, 10 et 11.

Règles : une requête à la fois ; timeout court (100 ms) ; carte muette →
`[STATUS] code=NO_RESPONSE` existant, sans bloquer les suivantes ; ne rien
émettre pendant le jeu (même refus que la requête d'UID).

## 3. Commande ignorée signalée (décision 3)

Toute `SOLENOID_COMMAND` reçue et **non exécutée** émet un `CARD_EVENT` (ADR-0018)
`COMMAND_IGNORED`, avec la raison encodée dans `state` (§4 de la note 0018) :

| Raison | Code `state` | Compteur |
|---|---|---|
| tag inconnu | 1 | `solenoid_ignored_tag` |
| canal hors bornes | 2 | `solenoid_bad_channel` |
| auto-test en cours | 3 | `solenoid_selftest_blocked` |
| canal non occupé | 4 | `solenoid_unoccupied` |
| refus du pilote | 5 | `solenoid_rejected` (`fault` porte le défaut) |

Limiteur : **au plus un événement par seconde et par raison** (horodatage
`now_us`). Le compteur reste la trace de dernier recours si `send_frame` échoue.
Ne **jamais** exécuter autrement : on rend le refus visible, on ne le contourne
pas.

## 4. Journal des notes à l'exécution (IMP-008)

`CLAVIER_NOTE_LOG` vaut `0` par défaut : une note **exécutée** et une note
**refusée** produisent le même journal vide. Ajouter une commande console `n`
qui **bascule** l'affichage des lignes `[NOTE]` à l'exécution (aucun autre effet,
aucune persistance). C'est l'outil de corrélation frappe ↔ commande.

## 5. Fichiers à revoir

| Fichier | Revue requise |
|---|---|
| `shared/protocol/can_protocol.h` | **modifié** : `CAN_ID_CARD_STATS_REQUEST 0x403u`, `CAN_ID_CARD_STATS 0x425u`, DLC 4/8, `can_card_counter_t` (§2.3), structures. |
| `shared/protocol/can_protocol.c` | **modifié** : encodeurs/décodeurs (bornes `counter_id`, `dlc` stricts). |
| `firmware/moteur/src/can_link.c` | **modifié** : répétition de l'acquittement (§1), `handle_card_stats_request` → réponse `0x425`, `profile_status_lost`, émission `COMMAND_IGNORED` (§3) après incrément. |
| `firmware/moteur/src/can_link.h` | **modifié** : ajouter `profile_status_lost`, `card_events_sent` aux stats si absents. |
| `firmware/clavier/src/can_link.c` | **modifié** : émission `0x403` sérialisée, réception `0x425`, `m stats`, comparaison `note_sent`/`commands_ok`. |
| `firmware/clavier/src/bringup_console.c` | **modifié** : sous-commande `m stats`, bascule `n` (§4), aide `?`. |
| `firmware/clavier/src/diag_log.h/.c` | **modifié** : aucun nouveau code STATUS (réutiliser `NO_RESPONSE`) ; formateur `[CARD] … stats …` testable hôte. |
| `tests/host/*` | **modifié** : codecs `0x403`/`0x425`, double acquittement idempotent, limiteur 1/s (fonction pure), bascule `n` sans effet de bord. |
| `docs/specs/CAN.md` | **modifié** : trames `0x403`/`0x425`, tableau des compteurs (fait dans ce lot). |

Dépendance : la trame `0x424` et son énumération doivent exister (lot 0018) avant
l'émission `COMMAND_IGNORED`.

## 6. Critères d'acceptation

- `p send` sur un profil identique : `sync ok`, un seul acquittement suffit, pas
  de doublon compté (`acquittements` reste `k/N`, jamais `2k/N`).
- Une carte dont on coupe l'émission (ou dont on sature la file TX) ne fait plus
  échouer la synchronisation : 3 tentatives dans la fenêtre.
- `m stats` affiche les 13 compteurs des cartes présentes et la ligne `[STATS]`.
- Presser une touche commissionnée pendant qu'une carte refuse produit, en moins
  d'une seconde, une ligne `[CARD] … event=COMMAND_IGNORED` avec la bonne raison.
- Aucune régression sur les trames existantes : `CAN_PROTO_VERSION` reste 1.
