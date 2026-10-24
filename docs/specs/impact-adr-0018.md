# Impact ADR-0018 — retour d'état du bouton et des canaux Moteur (`CARD_EVENT` 0x424)

Note de référence pour le **Codeur** et le **Validateur**.
[ADR-0018](../adr/adr-0018-retour-etat-bouton-canaux-moteur-vers-clavier.md)
(`Accepted`, 2026-10-23) ajoute une trame **Moteur → Clavier** de compte rendu
d'événements : appui court (test manuel), déclenchement en attente / émis /
bloqué, défauts de canal, commande ignorée, auto-test avorté.

> **État (2026-10-24) : exécutée et validée en compilation + tests hôte.**
> Trame `0x424`, énumération des 8 événements, émission Moteur, réception Clavier
> et codes `0x38`/`0x39` sont en place ; 413 vérifications hôte (390 + 23),
> 0 échec. La **validation matérielle** reste à faire (voir
> `tests/phase3b_bout_en_bout.md`, test 7).

Les chemins de ligne cités sont ceux de l'état du 2026-10-23 : point d'entrée,
pas référence stable. Les fichiers sous `.kilo/worktrees/` sont des copies de
travail : à exclure.

## 0. Règle à implémenter

Le Clavier est seul décideur (invariants 2 et 3) ; la carte **informe**. Une
action locale du Moteur — appui de bouton, blocage de déclenchement, défaut de
canal, refus de commande — ne doit plus être invisible côté Clavier
(invariant 1).

| | Avant | Après (ADR-0018) |
|---|---|---|
| Action locale du Moteur | invisible (aucune console) | trame `0x424` + lignes `[CARD]` |
| `CAN_PROTO_VERSION` | 1 | **inchangé** (trame additive) |
| Comportement des solénoïdes | — | **inchangé** |
| Déclenchement `0x402` | conditionné aux 12 canaux IDLE | **inchangé** (le blocage devient visible) |

## 1. Trame `CARD_EVENT` — ID `0x424`, DLC 8

Émetteur : **Moteur** vers le Clavier, de sa propre initiative, sur transition.
Récepteur : Clavier ; les **autres cartes l'ignorent** (`rx_ignored`) : ce n'est
pas une commande.

| Octet | Nom | Valeurs / remarques |
|---|---|---|
| 0 | `proto_version` | `CAN_PROTO_VERSION` |
| 1 | `event` | voir §2 |
| 2 | `channel` | `0..11`, ou `0xFF` si sans objet |
| 3 | `state` | valeur `SOLENOID_STATE_*` ; **pour `COMMAND_IGNORED`, porte la raison** (§4) |
| 4 | `fault` | valeur `SOLENOID_FAULT_*` ; `0` si sans objet |
| 5..6 | `uid_tag` | CRC16 LE de la carte émettrice |
| 7 | `reserved` | **doit être émis à `0`** |

Décodage : refuser (`BAD_DLC` / `BAD_VALUE`) si `dlc != 8`, `event` inconnu,
`channel > 11` et `channel != 0xFF`, `reserved != 0`, ou `uid_tag == 0`.
Réserver l'identifiant dans le plan (`can_protocol.h:26-49`) et vérifier qu'il
n'est pas dans la zone interdite (`>= 0x7F0`).

## 2. Événements (v1) — énumération complète, à poser en une seule fois

| Valeur | Nom | `channel` | `state` | `fault` | Émetteur |
|---|---|---|---|---|---|
| 0 | `BUTTON_SHORT_PRESS` | canal du test manuel | état après `note_on` | `0` | `main.c` |
| 1 | `TRIGGER_PENDING` | `0xFF` | `0` | `0` | `main.c` |
| 2 | `TRIGGER_SENT` | `0xFF` | `0` | `0` | `main.c` |
| 3 | `TRIGGER_BLOCKED` | premier canal non `IDLE` | état de ce canal | défaut éventuel | `main.c` |
| 4 | `CHANNEL_FAULT` | canal fautif | état | défaut | `can_link.c`/`ipropi_selftest.c` |
| 5 | `CHANNEL_FAULT_CLEARED` | canal | état après reset | `0` | `can_link.c` |
| 6 | `COMMAND_IGNORED` | canal de la commande (`0xFF` si hors bornes) | **raison** (§4) | défaut si raison = pilote | `can_link.c` (ADR-0020) |
| 7 | `SELFTEST_ABORTED` | canal en cours de mesure | état | `0` | `ipropi_selftest.c` (ADR-0021) |

Les événements 6 et 7 sont **déclarés ici** mais émis par les lots 0020 et 0021 :
une seule migration de `shared/protocol/`, trois lots d'émission.

## 3. Règles d'émission (Moteur)

- **Une seule émission par transition** : aucun événement répété tant que l'état
  ne change pas ; aucun événement périodique.
- `TRIGGER_BLOCKED` : émis **une fois**, après un délai borné de **200 ms**
  suivant `TRIGGER_PENDING` si `all_channels_idle()` est toujours faux. Le canal
  rapporté est le **premier** canal non `IDLE` dans l'ordre `0..11`.
- Un `send_frame` refusé (file TX pleine) ne doit pas faire perdre l'information :
  l'événement n'est pas réémis, mais le compteur correspondant est incrémenté
  (lisible via `CARD_STATS`, ADR-0020). Ne **jamais** bloquer la boucle pour
  réémettre.
- Aucun événement ne doit être émis pendant l'écriture flash du profil
  (`flash_safe_execute`, 40-70 ms d'interruptions coupées) : l'émission se fait
  au tour suivant.

## 4. Encodage de la raison de `COMMAND_IGNORED`

`state` porte un code de raison (le champ `state` du pilote n'a pas de sens pour
un refus) :

| Code | Raison | Compteur associé (ADR-0020) |
|---|---|---|
| 1 | `uid_tag` inconnu | `solenoid_ignored_tag` |
| 2 | canal hors bornes | `solenoid_bad_channel` |
| 3 | auto-test en cours | `solenoid_selftest_blocked` |
| 4 | canal non occupé (bitmap local) | `solenoid_unoccupied` |
| 5 | refus du pilote (BUSY/FAULT) | `solenoid_rejected` (`fault` porte le défaut) |

## 5. Réception Clavier

1. `handle_frame` route `0x424` vers `handle_card_event()`.
2. Attribution : `profile_seed_index_for_tag(uid_tag)` (déjà en place, ADR-0016)
   → **position stockée**. `uid_tag` inconnu : `[CARD] uid=<uid> state=NOT_OK` +
   `[STATUS] code=UID_NOT_IN_TABLE` (jamais un numéro de position supposé).
3. Journal, une ligne par événement :
   `[CARD] pos=N event=<NOM> ch=<n> state=<s> fault=<f>`.
4. `STATUS` : `0x38` `TRIGGER_BLOCKED` (événement 3) et `0x39`
   `CHANNEL_FAULT_IDLE` (événement 4). Les événements 0, 1, 2, 5, 6, 7 sont des
   lignes simples.
5. Un `CARD_EVENT` **n'est jamais** un acquittement de profil ni une réponse à une
   requête d'UID : ne toucher ni `profile_sync_received`, ni
   `uid_inflight_responses`.

## 6. Fichiers à revoir

| Fichier | Revue requise |
|---|---|
| `shared/protocol/can_protocol.h` | **modifié** : `CAN_ID_CARD_EVENT 0x424u`, `CAN_DLC_CARD_EVENT 8u`, `can_card_event_t` (8 valeurs), `can_card_event_msg_t`. Documenter le plan d'ID. |
| `shared/protocol/can_protocol.c` | **modifié** : `can_encode_card_event` / `can_decode_card_event` (contrôles §1). |
| `firmware/moteur/src/can_link.c` | **modifié** : `send_card_event(...)` (encodage + `send_frame`), `rx_ignored` sur réception `0x424`, compteur `card_events_sent`. |
| `firmware/moteur/src/main.c` | **modifié** : émission 0 → 3 aux transitions du bouton (`:135-152`, `:168-179`), avec le délai de 200 ms du §3. |
| `firmware/moteur/src/ipropi_selftest.c` | **modifié (0021)** : événement 7 au `finish_now(ABORTED)` (`:295-297`). |
| `firmware/clavier/src/can_link.c` | **modifié** : routage, `handle_card_event`, attribution, lignes, compteurs `card_events`, `trigger_blocked`, `channel_fault_idle`. |
| `firmware/clavier/src/diag_log.h/.c` | **modifié** : codes `0x38`/`0x39` + libellés ; `diag_format_card_event_line()` (pure, testable hôte) ; nom de chaque événement. |
| `tests/host/test_phase3_helpers.c` | **modifié** : aller-retour des 8 événements, rejets (`dlc`, `event` inconnu, `channel` invalide, `reserved`, `uid_tag` nul), format de la ligne. |
| `docs/specs/CAN.md` | **modifié** : section `0x424` + codes `0x35`–`0x39` (fait dans ce lot). |

Aucune autre utilisation de `shared/protocol` n'est impactée : les codecs
existants (`0x100`, `0x400`–`0x423`, `0x600`, `0x601`) ne changent pas.

## 7. Critères d'acceptation

- Les deux cibles compilent sans erreur ; `run_host_tests.ps1` passe (les
  vérifications ajoutées couvrent le codec et le formateur).
- Un appui court sur le bouton Moteur produit `[CARD] pos=N event=BUTTON_SHORT_PRESS ch=…`.
- Un appui long avec un canal bloqué produit `TRIGGER_PENDING` puis
  `TRIGGER_BLOCKED` (canal nommé) et `[STATUS] code=TRIGGER_BLOCKED pos=N` — le
  `0x402` n'est pas émis.
- Aucun `CARD_EVENT` ne modifie le résultat d'une synchronisation de profil.
- Une carte qui n'émet pas `0x424` (firmware antérieur) ne provoque **aucune**
  erreur : absence d'événement ≠ événement absent (le journal reste valide).
