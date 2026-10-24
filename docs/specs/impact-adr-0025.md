# Impact ADR-0025 — journal par niveaux, verdicts conservés et statistiques agrégées

Note de référence pour le **Codeur** et le **Validateur**.
[ADR-0025](../adr/adr-0025-journal-diagnostic-niveaux-runtime-agregation.md) est
**`Accepted`** depuis le 2026-10-24.

> **État de cette note (2026-10-24).** ADR-0025 est **implémentée** et
> **validée par compilation et tests hôte** (`test_diag_out`, `test_console_tuning`,
> `test_phase3_helpers`). La **validation matérielle** (comptage de lignes,
> trafic `0x424` résiduel, filtrage sur banc) n'est pas exécutée. Voir
> [`validation-adr-0023-25-2026-10-24.md`](./validation-adr-0023-25-2026-10-24.md) ;
> les sections ci-dessous décrivent le comportement réellement implémenté.

## 0. Portée

| Élément | Décision ADR-0025 |
|---|---|
| Journal Clavier | **modifié** : niveaux runtime, horodatage commutable, catégories |
| Journal Moteur | **modifié** : événements ordinaires désactivables à la compilation |
| `0x424` | **symbole 0 renommé**, valeur/DLC/octets inchangés |
| `0x403`/`0x425` | format inchangé ; sortie `m stats` agrégée côté Clavier |
| `CAN_PROTO_VERSION` | reste **1** |
| Commandes | `n` retirée ; `l` ajoutée ; `c`, `d`, `m stats`, `p` conservées |
| Fonctionnel | aucune note, sécurité, limite ou décision de commissioning modifiée |
| Persistance | aucun réglage de journal en flash |

## 1. Catégories d'émission

Le point central reste `diag_out_printf()` / `diag_out_line()`
(`firmware/clavier/src/diag_out.c:19-50`). Aucun `printf` de diagnostic ne doit
subsister hors de ce point, y compris pour la matrice.

Chaque ligne reçoit une catégorie :

| Catégorie | Émission | Usage |
|---|---|---|
| `ALWAYS` | à tous les niveaux, y compris `l 0` | divergence, défaut, refus, action physique, mesure manquante |
| `LEVEL0` | niveaux 0, 1, 2 | verdict d'une opération explicitement demandée |
| `LEVEL1` | niveaux 1 et 2 | boot, identité, table, résumés normaux, événements ordinaires |
| `LEVEL2` | niveau 2 uniquement | notes, RAW, matrice, PUSH courants |
| `CONSOLE` | toujours, sur commande explicite | réponses `l`, `d`, `m stats`, `p`, `?` |

Règles :

* `ALWAYS` n'est jamais filtré par `l 0` ;
* `CONSOLE` contourne le filtre de niveau, mais reste soumis aux options de
  compilation ;
* `LEVEL2` n'existe pas si `CLAVIER_DIAG_TRACE=OFF` ;
* une catégorie ne modifie jamais une décision fonctionnelle, un compteur, une
  acquisition ou une coupure du pilote ;
* une ligne vide n'est jamais émise ;
* une ligne dépassant le tampon est un défaut de format, jamais une troncature
  silencieuse. `DIAG_OUT_LINE_MAX` passe de 288 à **640 avant** l'implémentation
  de la ligne agrégée de statistiques ; aucun format ne doit être livré en
  comptant sur une troncature.

## 2. Niveaux runtime et commande `l`

### 2.1 Commande

Remplacer `n` par la famille `l` dans `bringup_console.c` :

| Entrée | Effet |
|---|---|
| `l` | affiche `[CONSOLE] log niveau=<0|1|2> timestamp=<on|off> trace=<on|off>` |
| `l 0` | journal normal : verdicts, alertes, réponses console |
| `l 1` | bring-up : niveau 0 + boot/UID/table/résumés/événements ordinaires |
| `l 2` | trace : niveau 1 + notes/RAW/matrice/PUSH (si compilé) |
| `l t` | bascule le préfixe `[T+… ms]` |
| autre | `[CONSOLE] l -> usage: l [0|1|2|t]`, sans changement d'état |

* Une réponse à `l` est toujours affichée, même à `l 0`.
* `l 2` avec `CLAVIER_DIAG_TRACE=OFF` répond
  `[CONSOLE] l -> niveau 2 indisponible (CLAVIER_DIAG_TRACE=OFF)` et conserve le
  niveau courant.
* Le niveau n'est **pas** persisté en flash. Le défaut est
  `CLAVIER_DIAG_DEFAULT_LEVEL` (1 au bring-up, 0 pour un build final). Une valeur
  hors 0–2 retombe sur 1 à la compilation ; si TRACE est OFF, le maximum est 1.
* `l t` ne change ni le contenu, ni le niveau, ni les catégories : il ajoute ou
  retire uniquement le préfixe temporel.
* `n` disparaît de l'aide, du parseur et des tests. `c` reste la commande
  d'**acquisition** de calibration, pas un niveau de journal.

### 2.2 Réponses console

`d`, `m stats`, `p get/show/diff/fields` et `?` restent toujours rendus à
`l 0`. Le niveau filtre les émissions spontanées, pas la réponse à une question.

## 3. Matrice des lignes existantes

Cette matrice est normative pour le filtrage. Les tags restent ceux du journal
actuel, sauf `[KEY]` qui remplace les lignes libres de matrice et
`MANUAL_TEST_STARTED` qui remplace le libellé de l'événement 0.

| Source / tag | Exemples / contenu | Catégorie | Notes |
|---|---|---|---|
| `[BOOT]` | version, table, identité | `LEVEL1` | aucune ligne au boot sain à `l 0` |
| `[HELP]` | aide au boot | `LEVEL1` | `?` explicite → `CONSOLE` |
| `[TABLE] loaded/state/pos` | état normal de table | `LEVEL1` | pas d'impression automatique à `l 0` |
| `[TABLE] invalid/diff` | table invalide, UID changé | `ALWAYS` | divergence jamais masquée |
| `[TABLE] bitmap conserve` | mesure non concluante | `ALWAYS` | ADR-0021 |
| dump `d` | table + mapping complet | `CONSOLE` | réponse explicite uniquement |
| `[MAP]` | mapping automatique | `LEVEL1` | plus d'impression automatique à `l 0` |
| `[UID]` | scan/réponse normale | `LEVEL1` | divergence UID → `ALWAYS` |
| `[NOTE]` | note routée | `LEVEL2` | supprimée à `l 0`/`l 1` |
| `[NOTE_DROPPED]` | note non routée | `ALWAYS` | cause conservée |
| `[MODE]` | `maintenance=ON|OFF` | `ALWAYS` | changement de mode de sécurité |
| `[PUSH] start/stop` | poussée maintenance courante | `LEVEL2` | timeout/refus → `ALWAYS` |
| `[PROFILE] sync ok/reprise` | verdict d'une commande explicite | `LEVEL0` | une reprise = une ligne |
| `[PROFILE] sync echec/carte/detient` | échec, divergence, détail | `ALWAYS` | détail par carte seulement en échec ou `l 1` |
| `sync debut`, `reglage: OK/ECHEC` | doublons | **supprimés** | source conservée dans le verdict final |
| `[STATUS]` erreur/refus/divergence | codes `21`, `22`, `24`–`31` (refus, carte absente, table partielle/différente/invalide, divergence, doublon, non calibré, busy, défaut, maintenance, provisoire), `32`+ (`PROFILE_*`, `UID_*`, `BAD_*`, `NO_RESPONSE`, `CAN_TX_FAIL`, `CARD_REBOOTED`, `TRIGGER_BLOCKED`, `CHANNEL_FAULT_IDLE`) | `ALWAYS` | jamais masqué par le niveau |
| `[STATUS]` progression normale | `20` `SELFTEST_STARTED`, `23` `TABLE_WRITTEN` | `LEVEL1` | le verdict final reste `[SUMMARY]`/`[PROFILE]` |
| `[CARD]` reboot/état | reboot, état de carte | reboot `ALWAYS`, état `LEVEL1` | `CARD_REBOOTED` jamais masqué |
| `[CARD] event` santé/action | `MANUAL_TEST_STARTED`, `TRIGGER_BLOCKED`, `CHANNEL_FAULT`, `SELFTEST_ABORTED`, premier `COMMAND_IGNORED` ciblé | `ALWAYS` | limiteur 1/s par motif |
| `[CARD] event` ordinaire | `TRIGGER_PENDING`, `TRIGGER_SENT`, `CHANNEL_FAULT_CLEARED` | `LEVEL1` | retirés du build final si TRACE OFF |
| `[CARD] pos=N stats` | une ligne agrégée par carte | `CONSOLE` | jamais périodique |
| `[SELFTEST]` carte OK | résumé normal par carte | `LEVEL1` | résultat non OK → `ALWAYS` |
| `[SELFTEST]` `ohm_manquants` | canaux demandés sans OHM | `ALWAYS` | agrégé par position |
| `[SELFTEST_OHM]` ordinaire | estimation par canal | `LEVEL1` | |
| `[SELFTEST_OHM] warn` | `warn=i100>3.0A` | `ALWAYS` | jamais filtré |
| `[SELFTEST_RAW]` | `idle/probe`, `delta/ohm_est` | `LEVEL2` | acquisition `c` indépendante |
| `[SUMMARY]` | résumé final auto-test, `calib=ON|OFF` | `LEVEL0` | émis après une demande explicite |
| `[TRIGGER]` lancement | source/requête | `LEVEL1` | refus → `ALWAYS` |
| `[TRIGGER]` refus | déclenchement bloqué | `ALWAYS` | |
| `[KEY]` | comportement de matrice (remplace les lignes libres) | `LEVEL2` | un seul format tagué |
| `[STATS]` | bilan `note_sent/cartes_ok/ecart` | `CONSOLE` | avec `m stats` |
| `[CONSOLE]` | réponse à une commande | `CONSOLE` | toujours rendue |

### 3.1 Lignes de matrice

Les deux `diag_out_printf("position …")` de `firmware/clavier/src/main.c:264,269`
sont remplacés par une ligne taguée unique émise à `LEVEL2`, par exemple :

```text
[KEY] pos=<0..79> behavior=<nom> state=<n> event=<note|unused|...>
```

Le format exact reprend les champs utiles actuels ; aucun `printf` parallèle ne
doit subsister. Les lignes ne sont pas émises à `l 0`/`l 1`.

## 4. `m stats` agrégé

### 4.1 Collecte

Le balayage reste **séquentiel**, une requête `0x403` en vol, timeout 100 ms. Le
Clavier doit conserver, par position :

* la valeur reçue pour chaque `counter_id` ;
* un booléen de réponse par compteur ;
* le fait qu'au moins un compteur historique (0–12) a répondu ;
* les compteurs `note_sent` et la somme `commands_ok` pour le bilan.

Ne pas imprimer pendant le balayage. Imprimer après la fin du dernier compteur.

### 4.2 Format

Une seule ligne par carte, sans `[CARDSTATS]` :

```text
[CARD] pos=N stats can_errors=<v> tx_fail=<v> rx_overrun=<v> solenoid_commands_ok=<v> solenoid_rejected=<v> [autres=…] [ext=n/a]
[STATS] note_sent=<n> cartes_ok=<m> ecart=<n-m>
```

Règles :

* les cinq champs `can_errors`, `tx_fail`, `rx_overrun`,
  `solenoid_commands_ok` et `solenoid_rejected` sont toujours présents, même à
  zéro : ils servent au diagnostic et au bilan ;
* les autres compteurs sont ajoutés dans l'ordre des IDs **uniquement s'ils sont
  non nuls** ;
* si ADR-0024 est implémentée, les IDs 13–17 sont ajoutés selon la même règle ;
* si une carte répond aux IDs 0–12 mais pas aux IDs 13–17, ajouter un seul champ
  `ext=n/a` et ne pas imprimer `NO_RESPONSE` pour cette carte ;
* si la carte ne répond à aucun compteur, aucune ligne `[CARD] ... stats` n'est
  produite ; `[STATUS] code=NO_RESPONSE pos=N` est émis une seule fois ;
* la ligne complète doit tenir dans le tampon ; aucune valeur ne doit être
  tronquée. `DIAG_OUT_LINE_MAX` est porté à **640** avant cette ligne (§1) ;
* aucun nouvel identifiant de ligne n'est créé.

Exemple minimal deux cartes :

```text
[CARD] pos=0 stats can_errors=0 tx_fail=0 rx_overrun=0 solenoid_commands_ok=61 solenoid_rejected=0
[CARD] pos=2 stats can_errors=0 tx_fail=0 rx_overrun=0 solenoid_commands_ok=62 solenoid_rejected=0 ext=n/a
[STATS] note_sent=123 cartes_ok=123 ecart=0
```

L'écart est calculé selon la règle existante ; il n'est pas modifié par cette
ADR. `note_sent` compte ON+OFF côté Clavier, `cartes_ok` la somme des
`solenoid_commands_ok`.

### 4.3 Avant/après

| Situation | Lignes `m stats` pour 2 cartes |
|---|---|
| Avant ADR-0025, 13 compteurs | 26 `[CARD]` + 1 `[STATS]` |
| Avant ADR-0025, avec ADR-0024 (18) | 36 `[CARD]` + 1 `[STATS]` |
| Après ADR-0025 | 2 `[CARD]` + 1 `[STATS]` |

## 5. RAW, calibration et verdicts

`c` conserve son rôle d'acquisition de calibration et de calcul OHM. `l 2`
contrôle uniquement l'impression des RAW.

À `l 0`/`l 1` :

* les données sont toujours collectées et calculées ;
* les `[SELFTEST_OHM]` ordinaires sont `LEVEL1` ;
* une ligne `[SELFTEST_OHM]` avec `warn=i100>3.0A` est `ALWAYS` ;
* chaque canal demandé sans OHM est agrégé par position dans
  `[SELFTEST] ... ohm_manquants=0xNNN` (`ALWAYS`), pas dans des RAW filtrés ;
* `[SUMMARY]` porte `calib=ON` ou `calib=OFF`, pour ne pas confondre absence de
  mesure et échec de mesure ;
* si `c` est OFF, aucune mesure n'est demandée et `ohm_manquants` ne transforme
  pas l'absence en erreur.

Un `[SELFTEST]` de carte est `LEVEL1` quand le résultat est OK sans OHM manquant.
Il devient `ALWAYS` si le statut n'est pas OK, si un OHM demandé manque ou si un
avertissement de courant existe.

## 6. Événement 0 : `MANUAL_TEST_STARTED`

Renommer le symbole partagé, sans changer sa valeur ni son payload :

```text
CAN_CARD_EVENT_BUTTON_SHORT_PRESS = 0
    -> CAN_CARD_EVENT_MANUAL_TEST_STARTED = 0
```

À mettre à jour ensemble :

* `shared/protocol/can_protocol.h` (symbole) ;
* `shared/protocol/can_protocol.c` (tests de validité, valeur inchangée) ;
* `firmware/moteur/src/main.c:148-149` (émetteur) ;
* `firmware/clavier/src/can_link.c:1077-1101` (récepteur) ;
* `firmware/clavier/src/diag_log.c:98` (libellé CDC) ;
* `tests/host/test_phase3_helpers.c` (vérifier les assertions d'événement 0 ;
  aucune occurrence du libellé à renommer n'y figure aujourd'hui) ;
* `docs/specs/CAN.md`, `impact-adr-0018.md`, procédures et outils.

L'événement reste émis au même instant et avec le même `channel`, `state`,
`fault`, `uid_tag` et `reserved=0`. Un appui long peut produire
`MANUAL_TEST_STARTED`, puis `TRIGGER_PENDING`, puis `TRIGGER_SENT`.

## 7. Bruit de routage et événements Moteur

Dans `handle_solenoid_command()` (`firmware/moteur/src/can_link.c:157-161`), un
`uid_tag` étranger :

1. incrémente toujours `solenoid_ignored_tag` ;
2. retourne sans appeler `report_ignored_command()` ;
3. n'émet aucun `CARD_EVENT` et ne consomme pas le limiteur.

Les autres refus restent inchangés et conservent le limiteur 1/s par motif. Le
premier refus de chaque motif reste `ALWAYS` côté Clavier ; les répétitions sont
comptées si les compteurs sont compilés.

## 8. Options de compilation

| Option | Défaut bring-up | Effet |
|---|---|---|
| `CLAVIER_DIAG_DEFAULT_LEVEL` | `1` | niveau initial 0–2 ; 1 si invalide ; plafonné à 1 si TRACE OFF |
| `CLAVIER_DIAG_TRACE` | `ON` | inclut `l 2`, matrice, notes, RAW et PUSH courants ; OFF retire ces traces et le niveau 2. La commande `m stats` reste compilée, mais sans compteurs Moteur (`MOTEUR_DIAG_TRACE=OFF`) elle ne peut pas aboutir ; ne pas l'utiliser dans ce profil. |
| `MOTEUR_DIAG_TRACE` | `ON` | inclut `TRIGGER_PENDING`, `TRIGGER_SENT`, `CHANNEL_FAULT_CLEARED`, compteurs et répondeur `0x403`/`0x425` ; OFF retire ces outils |
| `CLAVIER_SELFTEST_CALIBRATION` | `ON` | inchangé : autorise l'acquisition de calibration |
| `CLAVIER_CDC_CONSOLE` | `ON` | inchangé : transporte la console et le journal |
| `MOTEUR_ENABLE_CLOCK_TEST` | `ON` | inchangé : diagnostic d'horloge indépendant |

`MOTEUR_DIAG_TRACE=OFF` ne retire **pas** les événements de santé/action :
`MANUAL_TEST_STARTED`, `TRIGGER_BLOCKED`, `CHANNEL_FAULT`, `SELFTEST_ABORTED`
et la première occurrence des refus ciblés restent émis ; les trames UID,
profil, auto-test et commandes ne changent pas. Leur chemin d'encodage `0x424`
reste compilé.

`CLAVIER_DIAG_TRACE=OFF` ne retire aucun verdict :
divergences, défauts, `NOTE_DROPPED`, `PROFILE_SYNC_FAILED`, `[SUMMARY]` et
réponses aux commandes fonctionnelles restent disponibles. La commande `m stats`
reste compilée mais, avec `MOTEUR_DIAG_TRACE=OFF`, aucun compteur Moteur ne
répond : ne pas l'utiliser dans ce profil. Le `d` de commissioning reste.

## 9. Fichiers à revoir

| Fichier | Revue |
|---|---|
| `firmware/clavier/src/diag_out.c/.h` | **modifié** : catégories, niveau, timestamp, buffer stats si nécessaire |
| `firmware/clavier/src/diag_log.c/.h` | **modifié** : `MANUAL_TEST_STARTED`, formats, `calib=`, `ohm_manquants` |
| `firmware/clavier/src/bringup_console.c` | **modifié** : `l`, retrait `n`, aide |
| `firmware/clavier/src/can_link.c` | **modifié** : catégories des lignes, stats agrégées, tag étranger, RAW/OHM |
| `firmware/clavier/src/card_counter_names.h` | **créé** : noms purs des 18 compteurs, testés |
| `firmware/clavier/src/stats_probe_logic.h` | **créé** : décisions `ext=n/a`/carte muette, testées |
| `firmware/clavier/src/stats_line_format.h` | **créé** : formateur pur de la ligne carte et du bilan, testé |
| `firmware/clavier/src/main.c` | **modifié** : tag `[KEY]`, catégories des lignes de matrice |
| `firmware/clavier/CMakeLists.txt` | **modifié** : `CLAVIER_DIAG_DEFAULT_LEVEL`, `CLAVIER_DIAG_TRACE` |
| `firmware/moteur/CMakeLists.txt` | **modifié** : `MOTEUR_DIAG_TRACE` |
| `firmware/moteur/src/can_link.c` | **modifié** : tag étranger sans événement, événements ordinaires sous option |
| `firmware/moteur/src/main.c` | **modifié** : événements ordinaires sous option, valeur 0 renommée |
| `shared/protocol/can_protocol.h` | **modifié** : renommage du symbole 0 |
| `shared/protocol/can_protocol.c` | **vérifié** : valeur 0, validité inchangée |
| `tests/host/test_phase3_helpers.c` | **modifié** : événement 0, filtres, format stats, `[KEY]`, `[SUMMARY]` |
| `tests/host/test_console_tuning.c` | **modifié** : `l`, `l t`, absence de `n`, réponses console |
| `tests/host/test_diag_out.c` | **créé** : niveaux/catégories/horodatage du point d'émission réel |
| `tests/host/run_host_tests.ps1` | **modifié** : nouveaux harnais/formateurs |
| `tests/phase3b_bout_en_bout.md` | **modifié** : `l`, `m stats`, événement renommé |
| `tests/procedure_adr0025_journal_niveaux.md` | **créé** : validation hôte et banc |
| `docs/specs/CAN.md` | **modifié** : événement 0 et statistiques |
| `docs/specs/impact-adr-0018.md` | **modifié** : événement 0 |
| `docs/specs/impact-adr-0020.md` | **complété** : agrégation et niveaux |
| `docs/specs/Detail_architecture.md` | **modifié** : état ADR-0025 |
| `docs/specs/impact-adr-0024.md` | **complété** : interaction des options/stats |

Inchangés : `firmware/moteur/src/can_link.h` hors compteurs ADR-0024, les codecs
`0x403`/`0x424`/`0x425`, `CAN_PROTO_VERSION`, le mapping et les profils.

## 10. Tests hôte

| Id | Scénario | Attendu |
|---|---|---|
| L-01 | `l` | niveau et timestamp affichés |
| L-02 | `l 0`, `l 1`, `l 2` | niveau courant correct |
| L-03 | `l t` | préfixe retiré/remis sans changer le contenu |
| L-04 | `l 2` avec TRACE OFF | refus explicite, niveau inchangé |
| L-05 | `n` | commande absente de l'aide et rejetée |
| L-06 | `l 0` + note | aucun `[NOTE]`, aucun événement TAG ; la note joue |
| L-07 | `l 2` + note | une ligne `[NOTE]` |
| L-08 | boot sain `l 0` | aucune ligne périodique |
| L-09 | `l 1` | boot/UID/table/map visibles |
| L-10 | alertes injectées à tous niveaux | toujours visibles |
| L-11 | `PUSH` start/stop | `l 2` seulement ; timeout/refus toujours |
| L-12 | RAW `l 0`/`l 1` | absents ; `l 2` présents |
| L-13 | OHM manquant | `ohm_manquants` visible à `l 0` |
| L-14 | warning courant | `[SELFTEST_OHM] warn` visible à `l 0` |
| L-15 | `calib=OFF` | `[SUMMARY]` le porte |
| L-16 | `m stats` 2 cartes | 2 lignes `[CARD]` + 1 `[STATS]` |
| L-17 | compteurs non nuls | ajoutés, zéros omis sauf champs obligatoires |
| L-18 | carte ancienne | `ext=n/a`, pas de `NO_RESPONSE` |
| L-19 | carte muette | aucune ligne stats + un `NO_RESPONSE` |
| L-20 | réponse hors balayage | ignorée, aucun affichage |
| L-21 | tag étranger | aucun `CARD_EVENT`, compteur incrémenté |
| L-22 | refus ciblé répété | première ligne par motif/seconde, compteur cumulatif |
| L-23 | événement 0 | `MANUAL_TEST_STARTED`, valeur 0 dans le codec |
| L-24 | `MOTEUR_DIAG_TRACE=OFF` | santé/action conservés, traces ordinaires absentes |
| L-25 | `CLAVIER_DIAG_TRACE=OFF` | aucun niveau 2 ; `m stats` sans réponse si le Moteur n'a pas de compteurs ; verdicts conservés |

## 11. Critères d'acceptation

* `l 0` sain ne produit aucune ligne périodique ni note/TAG ; les alertes
  restent visibles ;
* `m stats` produit au plus une ligne par carte plus le bilan, sans troncature ;
* une carte ancienne n'est pas déclarée morte à cause de `ext=n/a` ;
* un OHM demandé mais absent et un warning de courant sont visibles à `l 0` ;
* le symbole 0 est `MANUAL_TEST_STARTED` avec la même valeur et le même payload ;
* le tag étranger n'émet plus d'événement ;
* les options finales retirent les traces optionnelles sans retirer les
  événements de santé/action ni les verdicts ;
* `CAN_PROTO_VERSION` reste 1 et aucune trame fonctionnelle ne change.

## 12. Questions ouvertes pour l'Architecte

Aucune. La requête `m stats` reste compilée ; c'est le répondeur Moteur
(`MOTEUR_DIAG_TRACE=OFF`) qui retire les compteurs à la source. Aucune trame ni
aucun seuil n'est modifié.
