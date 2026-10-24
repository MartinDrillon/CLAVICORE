---
title: "ADR-0025: Journal CDC par niveaux d'exécution, verdicts conservés et statistiques agrégées"
status: "Accepted"
date: "2026-10-24"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "diagnostic", "protocol"]
supersedes: "0022 (décisions 2 et 3)"
superseded_by: ""
---

### Status
**Accepted**

Reprend les décisions 2 et 3 d'ADR-0022 et les intègre à une politique complète
de journal. Amende ADR-0004 et ADR-0009 pour les traces de notes/mapping,
ADR-0005, ADR-0017, ADR-0018, ainsi qu'ADR-0020 §4 et pour la présentation des
compteurs.

Le renommage de l'événement CAN 0 est couvert par cette ADR avant modification de
son symbole dans `shared/protocol/`.

> **Interface source partagée modifiée — validation humaine requise.** Seul le nom
> du symbole change : **valeur numérique, trame, DLC et octets inchangés**,
> `CAN_PROTO_VERSION = 1`. Aucune implémentation avant acceptation.

Aucune dérogation aux invariants : les divergences et défauts restent signalés à
tous les niveaux d'exécution.

### Context

Le journal mélange actuellement des verdicts indispensables et du trafic normal :

* chaque note diffusée produit potentiellement N−1 événements
  `COMMAND_IGNORED raison=TAG`, alors que les autres cartes font simplement leur
  routage normal ;
* `m stats` produit treize lignes par carte, bientôt davantage avec ADR-0024 ;
* `[SELFTEST_RAW]` produit plusieurs lignes par canal et par carte ;
* les lignes de matrice, `[MAP]`, `[PUSH]`, `[NOTE]`, les événements de bouton et
  les répétitions de début/fin de synchronisation noient les états réellement
  fautifs ;
* `BUTTON_SHORT_PRESS` est émis au front montant du test manuel, y compris au
  début d'un appui long : son nom ne décrit pas l'action réelle ;
* l'horodatage et plusieurs familles de traces ne sont pas désactivables à
  l'exécution, et la commande `c` couple l'acquisition de calibration à
  l'affichage des RAW.

Le besoin n'est pas un nouveau vocabulaire de diagnostic. C'est un journal
**silencieux quand le système est sain**, bref sur une opération normale et
explicite sur une anomalie. Les réponses à une commande console explicite restent
évidemment affichées.

### Decision

#### 1. Trois niveaux runtime, une seule commande

Une commande console `l 0|1|2` sélectionne le niveau, sans recompilation. `l`
sans argument affiche le niveau et l'état de l'horodatage. `l t` active ou coupe
le préfixe `[T+… ms]` sans changer le contenu des lignes.

| Niveau | Usage | Contenu |
|---|---|---|
| `l 0` | fonctionnement normal | verdicts d'opérations demandées, défauts, divergences, refus et réponses console |
| `l 1` | bring-up | niveau 0 + boot, UID, table, résumés par carte, événements normaux et détail des auto-tests |
| `l 2` | trace | niveau 1 + notes, RAW, matrice, PUSH courants et traces de trafic conservées |

Les réponses à `d`, `m stats`, `p get/show/diff/fields` et aux autres commandes de
lecture sont toujours rendues, même à `l 0` : le niveau filtre les émissions
spontanées, pas la réponse à une question de l'opérateur.

La commande `n` est retirée au profit de `l 2`/`l 0`, afin de ne pas conserver
deux commandes pour la même famille. `c` est conservé : il commande
l'**acquisition** de calibration et non le niveau de journal.

Le niveau par défaut est fixé par `CLAVIER_DIAG_DEFAULT_LEVEL` (1 pendant le
bring-up, 0 pour un build final). Le choix runtime n'est pas persisté en flash :
aucun nouveau format persistant n'est introduit. Le préfixe temporel reste actif
par défaut pendant le bring-up et peut être coupé par `l t`.

#### 2. Les anomalies ne sont jamais masquables à l'exécution

À tous les niveaux, y compris `l 0`, restent émis :

* toute divergence d'UID, de tag, de table ou de commissioning, notamment les
  lignes `[TABLE] diff` avec changement d'UID, les écarts `carte/local` de profil
  et les acquittements inattendus ou inattribuables ; toute table invalide et
  tout bitmap conservé après mesure non concluante ;
* `PROFILE_SYNC_FAILED`, `PROFILE_DIVERGENT`, `PROFILE_FALLBACK_IN_USE`, refus de
  profil et réconciliations d'ADR-0023 ;
* `CARD_REBOOTED`, `TRIGGER_BLOCKED`, `CHANNEL_FAULT`, `SELFTEST_ABORTED` et le
  premier refus de chaque motif pour une commande adressée à la carte dans la
  fenêtre courante d'une seconde ; les répétitions restent comptées lorsque
  l'instrumentation est compilée ;
* `MANUAL_TEST_STARTED`, car il signale l'alimentation physique d'un solénoïde,
  et non une simple trace de bouton ;
* `NOTE_DROPPED` et les refus de maintenance ;
* toute ligne `[SELFTEST_OHM]` portant `warn=i100>3.0A`, même si les résumés OHM
  ordinaires sont filtrés ;
* tout canal dont une mesure OHM demandée n'a pas été reçue, agrégé dans le
  verdict `[SELFTEST]` par `ohm_manquants=0xNNN` plutôt que dans des RAW ;
* le résumé final d'une opération explicitement demandée, avec `calib=OFF` dans
  `[SUMMARY]` si la calibration n'a pas été demandée.

Un niveau de journal ne change jamais une décision fonctionnelle, un compteur,
une acquisition de sécurité ou une coupure du pilote. Les événements de santé et
d'action physique de cette liste ne sont pas des traces optionnelles.

#### 3. Le bruit de routage disparaît à la source

Une `SOLENOID_COMMAND` dont l'`uid_tag` n'est pas celui de la carte est le
routage normal en diffusion. Elle incrémente toujours `solenoid_ignored_tag`,
mais ne produit **aucun** `CARD_EVENT`.

Les autres refus d'une commande adressée à la carte — auto-test, canal invalide,
canal non occupé et pilote — conservent le limiteur existant d'ADR-0020 : le
premier refus de chaque motif est émis au plus une fois par seconde. Dans les
builds incluant les compteurs, chaque occurrence supplémentaire est comptée sans
nouvelle trame et son cumul est lisible dans `m stats`. Dans le build allégé, le
premier événement périodique reste émis mais la lecture cumulative est absente,
conséquence explicitement acceptée de la désactivation des compteurs. Cette
décision reprend ADR-0022 décision 2 et supprime N−1 fausses alertes par note à
N cartes.

#### 4. Les statistiques sont agrégées par carte

`m stats` mémorise les valeurs reçues pendant le balayage et produit :

* une ligne `[CARD] pos=N stats …` par carte, regroupant les compteurs au lieu
  d'une ligne par compteur ;
* une seule ligne de bilan `[STATS]` pour la corrélation notes/refus d'ADR-0024 ;
* une ligne d'absence seulement si la carte ne répond à aucun compteur. Des
  compteurs récents absents sur une carte qui répond aux historiques sont marqués
  indisponibles, pas présentés comme une carte morte.

Les valeurs nulles peuvent être omises de la ligne, sauf les compteurs nécessaires
au bilan et les compteurs d'erreur explicitement demandés. Aucun nouvel
identifiant de ligne tel que `[CARDSTATS]` n'est créé.

Les requêtes restent à la demande, jamais périodiques. Aucune trame CAN ne change.

#### 5. Les RAW sont découplés de leur acquisition

Le mode `c` continue de demander les trames de calibration et les calculs OHM.
L'impression des lignes `[SELFTEST_RAW]` `idle/probe` et `delta/ohm_est` n'a lieu
qu'à `l 2`.

À `l 0` et `l 1`, les données reçues restent exploitables pour les verdicts,
l'estimation ohmique et les avertissements :

* à `l 1`, les résumés `[SELFTEST_OHM]` ordinaires sont affichés ; à `l 0`, seuls
  ceux portant un avertissement de courant sont émis ;
* tout OHM demandé mais absent est agrégé dans `[SELFTEST]` **par position** avec
  `ohm_manquants=0xNNN` à tous les niveaux ; aucune absence ne dépend d'une ligne
  RAW filtrée ;
* si `c` est OFF, le résumé final `[SUMMARY]` porte explicitement `calib=OFF`,
  sans transformer en erreur les mesures qui n'ont pas été demandées.

Couper l'affichage RAW ne coupe donc ni les calculs, ni le warning de courant, ni
le signalement d'une mesure demandée mais manquante.

#### 6. Les lignes ordinaires sont supprimées, regroupées ou reléguées

* `p send` n'imprime plus une ligne d'annonce, une ligne de début, un échec de
  synchronisation puis un second échec « réglage ». Le verdict unique d'ADR-0023
  porte `but=reglage`, la durée, les tentatives et le résultat ; les détails par
  carte ne sont automatiques qu'en échec ou à `l 1`.
* Les lignes `[MAP]` ne sont plus imprimées automatiquement à chaque boot ou
  écriture ; elles sont rendues par `d` ou à `l 1` selon le besoin de bring-up.
* Les lignes libres de matrice `position … behavior/unused/note …` passent par le
  point de diagnostic central et n'existent qu'à `l 2`, avec un format tagué
  unique ; aucun `printf` parallèle ne subsiste.
* `[NOTE]` et les lignes par note n'existent qu'à `l 2` ; `[NOTE_DROPPED]` reste
  toujours visible.
* `[PUSH] start/stop` passe à `l 2` ; un timeout ou un refus reste un verdict
  visible à `l 0`.
* Les événements `TRIGGER_PENDING`, `TRIGGER_SENT` et
  `CHANNEL_FAULT_CLEARED` sont limités à `l 1`/`l 2`. `MANUAL_TEST_STARTED` et
  les événements de défaut listés à la décision 2 restent visibles à `l 0`.
* Les lignes `[UID]` de scan et le détail `[BOOT]` sont relégués à `l 1`, mais un
  reboot inattendu reste visible à `l 0`.

#### 7. L'événement 0 décrit le test manuel réellement démarré

L'événement CAN 0 est renommé `MANUAL_TEST_STARTED` dans le symbole partagé, le
journal et la documentation. Sa valeur reste 0, son instant d'émission reste le
front montant qui démarre réellement le test manuel et son payload ne change pas.

Un appui long peut donc produire `MANUAL_TEST_STARTED`, puis `TRIGGER_PENDING`,
puis `TRIGGER_SENT` : c'est la séquence réelle, sans nom contradictoire.

#### 8. Le build final peut retirer les traces, sans retirer les verdicts

Le runtime règle l'affichage mais ne peut pas empêcher une carte Moteur d'émettre
un événement. Deux options de compilation complètent donc les niveaux :

* `CLAVIER_DIAG_TRACE` inclut le niveau 2 et les traces de matrice/notes/RAW ;
  `OFF` retire ce code du build final ;
* `MOTEUR_DIAG_TRACE` inclut les événements ordinaires `TRIGGER_PENDING`,
  `TRIGGER_SENT` et `CHANNEL_FAULT_CLEARED`, ainsi que les compteurs et le
  répondeur `0x403`/`0x425` ; `OFF` retire ces outils d'un build Moteur final
  validé.

Les événements de santé et d'action physique de la décision 2 —
`MANUAL_TEST_STARTED`, `TRIGGER_BLOCKED`, `CHANNEL_FAULT`, `SELFTEST_ABORTED` et
la première occurrence des refus de commandes adressées selon le limiteur —
restent émis dans tous les builds. Ils sont le compte rendu fonctionnel d'un
système sans console Moteur, pas des traces optionnelles. Leur chemin d'encodage
`0x424` est donc conservé même avec `MOTEUR_DIAG_TRACE=OFF`. Les trames UID,
profil, auto-test et commandes de solénoïdes ne sont jamais désactivées non plus,
et les divergences calculées par le Clavier restent signalées.

La désactivation des traces est donc complète pour les outils optionnels, sans
option capable de masquer une anomalie. Elle reste une décision de build explicite
pour un système déjà validé, pas un moyen de masquer une panne pendant le
bring-up.

Les options CMake existantes `CLAVIER_SELFTEST_CALIBRATION` et
`CLAVIER_CDC_CONSOLE` restent. Les macros `#ifndef` `CLAVIER_MAP_LOG` et
`CLAVIER_NOTE_LOG` de `can_link.c` sont remplacées par le niveau par défaut et
`CLAVIER_DIAG_TRACE`, afin de ne pas multiplier les commutateurs recouvrants.

### Consequences

#### Positive
* **POS-001**: En fonctionnement sain, `l 0` ne produit aucune ligne de routage, de note ou de compteur ; les anomalies restent visibles.
* **POS-002**: La suppression des événements de tag étranger retire N−1 lignes parasites par note sans perdre le compteur de routage.
* **POS-003**: `m stats` passe de treize lignes ou plus par carte à une ligne par carte plus un bilan.
* **POS-004**: Les RAW peuvent être analysés à la demande sans que leur impression masque les verdicts de l'auto-test.
* **POS-005**: Les diagnostics d'affichage hors verdicts de la décision 2 sont désactivables à l'exécution ; les traces Moteur et Clavier sont retirables à la compilation.
* **POS-006**: Le vocabulaire est réduit : pas de nouveau tag de statistiques, commandes `n`/macros recouvrantes supprimées, doublons de synchronisation fusionnés.

#### Negative
* **NEG-001**: Le niveau runtime n'est pas persistant ; le build final doit fixer son niveau par défaut ou l'opérateur doit rejouer `l 0` après reboot.
* **NEG-002**: `MOTEUR_DIAG_TRACE=OFF` retire une partie de l'observabilité de cartes sans console ; les événements de santé/action restent, mais l'option ne doit être utilisée qu'après validation.
* **NEG-003**: Le renommage du symbole partagé impose de mettre à jour les deux firmwares, les tests et la documentation, malgré l'absence de changement binaire CAN.
* **NEG-004**: Le filtrage central et l'agrégation des compteurs modifient de nombreux formats de test et procédures de banc.
* **NEG-005**: Un mélange d'anciennes et nouvelles cartes peut continuer d'émettre des traces que le nouveau Clavier filtre ; le trafic de diagnostic ordinaire n'est silencieux qu'avec les cartes recompilées.

### Alternatives Considered

#### Un commutateur « mute » global
* **ALT-001**: **Description**: couper toutes les lignes, y compris erreurs et divergences.
* **ALT-002**: **Rejection Reason**: masque exactement les états que l'invariant 1 exige de signaler ; `l 0` conserve toujours les anomalies.

#### Filtrer seulement côté Clavier
* **ALT-003**: **Description**: laisser toutes les cartes émettre et ne rien changer à leur build.
* **ALT-004**: **Rejection Reason**: suffit pour le confort runtime, mais pas pour un système final léger sur le bus ; l'option Moteur de compilation est nécessaire.

#### Conserver une ligne par compteur
* **ALT-005**: **Description**: ajouter les compteurs d'ADR-0024 dans la sortie actuelle.
* **ALT-006**: **Rejection Reason**: aggrave le bruit précisément au moment où davantage de causes deviennent mesurables ; l'agrégation ne change aucune donnée transportée.

#### Persister le niveau de journal en flash
* **ALT-007**: **Description**: retrouver le dernier niveau choisi après chaque coupure.
* **ALT-008**: **Rejection Reason**: nouveau format persistant, usure et état caché pour un besoin couvert par une valeur de build et une commande runtime.

#### Supprimer immédiatement tous les diagnostics
* **ALT-009**: **Description**: retirer événements, compteurs et RAW dès maintenant.
* **ALT-010**: **Rejection Reason**: la cause physique des acquittements perdus et le cas 24 h ne sont pas tous mesurés ; la désactivation doit rester disponible après validation, pas précéder le diagnostic.

### Implementation Notes

* **IMP-001**: Centraliser niveau, horodatage et émission dans `diag_out`. Tous les `printf` de diagnostic Clavier, y compris la matrice, passent par ce point ; les formateurs purs de `diag_log.c` restent testables sur hôte.
* **IMP-002**: Les réponses console explicites contournent le filtre de niveau, jamais les options de compilation. Les anomalies de la décision 2 utilisent une catégorie non masquable distincte du niveau 0.
* **IMP-003**: Implémenter `l`, `l 0|1|2` et `l t` dans la console ; retirer `n`, conserver `c` pour l'acquisition. Mettre l'aide à jour sans ajouter d'autre commande de journal.
* **IMP-004**: Côté Moteur `handle_solenoid_command`, un tag étranger incrémente le compteur puis retourne sans `report_ignored_command`. Les autres refus sont inchangés.
* **IMP-005**: Stocker les compteurs du balayage par position et `counter_id`, n'imprimer qu'à la fin du balayage et conserver la corrélation `uid_tag + counter_id`. Tester les réponses hors balayage explicitement.
* **IMP-006**: Renommer l'événement 0 dans `shared/protocol/can_protocol.h`, tous les émetteurs/récepteurs et les tests. Ne changer ni sa valeur ni le codec ; `CAN_PROTO_VERSION` reste 1.
* **IMP-007**: Ajouter les options CMake avec valeurs de bring-up actives par défaut. Vérifier que `MOTEUR_DIAG_TRACE=OFF` conserve tous les événements de santé/action de la décision 2 et toute trame fonctionnelle, et que `CLAVIER_DIAG_TRACE=OFF` ne retire aucun verdict.
* **IMP-008**: Tests hôte : chaque famille à chaque niveau ; anomalies toujours émises ; horodatage commutable ; réponses console à `l 0` ; RAW cachés sans perte des calculs ; warning de courant et `ohm_manquants` à `l 0` ; `calib=OFF` ajouté au formateur `[SUMMARY]` ; aucune ligne TAG étranger ; premier refus adressé émis et répétition dans la même seconde comptée sans nouvelle ligne ; libellé `MANUAL_TEST_STARTED` ; agrégation de tous les compteurs par carte ; événements de santé conservés avec `MOTEUR_DIAG_TRACE=OFF`.
* **IMP-009**: Banc avant/après sur le même scénario : deux cartes, un `p send`, un auto-test, `m stats`, puis jeu de toutes les touches. Critères mesurables : une note produit au moins une ligne TAG parasite avant et zéro après ; `m stats` produit 26 lignes de compteurs plus le bilan avant, contre deux lignes agrégées plus le bilan après ; un `p send` nominal aboutit à un verdict unique ; aucun RAW n'apparaît à `l 0`/`l 1`. À `l 0`, une session saine sans commande de lecture ne produit aucune ligne périodique et toutes les alertes injectées restent visibles.
* **IMP-010**: Documentaliste après acceptation : `docs/specs/CAN.md`, `docs/specs/impact-adr-0018.md`, `docs/specs/impact-adr-0020.md`, `tests/build_clavier_windows.md` §5 et toutes les procédures qui citent `n`, `BUTTON_SHORT_PRESS` ou les anciennes lignes.
* **IMP-011**: Conserver les logs de build et de campagne dans des fichiers. Ne pas augmenter le nombre de lignes normales pour introduire les niveaux : les exemples de documentation doivent montrer le journal `l 0` le plus court.

### References
* **REF-001**: [ADR-0004 — Auto-test et commandes de solénoïdes](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md)
* **REF-002**: [ADR-0005 — Journal série CDC](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md)
* **REF-003**: [ADR-0009 — Commissioning et traces de mapping](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)
* **REF-004**: [ADR-0017 — Horodatage et redémarrages](./adr-0017-journal-diagnostic-auto-suffisant-horodatage-identite-redemarrages.md)
* **REF-005**: [ADR-0018 — Événements Moteur](./adr-0018-retour-etat-bouton-canaux-moteur-vers-clavier.md)
* **REF-006**: [ADR-0020 — Compteurs et commandes ignorées](./adr-0020-comptabilite-echecs-cartes-moteur.md)
* **REF-007**: [ADR-0022 — Décisions 2 et 3 reprises](./adr-0022-precisions-banc-acquittement-tag-appui-court.md)
* **REF-008**: [ADR-0023 — Verdicts de synchronisation](./adr-0023-synchronisation-profil-reprise-verification-etat.md)
* **REF-009**: [ADR-0024 — Nouveaux compteurs de refus](./adr-0024-reveil-solenoides-permanent-diagnostic-refus.md)
* **REF-010**: `firmware/clavier/src/can_link.c` — émissions PROFILE, NOTE, MAP, SELFTEST_RAW et `m stats`
* **REF-011**: `firmware/clavier/src/diag_out.[ch]`, `diag_log.[ch]`, `bringup_console.c` — point d'émission et commandes
* **REF-012**: `firmware/moteur/src/can_link.c:127-193,514-550` — refus et compteurs Moteur
