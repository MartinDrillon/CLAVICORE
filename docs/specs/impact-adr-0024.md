# Impact ADR-0024 — réveil permanent après tWAKE et refus pilote comptés par cause

Note de référence pour le **Codeur** et le **Validateur**.
[ADR-0024](../adr/adr-0024-reveil-solenoides-permanent-diagnostic-refus.md) est
**`Accepted`** depuis le 2026-10-24.

> **État de cette note (2026-10-24).** ADR-0024 est **implémentée** et
> **validée par compilation et tests hôte** (`test_solenoid_wake`, W-01 à W-09,
> et `test_phase3_helpers`, C-01 à C-10). La **validation matérielle**
> (campagnes uptime 150 min et 24 h) n'est pas exécutée. Voir
> [`validation-adr-0023-25-2026-10-24.md`](./validation-adr-0023-25-2026-10-24.md) ;
> les sections ci-dessous décrivent le comportement réellement implémenté.

## 0. Portée

| Élément | Décision ADR-0024 |
|---|---|
| Firmware modifié | **Moteur** (réveil et compteurs) + petite revue Clavier pour les noms/IDs |
| `shared/protocol/` | **modifié** : IDs de compteurs 13–17, `COUNT=18` |
| Trames `0x403`/`0x425` | **format et DLC inchangés** |
| `CAN_PROTO_VERSION` | reste **1** |
| Profil, limites, phases, duty | inchangés |
| Récupération automatique | **aucune** |
| `uid_position.h`, mapping | inchangés |

L'extension des compteurs est additive : les IDs 0 à 12 gardent leur valeur et
leur sens. Une carte ancienne ne répond pas aux IDs 13–17 ; le Clavier doit
l'afficher comme indisponible, pas comme une carte morte.

## 1. Défaut de réveil à corriger

### 1.1 État actuel démontré

Dans `firmware/moteur/src/solenoid_driver.c` :

* `g_wake_deadline_us` est la seule mémoire du réveil (`:84`) ;
* `solenoid_global_wake()` arme `now + wake_delay_us` (`:92-95`) ;
* `global_ready()` exige `global_awake()` et
  `(uint32_t)(now - deadline) < 0x80000000` (`:105-107`) ;
* `solenoid_driver_tick()` retourne avant de lire le réveil si l'état est FAULT
  (`:297-300`) ;
* `solenoid_global_sleep()` n'a aucun appelant.

Le réveil est pourtant **one-shot** : une fois `wake_delay_us` écoulé, il ne doit
plus jamais redevenir faux. Le test actuel est vrai pendant une demi-période de
2³² µs puis faux pendant l'autre moitié. Le basculement apparaît à
**2 147,486 s** après le boot (35 min 47,486 s), puis le défaut se répète tous
les **4 294,967 s** (71 min 34,967 s).

Conséquences : `note_on`, `probe_start`, `push_start` et `reset_fault` renvoient
`SOLENOID_ERR_SLEEP` sans que le Clavier voie un `NOTE_DROPPED`. La surveillance
NFAULT de `tick` est également sautée pendant ces fenêtres.

### 1.2 Verrou de réveil

Ajouter un état statique `g_wake_latched` dans `solenoid_driver.c`, sans changer
l'API publique de `solenoid_driver.h` :

```text
g_wake_latched = false à l'init

solenoid_global_wake(now):
    g_wake_latched = false
    g_wake_deadline_us = now + profile()->wake_delay_us
    nSLEEP = haut

solenoid_global_sleep() / sleep_init():
    g_wake_latched = false
    deadline effacée ou ignorée
    nSLEEP = bas

global_ready(now):
    si !global_awake()        -> faux
    si g_wake_latched         -> vrai
    si échéance atteinte      -> g_wake_latched = true ; vrai
    sinon                     -> faux
```

La comparaison d'échéance reste la comparaison signée sûre au voisinage du wrap
32 bits (`(int32_t)(now - deadline) >= 0`). **Elle ne suffit pas seule** : sans le
verrou, elle redevient fausse après 2³¹ µs. Le verrou est la correction ; le test
signé n'est que le passage initial.

`solenoid_global_sleep_init()` doit explicitement remettre le verrou à faux, même
si l'initialisation statique le fait déjà. `solenoid_global_wake()` ne doit
**jamais** poser le verrou : il arme une nouvelle attente.

### 1.3 Évaluation avant tout retour FAULT

`solenoid_driver_tick(now_us)` doit évaluer le réveil **en tête de fonction**,
avant le retour anticipé de l'état FAULT et avant toute autre sortie :

```text
bool ready = global_ready(now_us);
if (state == FAULT) return;
...
if (ready && nfault_asserted) { ... }
```

Ainsi, même si les douze canaux sont en FAULT avant l'échéance, le premier tick
qui suit le délai pose le verrou. Le verrou est un état global, pas un effet de
bord d'un canal particulier.

### 1.4 Ce qui ne change pas

* `wake_delay_us` conserve sa valeur et sa sémantique de délai initial ;
* les phases IMPACT/PULL/HOLD/RELEASE, les duty, les maxima et les repos ne
  changent pas ;
* aucun `solenoid_global_wake()` périodique n'est ajouté ;
* aucun reset, watchdog, auto-test ou rejeu de note n'est déclenché sur SLEEP ;
* un SLEEP hors réveil explicite reste une anomalie signalée.

## 2. Tests hôte du réveil

Aucun test hôte ne compile actuellement `solenoid_driver.c`. Le Codeur doit
ajouter un harnais dédié (`tests/host/test_phase3_helpers.c` étendu) avec
des doublures GPIO/PWM pour le SDK Pico. `now_us` est déjà un paramètre de
toutes les fonctions concernées : l'horloge est injectable sans variable globale.
L'oracle de durée utilise un compteur 64 bits non replié ; les valeurs injectées
restent des `uint32_t`.

Cas minimaux :

| Id | Scénario | Attendu |
|---|---|---|
| W-01 | wake à t=0, tick avant 2 000 µs | `global_ready=false`, aucun SLEEP prématuré |
| W-02 | tick juste après 2 000 µs | `global_ready=true`, verrou posé |
| W-03 | ticks à 2³¹ µs, 2³² µs puis plusieurs wraps | reste prêt, verrou inchangé |
| W-04 | wake avec échéance qui déborde `UINT32_MAX` | prêt après le wrap, pas avant |
| W-05 | sleep puis wake | redevient faux pendant le nouveau délai, puis vrai |
| W-06 | douze canaux en FAULT avant l'échéance | le tick pose quand même le verrou |
| W-07 | note_on, probe_start, push_start, reset_fault avant/après échéance | SLEEP avant, succès après |
| W-08 | NFAULT asserté avant l'échéance puis après | non surveillé avant réveil, FAULT détecté après |
| W-09 | ancien code et mutant « comparaison signée sans verrou » | test en échec après 2³¹ µs ; seule la version verrouillée passe |

Le cas W-09 est verrouillé sur la version corrigée par le harnais hôte. La
démonstration de l'échec du mutant « comparaison signée sans verrou » reste une
vérification locale non committée, pas un mode de production.

## 3. Compteurs de refus du pilote

### 3.1 Identifiants ajoutés

Dans `shared/protocol/can_protocol.h`, conserver les IDs 0–12 et ajouter :

| Valeur | Énumération | Champ `can_link_stats_t` | Sens |
|---|---|---|---|
| 13 | `CAN_CARD_COUNTER_REJECT_SLEEP` | `solenoid_reject_sleep` | `SOLENOID_ERR_SLEEP` |
| 14 | `CAN_CARD_COUNTER_REJECT_REST` | `solenoid_reject_rest` | `SOLENOID_ERR_REST` |
| 15 | `CAN_CARD_COUNTER_REJECT_BUSY` | `solenoid_reject_busy` | `SOLENOID_ERR_BUSY` |
| 16 | `CAN_CARD_COUNTER_REJECT_FAULT` | `solenoid_reject_fault` | `SOLENOID_ERR_FAULT` |
| 17 | `CAN_CARD_COUNTER_REJECT_OTHER` | `solenoid_reject_other` | PARAM, PWM, FAULT_PERSISTS ou autre résultat non OK |

`CAN_CARD_COUNTER_COUNT` passe de 13 à **18**. Les IDs restent sur un octet ;
`0x403` et `0x425` conservent DLC 4/8 et tous leurs champs.

### 3.2 Ventilation exacte

Dans `firmware/moteur/src/can_link.c`, au point `:186-193` où
`solenoid_rejected` est déjà incrémenté :

1. si le résultat du pilote est `SOLENOID_OK` :
   `solenoid_commands_ok++`, aucun compteur de refus ;
2. sinon :
   `solenoid_rejected++` **et exactement un** compteur parmi 13–17 ;
3. le compteur de cause doit être incrémenté au même endroit, dans la même
   branche, pour garantir que la somme des IDs 13–17 égale le nombre de refus
   pilote des commandes CAN ;
4. ne pas ajouter d'événement `CARD_EVENT` supplémentaire : le limiteur 1/s et
   `COMMAND_IGNORED` restent ceux d'ADR-0020.

Le Clavier doit exposer les noms 13–17 dans `card_counter_name()`
(`firmware/clavier/src/can_link.c:2015-2030`) et dans l'outil
`tests/tools/moteur_swd_profile.ps1:107`. Libellés retenus pour `m stats` :

```text
solenoid_reject_sleep
solenoid_reject_rest
solenoid_reject_busy
solenoid_reject_fault
solenoid_reject_other
```

### 3.3 Lecture du vrai débordement RX

`card_counter_value()` (`firmware/moteur/src/can_link.c:512-529`) doit retourner
pour `CAN_CARD_COUNTER_RX_OVERRUN` la valeur de `isr_rx_overrun` (incrémentée dans
l'ISR à `:81`), et non `stats.rx_overrun` resté à zéro. La copie dans
`can_link_stats()` peut rester pour le diagnostic local, mais **une seule source
fait foi** pour la trame : le compteur ISR.

### 3.4 Cartes anciennes et mélange de firmwares

Une carte au firmware antérieur répond aux IDs 0–12 mais ignore 13–17. Le
balayage `m stats` doit :

* mémoriser, par position, si la carte a répondu à au moins un compteur
  historique ;
* afficher `n/a` (ou omettre les valeurs à zéro si la sortie agrégée ADR-0025 est
  en place) pour un nouvel ID sans réponse sur une carte qui répond aux anciens ;
* réserver `[STATUS] code=NO_RESPONSE pos=N` à une carte qui ne répond à **aucun**
  compteur du balayage ;
* ne jamais marquer la carte absente à cause des seuls IDs 13–17.

Le code actuel émet `NO_RESPONSE` dès le premier compteur muet
(`firmware/clavier/src/can_link.c:2063-2070`). Cette logique doit être corrigée :
un timeout sur les seuls IDs 13–17 d'une carte qui a répondu aux IDs 0–12 est
une indisponibilité, pas une absence.

Tant qu'ADR-0025 n'est pas implémentée, le format courant « une ligne par
compteur » est simplement étendu aux 18 IDs. Si les deux ADR sont livrées
ensemble, la ligne agrégée d'ADR-0025 remplace ces lignes ; le contrat de données
reste celui ci-dessus.

## 4. Journal et console

Aucune commande nouvelle, aucun changement de format public de trame.
`m stats` reste la seule lecture, à la demande, jamais périodique.

Après réveil, un refus SLEEP est anormal et doit être visible :

```text
[CARD] pos=N event=COMMAND_IGNORED ... raison=PILOTE
[CARD] pos=N stats ... solenoid_rejected=1 ... solenoid_reject_sleep=1
```

Le premier événement reste limité par ADR-0020 ; le compteur conserve toutes les
occurrences. Aucun message ne doit être ajouté par note.

## 5. Fichiers à revoir

| Fichier | Revue |
|---|---|
| `firmware/moteur/src/solenoid_driver.c` | **modifié** : verrou, wake/sleep/init, tick avant FAULT |
| `firmware/moteur/src/solenoid_wake.h` | **créé** : logique pure du verrou one-shot, testable sur hôte |
| `firmware/moteur/src/solenoid_driver.h` | **commentaire seulement**, aucune signature ne change |
| `firmware/moteur/src/can_link.c` | **modifié** : ventilation `:186-193`, `card_counter_value`, RX overrun réel |
| `firmware/moteur/src/can_link.h` | **modifié** : champs de statistiques 13–17 |
| `firmware/moteur/src/motor_counter_map.h` | **créé** : mapping pur refus→compteur et RX ISR, testé sur hôte |
| `shared/protocol/can_protocol.h` | **modifié** : IDs 13–17, `COUNT=18` |
| `shared/protocol/can_protocol.c` | **vérifié** : validation `counter_id < COUNT`, codecs inchangés |
| `firmware/clavier/src/can_link.c` | **modifié** : `card_counter_name`, gestion `n/a`, lecture des 18 IDs |
| `firmware/clavier/src/bringup_console.c` | **inchangé** sauf si l'aide énumère les compteurs |
| `tests/host/test_phase3_helpers.c` | **étendu** : mapping des refus, IDs 13–17, rejet de 18, décisions `ext=n/a`/muette |
| `tests/host/test_solenoid_wake.c` | **créé** : pilote réel W-01 à W-09 avec GPIO/PWM simulés |
| `tests/host/run_host_tests.ps1` | **modifié** : compilation du harnais driver |
| `tests/tools/moteur_swd_profile.ps1` | **modifié** : liste des noms de compteurs |
| `tests/phase3b_bout_en_bout.md` | **modifié** : `m stats` avec 18 IDs et `n/a` |
| `docs/specs/CAN.md` | **modifié** : table des compteurs |
| `docs/specs/impact-adr-0020.md` | **complété** : extension additive |
| `tests/procedure_adr0024_reveil_uptime.md` | **créé** : campagnes uptime et 24 h |

Inchangés : `firmware/moteur/src/main.c` (mêmes appels init/wake/tick),
`uid_position.h`, les tables de commissioning et le mapping.

## 6. Tests hôte des compteurs

| Id | Scénario | Attendu |
|---|---|---|
| C-01 | résultat OK | un compteur `commands_ok`, aucun refus |
| C-02 | SLEEP/REST/BUSY/FAULT | un seul compteur de cause et `rejected` |
| C-03 | PARAM/PWM/FAULT_PERSISTS | `reject_other`, `rejected` |
| C-04 | plusieurs refus successifs | somme des causes = `rejected` |
| C-05 | ID 17 dans `0x403`/`0x425` | accepté par les codecs |
| C-06 | ID 18 | rejeté, aucune trame émise |
| C-07 | `card_counter_value(RX_OVERRUN)` | égale `isr_rx_overrun`, pas `stats.rx_overrun` |
| C-08 | `card_counter_name` 0..17 | tous nommés, 18 hors bornes |
| C-09 | carte ancienne simulée : 0–12 répondent, 13–17 muets | `n/a`, pas `NO_RESPONSE` |
| C-10 | carte muette sur tout | `NO_RESPONSE` une fois |

## 7. Critères d'acceptation

* le verrou tient après 2³¹ µs, 2³² µs et plusieurs wraps ;
* il tient même si tous les canaux sont en FAULT ;
* `note_on`, `probe_start`, `push_start` et `reset_fault` ne renvoient plus
  SLEEP après le délai initial ;
* aucune limite de courant, durée, duty ou repos n'est modifiée ;
* les IDs 0–12 sont inchangés et `COUNT=18` ;
* la somme des cinq compteurs de refus égale `solenoid_rejected` pour les
  commandes CAN ;
* `RX_OVERRUN` reflète le compteur ISR ;
* une carte ancienne n'est pas déclarée muette à cause des IDs 13–17 ;
* toutes les campagnes de `tests/procedure_adr0024_reveil_uptime.md` passent,
  y compris la campagne 24 h distincte.

## 8. Questions ouvertes pour l'Architecte

Aucune. La ventilation exacte des résultats non listés dans
`CAN_CARD_COUNTER_REJECT_OTHER` est une application directe de l'ADR (« autre
résultat non OK ») et ne change ni trame ni seuil.
