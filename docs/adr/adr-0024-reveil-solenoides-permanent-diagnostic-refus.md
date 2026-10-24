---
title: "ADR-0024: Réveil permanent des solénoïdes après tWAKE et refus pilote comptés par cause"
status: "Accepted"
date: "2026-10-24"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "hardware", "protocol", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

> **Interface partagée modifiée — validation humaine requise.** La décision 3
> ajoute des identifiants de compteurs au contrat `0x403`/`0x425` d'ADR-0020.
> Aucun octet ni identifiant de trame ne change, les compteurs 0 à 12 conservent
> leur valeur et `CAN_PROTO_VERSION` reste 1. Aucune implémentation de cette
> extension avant acceptation.

Aucune limite de courant, de durée, de duty ou de repos n'est relâchée. Aucun
reset automatique, remapping ou masquage de divergence n'est introduit.

### Context

Le second problème est indépendant de l'acquittement du profil : après une longue
période sans jeu, une touche commissionnée part en MIDI et en CAN, mais aucun
solénoïde ne s'active alors que les cartes répondent encore aux UID et au profil.

Une cause logicielle certaine existe dans
`firmware/moteur/src/solenoid_driver.c:84-107` :

```text
g_wake_deadline_us = now_us + wake_delay_us
prêt = global_awake() && (uint32_t)(now_us - g_wake_deadline_us) < 0x80000000
```

Le réveil est armé une seule fois au boot (`firmware/moteur/src/main.c:81`) et
`solenoid_global_sleep()` n'a aucun appelant. Néanmoins, environ 2 000 µs +
2³¹ µs après le boot, soit **35 min 47,486 s**, la différence dépasse la moitié
du module 32 bits et `global_ready()` redevient faux. Il le reste pendant
2³¹ µs, puis alterne avec une période de 2³² µs ≈ 71 min 34,967 s.

Conséquences directes :

* `note_on`, `probe_start`, `push_start` et `reset_fault` retournent
  `SOLENOID_ERR_SLEEP` (`solenoid_driver.c:191,243,272,394`) ;
* le Clavier n'émet aucun `NOTE_DROPPED`, puisqu'il a bien transmis la commande ;
* le CAN, les UID et la synchronisation de profil continuent de fonctionner ;
* les auto-tests peuvent avorter et les tentatives de récupération sont
  intermittentes selon la phase du compteur ;
* la surveillance NFAULT est également court-circuitée par `global_ready()` dans
  `solenoid_driver_tick()` (`:304`), ce qui peut masquer des défauts pendant ces
  fenêtres.

C'est la cause racine **d'un mutisme périodique déterministe**, démontrée par le
code et testable sur hôte. Elle ne prouve pas à elle seule l'origine de chaque
incident rapporté : le symptôme dépend de l'uptime, non de l'inactivité. À
exactement 24 h après le boot, l'uptime modulo 2³² µs vaut environ 500,654 s,
dans une fenêtre où le code actuel est prêt. Le cas 24 h doit donc rester un
essai séparé et ne sera pas déclaré résolu par seule association.

Deux défauts d'observabilité empêchent actuellement de clore ce second volet :

* `CAN_CARD_COUNTER_RX_OVERRUN` lit `stats.rx_overrun`, jamais incrémenté, alors
  que l'ISR incrémente `isr_rx_overrun`
  (`firmware/moteur/src/can_link.c:81,516`) ;
* `solenoid_rejected` agrège tous les refus du pilote, sans distinguer SLEEP,
  REST, BUSY, FAULT ou une cause inattendue.

### Decision

#### 1. L'état « réveil terminé » est mémorisé définitivement

Le délai `wake_delay_us` est une échéance **one-shot**, pas une fenêtre de
validité périodique. Le pilote mémorise donc son franchissement :

* un booléen `g_wake_latched`, faux à l'initialisation ;
* `solenoid_global_wake()` efface le verrou, arme la nouvelle échéance puis lève
  nSLEEP ;
* tant que le verrou est faux, `global_ready()` évalue l'échéance avec la
  comparaison signée propre au wrap 32 bits et pose le verrou dès qu'elle est
  atteinte ;
* une fois le verrou posé, `global_ready()` dépend uniquement de
  `global_awake()` et ne recalcule plus l'ancienne échéance ;
* `solenoid_global_sleep()` et `solenoid_global_sleep_init()` effacent le verrou.

La première évaluation après l'échéance doit avoir lieu moins de 2³¹ µs après
celle-ci. Pour la garantir même si les douze canaux sont en FAULT,
`solenoid_driver_tick()` évalue le verrou **avant tout retour anticipé**, y
compris le retour propre à l'état FAULT. Le tick est appelé pour chaque canal à
chaque tour de boucle principale (`main.c:177`). Toutes ces fonctions
s'exécutent dans le contexte principal ; le callback CAN en IRQ ne touche pas au
pilote.

La valeur du profil `wake_delay_us`, les phases, les duty, les maxima et les
repos restent strictement inchangés. Aucune API publique du pilote ne change.

#### 2. Aucune récupération automatique n'est ajoutée

Le correctif ne relance pas une note, ne réarme pas périodiquement nSLEEP, ne
réinitialise pas une carte et ne lance pas d'auto-test. Un refus après le
correctif est compté et signalé avec sa cause ; il ne déclenche aucune action
correctrice silencieuse.

Un `SOLENOID_ERR_SLEEP` hors délai initial ou appel explicite à
`solenoid_global_sleep()` devient ainsi une anomalie réelle à investiguer, et non
un état périodique attendu.

#### 3. Les refus du pilote sont ventilés par compteurs additifs

Le compteur agrégé `CAN_CARD_COUNTER_REJECTED = 11` est conservé. Les compteurs
suivants sont ajoutés à `shared/protocol/can_protocol.h` :

| Valeur | Nom | Signification |
|---|---|---|
| 13 | `CAN_CARD_COUNTER_REJECT_SLEEP` | refus `SOLENOID_ERR_SLEEP` |
| 14 | `CAN_CARD_COUNTER_REJECT_REST` | refus `SOLENOID_ERR_REST` |
| 15 | `CAN_CARD_COUNTER_REJECT_BUSY` | refus `SOLENOID_ERR_BUSY` |
| 16 | `CAN_CARD_COUNTER_REJECT_FAULT` | refus `SOLENOID_ERR_FAULT` |
| 17 | `CAN_CARD_COUNTER_REJECT_OTHER` | autre résultat non OK, notamment PARAM/PWM |

`CAN_CARD_COUNTER_COUNT` devient 18. Chaque refus incrémente au plus un compteur
de cause et l'agrégat existant ; la somme des compteurs 13 à 17 correspond donc
aux refus pilote des commandes CAN ventilés par `REJECTED`.

Les trames `0x403` et `0x425`, leurs DLC et leurs champs sont inchangés. Un
Clavier récent ne présente pas l'absence de réponse aux seuls nouveaux IDs comme
une carte morte : si la carte répond aux compteurs historiques, les nouveaux
compteurs sont marqués indisponibles pour ce firmware. Aucun faux diagnostic de
présence n'est créé en mélange de versions.

Les interrogations restent exclusivement à la demande via `m stats`. Leur sortie
est agrégée par carte selon ADR-0025.

#### 4. Le débordement RX lu est celui réellement compté

`CAN_CARD_COUNTER_RX_OVERRUN` retourne le compteur alimenté par l'ISR
(`isr_rx_overrun`) et non le champ `stats.rx_overrun` resté à zéro. Cette
correction ne change pas le protocole.

Le bilan `m stats` distingue :

* commandes acceptées par le Clavier (`note_sent`, ON et OFF) ;
* commandes exécutées (`solenoid_commands_ok`) ;
* refus ciblés : auto-test, canal invalide, non occupé et pilote ;
* routage normal vers les autres cartes (`ignored_tag`), exclu des refus ciblés ;
* résidu non attribué, à corréler avec `can_errors` et le vrai `rx_overrun`.

Les compteurs RX couvrent toutes les trames : un résidu n'est pas présenté comme
une preuve qu'une note précise a été perdue sans mesure complémentaire.

#### 5. Le cas 24 h reste un critère de validation distinct

La correction du réveil est obligatoire, mais ne ferme pas automatiquement le
volet « après inactivité ». La validation comprend deux campagnes indépendantes :

1. **Campagne uptime** : une commande ON/OFF toutes les 10 s pendant au moins
   150 min, sans réarmer le réveil. Avant correction, le premier refus SLEEP doit
   apparaître à la première commande après 2 147,486 s (± 10 s d'échantillonnage).
   Après correction, aucun refus SLEEP sur au moins 900 notes et deux anciennes
   fenêtres de panne.
2. **Campagne inactivité** : 24 h sous tension sans trafic CAN applicatif ni
   auto-test, sans redémarrage de carte, puis jeu de toutes les touches
   commissionnées et relevé `m stats` avant/après. Aucun refus non expliqué et
   aucune note commissionnée muette ne sont acceptés après correction.

Si la campagne 24 h échoue encore, le compteur de cause, l'état du canal, le
bitmap, les défauts et les débordements déterminent la suite. Si les compteurs
impliquent le matériel, mesurer sans démontage, au moment d'une commande : le
24 V au driver, le niveau nSLEEP, NFAULT et la présence de l'impulsion de
courant. Une cause matérielle n'est affirmée qu'avec ces chiffres.

### Consequences

#### Positive
* **POS-001**: Le pilote ne retombe plus en SLEEP toutes les 71,6 minutes après un réveil pourtant définitif.
* **POS-002**: Les notes, sondes, poussées et resets de défaut restent disponibles sur toute la durée de fonctionnement, sans modifier les limites.
* **POS-003**: NFAULT et `reset_fault` redeviennent actifs dans les anciennes fenêtres aveugles ; un défaut auparavant masqué devient visible.
* **POS-004**: Le prochain incident de note muette produit une cause comptable au lieu du seul agrégat `DRIVER`.
* **POS-005**: Le compteur RX cesse de rapporter faussement zéro débordement.
* **POS-006**: Le test 24 h reste explicitement séparé : la première correction ne sert pas à déclarer prématurément le second problème clos.

#### Negative
* **NEG-001**: Le verrou suppose la vivacité de la boucle pour la première évaluation ; une boucle arrêtée pendant plus de 2³¹ µs invaliderait cette hypothèse.
* **NEG-002**: Cinq compteurs partagés supplémentaires allongent un balayage `m stats`, uniquement à la demande et avec sortie agrégée.
* **NEG-003**: Des défauts NFAULT auparavant non détectés peuvent apparaître après correction et exiger une investigation matérielle réelle.
* **NEG-004**: Une campagne de 150 min et une campagne de 24 h sont nécessaires ; les tests hôte seuls ne prouvent pas le comportement mécanique ou électrique.
* **NEG-005**: Les anciennes cartes ne fournissent pas les nouveaux compteurs ; le mélange de versions exige un affichage « indisponible » correct.

### Alternatives Considered

#### Remplacer le test par une comparaison signée seulement
* **ALT-001**: **Description**: utiliser `(int32_t)(now_us - deadline) >= 0` sans verrou.
* **ALT-002**: **Rejection Reason**: strictement équivalent au test actuel (`d < 0x80000000`) ; le SLEEP revient après 2³¹ µs. Le test hôte doit inclure ce mutant pour empêcher ce faux correctif.

#### Utiliser une échéance 64 bits
* **ALT-003**: **Description**: stocker `g_wake_deadline_us` et lire `time_us_64()` pour chaque évaluation.
* **ALT-004**: **Rejection Reason**: solution valide, mais elle ajoute une source d'horloge et des accès 64 bits à chaque appel alors que le verrou one-shot est plus petit. Elle devient préférable si la vivacité de la boucle n'est plus garantie.

#### Réarmer périodiquement le réveil
* **ALT-005**: **Description**: appeler `solenoid_global_wake()` périodiquement ou à chaque note.
* **ALT-006**: **Rejection Reason**: ajoute un délai de réveil à des notes valides, masque un éventuel nSLEEP bas et ne corrige pas le modèle one-shot.

#### Reset ou auto-test automatique sur refus SLEEP
* **ALT-007**: **Description**: redémarrer la carte ou relancer une séquence RUN/auto-test dès un refus.
* **ALT-008**: **Rejection Reason**: masque la cause, interrompt le jeu et enfreint l'exigence de ne pas récupérer silencieusement.

#### Déclarer le problème 24 h résolu par le seul correctif temporel
* **ALT-009**: **Description**: utiliser la présence du défaut `global_ready()` comme explication unique de tous les incidents.
* **ALT-010**: **Rejection Reason**: le calcul à 24 h et l'absence de lien avec l'inactivité ne le permettent pas ; la campagne indépendante est obligatoire.

### Implementation Notes

* **IMP-001**: Modifier uniquement la gestion globale du réveil dans `firmware/moteur/src/solenoid_driver.c`. Ne changer aucune valeur du profil ni aucune machine d'état de phase.
* **IMP-002**: Effacer le verrou sur wake, sleep et initialisation ; le poser uniquement après franchissement réel de l'échéance, jamais dans `solenoid_global_wake()` lui-même. Évaluer ce verrou en tête de `solenoid_driver_tick()`, avant le retour FAULT et avant toute autre sortie anticipée.
* **IMP-003**: Ajouter un test hôte du pilote avec horloge injectée : avant/après 2 ms, à 2³¹ µs ± marge, après 2³² µs, plusieurs wraps, deadline elle-même en wrap, puis sleep→wake. Ajouter le cas où tous les canaux sont en FAULT avant l'échéance : le tick doit malgré tout verrouiller le réveil. L'oracle utilise un temps 64 bits non replié.
* **IMP-004**: Démontrer que le test échoue sur le code actuel et sur le mutant « comparaison signée seule », puis réussit avec le verrou. Couvrir note, probe, push et reset selon la couverture existante.
* **IMP-005**: Ventiler les résultats dans `handle_solenoid_command` au point où `solenoid_rejected` est déjà incrémenté. Aucun double comptage et aucun événement supplémentaire par note.
* **IMP-006**: Mettre à jour `shared/protocol/can_protocol.[ch]`, les codecs/tests de `0x403`/`0x425`, la table `card_counter_value`, `card_counter_name()` et `m stats`. Les IDs 0 à 12 restent immuables. Une carte qui répond aux compteurs historiques mais pas à un nouvel ID obtient `n/a` dans la ligne agrégée, sans `NO_RESPONSE` pour ce compteur ; `NO_RESPONSE` reste réservé à une carte muette sur tout le balayage.
* **IMP-007**: Faire lire `CAN_CARD_COUNTER_RX_OVERRUN` au compteur ISR réel et ajouter un test qui échoue si les deux sources divergent à nouveau.
* **IMP-008**: Banc : journaliser uptime estimé depuis la dernière annonce de boot de chaque carte, notes exécutées/refusées et compteurs. Conserver les logs de build dans des fichiers et n'en extraire que les erreurs et le résultat.
* **IMP-009**: Documentaliste après acceptation : `docs/specs/CAN.md`, `docs/specs/impact-adr-0020.md`, procédure « notes muettes », `tests/build_clavier_windows.md` et les deux campagnes de validation.
* **IMP-010**: Ne pas ajouter de watchdog, de reset automatique ou de nouvelle commande de note dans ce chantier. Toute autre cause révélée par les nouveaux compteurs fait l'objet d'une décision distincte.

### References
* **REF-001**: [ADR-0001 — Contrat HAL DRV8874](./adr-0001-hal-solenoide-drv8874-phen.md)
* **REF-002**: [ADR-0007 — Limite HOLD](./adr-0007-hold-limite-20s.md)
* **REF-003**: [ADR-0014 — Limites pilotées par le profil](./adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md)
* **REF-004**: [ADR-0020 — Compteurs et commandes ignorées](./adr-0020-comptabilite-echecs-cartes-moteur.md)
* **REF-005**: [ADR-0021 — Conservation du bitmap](./adr-0021-conservation-bitmap-mesure-non-concluante.md)
* **REF-006**: [ADR-0025 — Journal et statistiques agrégées](./adr-0025-journal-diagnostic-niveaux-runtime-agregation.md)
* **REF-007**: `firmware/moteur/src/solenoid_driver.c:80-107,180-200,234-280,297-306,385-399`
* **REF-008**: `firmware/moteur/src/main.c:66-86,177` — initialisation, unique wake et tick principal
* **REF-009**: `firmware/moteur/src/can_link.c:76-93,157-193,514-550` — ISR, refus et compteurs
* **REF-010**: `shared/protocol/can_protocol.h:111-140` — contrat actuel des compteurs
