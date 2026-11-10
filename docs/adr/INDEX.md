# Index des ADR

Table de recherche rapide : lis cet index **avant** d'ouvrir une ADR, et n'ouvre
que celles dont le titre, les tags ou le résumé concernent ta tâche. Une ligne
par ADR, triées par numéro.

**Dernière vérification complète : 2026-10-25**

> Tant que cette date est « jamais », l'index peut être incomplet : liste aussi
> `docs/adr/` (noms de fichiers) avant de conclure qu'une ADR n'existe pas.

## Lecture rapide — règles en vigueur

Ne pas ouvrir les 26 ADR pour une tâche courante. Pour l'état consolidé des
règles actives, de la stack et des interfaces : **`./ARCHITECTURE_CURRENT.md`**
(une page). L'index ci-dessous sert seulement à retrouver l'ADR d'origine.

| Sujet | ADR à lire |
|---|---|
| HAL solénoïde, PH/EN, IPROPI, polarités | 0001 |
| CAN2040 (vendoring, PIO/IRQ, callback) | 0002 |
| Auto-test IPROPI, table de commissioning, `SOLENOID_COMMAND` | 0004, 0009, 0010, 0021, 0028 |
| Diagnostic texte sur CDC (PuTTY) | 0005, 0017, 0025 |
| Noms d'octaves / notes | 0008 |
| Mode MAINTENANCE | 0012 |
| Keycodes, transposition, altérations, armure, MIDI clavier | 0008, 0011, 0026, 0029, 0030 |
| Combos de positions (N touches → 1 keycode) | 0031 |
| Profil d'activation des solénoïdes (format, transfert, persistance) | 0013, 0014, 0019, 0023 |
| Réglage à chaud par console | 0015 |
| Synchronisation du profil, attribution UID, échecs | 0016, 0020, 0023 |
| Trames Moteur → Clavier (`CARD_EVENT`) | 0018 |
| Compteurs et refus du pilote | 0020, 0024 |

## Tenue de l'index

- **Architecte** : ajoute la ligne à la création de l'ADR (statut `Proposed`).
- **doc-aval** : met à jour le statut quand l'ADR est acceptée, ou
  reconstruit/contrôle l'index.
- En cas de divergence, le frontmatter de l'ADR fait foi.
- Colonne « Relations » : `remplace 0003`, `remplacée par 0009`, ou `—`.
- Colonne « Résumé » : 20 mots maximum, avec les termes de l'ADR. Si l'ADR
  déroge à un invariant de `AGENTS.md`, le dire dans le résumé.
- Toute ADR qui n'est plus la règle en vigueur descend dans la section
  **Historique** ci-dessous : elle reste consultable, mais ne doit plus être
  ouverte par défaut.

## Décisions actives

| ADR | Titre | Statut | Tags | Relations | Résumé |
|---|---|---|---|---|---|
| [0001](./adr-0001-hal-solenoide-drv8874-phen.md) | Contrat HAL de commande solénoïde DRV8874 (mode PH/EN) | Proposed | architecture, firmware, hardware | — | Contrat HAL solénoïde DRV8874 en mode PH/EN ; IPROPI via mux 74HC4067 (IC17) ; inversion de polarité pour RELEASE. |
| [0002](./adr-0002-integration-can2040.md) | Intégration de can2040 — vendoring, PIO/IRQ, GPIO, contrat callback | Proposed | architecture, firmware, protocol | — | Intégration can2040 (CAN par PIO) : vendoring, PIO/IRQ, GPIO, callback. Dérogation : licence GPLv3 à valider. |
| [0004](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) | Auto-test IPROPI, table d'assignation flash et SOLENOID_COMMAND | Accepted | architecture, firmware, protocol, hardware | Amendée par 0005, 0009, 0010, 0025 | Auto-test IPROPI (PROBE 20 %, un canal à la fois), table d'assignation flash, SOLENOID_COMMAND. Dérogation : invariant 1 reformulé. |
| [0005](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) | Affichage diagnostic sur journal série CDC, en remplacement du SysEx | Accepted | architecture, firmware, usb, diagnostic | Remplace partiellement 0003 (§6) et 0004 (addendum SysEx) ; amendée par 0025 | Diagnostic sur port série CDC (PuTTY) remplaçant tout SysEx de diagnostic ; trames CAN inchangées. |
| [0008](./adr-0008-convention-nommage-octaves-notation-scientifique.md) | Convention de nommage des octaves — notation scientifique (MIDI 36 = DO2, 84 = DO6) | Accepted | architecture, firmware, protocol, diagnostic | Amende partiellement 0004 (§6), 0005 (nommage) | Notation scientifique C4=60 : MIDI 36 = DO2, 84 = DO6 ; corrige docs et journal CDC, sans changer trames ni table. |
| [0009](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md) | Table de commissioning « provisoire » (bring-up) et estimation de résistance des bobines | Accepted | architecture, firmware, protocol, diagnostic | Amende 0004 ; amendée par 0010, 0021, 0025 | Table persistée à chaque auto-test demandé, état FINAL/PROVISIONAL signalé ; PROBE 5 ms, trame 0x601 ohm_est/i100. |
| [0010](./adr-0010-ancrage-grave-mapping-commissioning.md) | Ancrage du mapping de commissioning sur la note la plus grave — position 0, canal 0 = DO2 (MIDI 36) | Accepted | architecture, firmware, protocol | Remplace partiellement 0004 ; amende 0009 ; amendée partiellement par 0028 (§1, §5) | Assignation ascendante depuis la touche 1 (DO2) ; position 0, canal occupé le plus haut = DO2 (sens intra-carte : 0028) ; motif 0x40 ANCHOR_SHIFT. |
| [0011](./adr-0011-untranspose-remise-a-zero-etat-transposition.md) | Sémantique du keycode UNTRANSPOSE — remise à zéro totale de l'état de transposition et d'altération | Accepted | architecture, firmware | — | Remise à zéro absolue de l'état de transposition et d'altération par le keycode UNTRANSPOSE (position 73) ; sans changement d'interface partagée. |
| [0012](./adr-0012-mode-maintenance-force-inverse-duree-20s.md) | Mode MAINTENANCE — poussée en polarité inverse (REPEL) tenue jusqu'au relâchement, bornée à 20 s | Accepted | architecture, firmware, hardware | Remplace partiellement 0006 (§4) | MAINTENANCE pousse en polarité inverse jusqu'au relâchement, 20 s à budget thermique constant. Dérogation : invariant 4. |
| [0013](./adr-0013-profil-activation-solenoides-centralise-clavier.md) | Profil d'activation des solénoïdes centralisé sur le Clavier, transféré à l'auto-test IPROPI et persisté côté Moteur | Accepted | architecture, firmware, protocol, hardware | Amendée par 0014, 0023 | Profil unique côté Clavier, transféré en 0x420–0x423, validé structurellement et persisté en flash A/B. |
| [0014](./adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md) | Limites d'activation des solénoïdes entièrement pilotées par le profil Clavier — dérogation à l'invariant 4 | Accepted | architecture, firmware, hardware, protocol | Amende 0013 ; amende 0007 et 0012 ; déroge à l'invariant 4 ; amendée 2026-10-25 | Toutes les limites d'activation sont des données du profil Clavier, sans borne fixe. Dérogation à l'invariant 4, validée par l'humain. |
| [0015](./adr-0015-reglage-chaud-profil-console-cdc.md) | Réglage à chaud du profil d'activation par la console CDC (PuTTY) | Proposed | architecture, firmware, protocol, diagnostic | Étend 0013 ; D2 amenderait `can_protocol` (flag VOLATILE, non implémentée) | Réglage console du profil (D1 implémentée, Clavier seul) ; D2 : transfert volatil sans flash Moteur, en attente. |
| [0016](./adr-0016-attribution-diagnostic-synchronisation-profil.md) | Attribution et diagnostic de la synchronisation du profil — correspondance UID→position unique et motifs d'échec distincts | Accepted | architecture, firmware, diagnostic | Amende 0013 ; amendée par 0023 | Attendus/acquittements du profil attribués par UID sur `uid_position.h` ; motifs d'échec nommés ; échéances comparées en signé. |
| [0017](./adr-0017-journal-diagnostic-auto-suffisant-horodatage-identite-redemarrages.md) | Journal de diagnostic auto-suffisant — horodatage, identité du firmware, redémarrages de carte et durée mesurée | Accepted | architecture, diagnostic, firmware | Amende 0016 ; étend 0005 ; amendée par 0025 | Horodatage `[T+… ms]`, identité du firmware au boot, `CARD_REBOOTED`, durée mesurée ; redémarrage jamais silencieux. |
| [0018](./adr-0018-retour-etat-bouton-canaux-moteur-vers-clavier.md) | Retour d'état du bouton et des canaux Moteur vers le Clavier — trame CARD_EVENT (0x424) | Accepted | architecture, firmware, protocol, diagnostic | Étend 0004/0009 ; amendée par 0025 | Trame `0x424` : appui court, déclenchement en attente/émis/bloqué, défauts de canal ; plus d'action Moteur silencieuse. |
| [0019](./adr-0019-phase-impact-impulsion-courte-avant-pull.md) | Phase IMPACT — impulsion courte à pleine puissance avant PULL dans le profil d'activation | Accepted | architecture, firmware, protocol, hardware | Amende 0013, étend 0015 ; amendée 2026-10-25 | Phase IMPACT avant PULL : impulsion 100 %, champ profil v3, réglable console ; reflash Moteur requis. |
| [0020](./adr-0020-comptabilite-echecs-cartes-moteur.md) | Rendre les cartes Moteur comptables de leurs échecs — acquittement répété, compteurs lisibles, commandes ignorées signalées | Accepted | architecture, firmware, protocol, diagnostic | Étend 0013/0015 ; dépend de 0018 ; amendée par 0023, 0024, 0025 | Ack `PROFILE_STATUS` répété, compteurs de carte lisibles (`0x403`/`0x425`), `COMMAND_IGNORED`. Modifie `shared/protocol`. |
| [0021](./adr-0021-conservation-bitmap-mesure-non-concluante.md) | Ne jamais détruire une assignation valide sur une mesure non concluante — conservation du bitmap précédent | Accepted | architecture, firmware, diagnostic | Amende 0009 ; dépend de 0018 et 0020 | Mesure non concluante : bitmap précédent conservé (même UID), flag et ligne explicite ; le clavier ne devient plus muet. |
| [0023](./adr-0023-synchronisation-profil-reprise-verification-etat.md) | Synchronisation du profil par reprises bornées et vérification de l'état détenu | Accepted | architecture, firmware, protocol, diagnostic | Remplace 0022 (déc. 1) ; amende 0013/0016/0017/0020 | Reprises complètes bornées puis réconciliation NVM explicite ; aucun changement de trame ni intervention répétée. |
| [0024](./adr-0024-reveil-solenoides-permanent-diagnostic-refus.md) | Réveil permanent des solénoïdes après tWAKE et refus pilote comptés par cause | Accepted | architecture, firmware, hardware, protocol, diagnostic | Amende 0020 | Réveil tWAKE verrouillé ; refus pilote ventilés (`shared/protocol`, trames inchangées) ; campagnes uptime et 24 h distinctes. |
| [0025](./adr-0025-journal-diagnostic-niveaux-runtime-agregation.md) | Journal CDC par niveaux d'exécution, verdicts conservés et statistiques agrégées | Accepted | architecture, firmware, diagnostic, protocol | Remplace 0022 ; amende 0004/0005/0009/0017/0018/0020 | Niveaux runtime, verdicts non masquables, statistiques agrégées ; renomme l'événement 0 ; trames CAN inchangées. |
| [0026](./adr-0026-keycode-modificateur-alteration-deux-touches.md) | Keycode modificateur d'altération à deux touches (Bémol / Dièse) | Accepted | architecture, firmware | — ; amendée par 0029 (§6, IMP-002) | Deux modificateurs maintenus B/D remplacent les cinq altérations par racine ; bascule naturelle/altérée, dernière pression gagne. |
| [0027](./adr-0027-commande-console-probe-autotest-ipropi.md) | Commande console `probe` — déclenchement de l'auto-test IPROPI depuis PuTTY | Proposed | architecture, firmware, diagnostic | renumérotée 2026-10-25 (ex-0026, numéro en doublon) | Mot `probe` = demande explicite d'auto-test par la console CDC, alias `t` conservé ; aucune trame nouvelle. |
| [0028](./adr-0028-inversion-canal-touche-intra-carte.md) | Inversion intra-carte du sens canal → touche — canal occupé le plus haut = note la plus grave du bloc | Accepted | architecture, firmware, protocol | Amende partiellement 0010 (§1, §5) | Dans un bloc, canaux parcourus 11 → 0 : canal 11 = touche la plus grave. Inter-cartes et flash v2 inchangés. |
| [0029](./adr-0029-keycodes-armure-et-alteration-temporaire.md) | Keycodes d'armure (CLE_B/CLE_D), d'altération temporaire (TEMP_B/TEMP_D) et nommage court | Accepted | architecture, firmware | Déprécie 0026 §6/IMP-002 (positions) | Armure mineure/majeure par tonique, altération temporaire, noms courts (ALT_B). Aucune position figée dans une ADR. |
| [0030](./adr-0030-transpositions-temporaires-par-cote.md) | Transpositions temporaires par côté (demi-ton et octave) maintenues | Accepted | architecture, firmware | Étend 0029 | Huit keycodes momentanés T_TEMP_ST/OCT_L/R : ±1 demi-ton / ±1 octave tant que maintenus, par côté ; note tenue figée. |
| [0031](./adr-0031-combos-positions.md) | Combos de positions — un keycode pour N touches pressées ensemble | Accepted | architecture, firmware | Étend 0029 | Table de combos dans le keymap, type ZMK ; seules les positions membres subissent la fenêtre ; sortie libre, toujours active. |
| [0032](./adr-0032-mode-rec-ecoute-ipropi-clavier-maitre.md) | Mode REC — écoute des touches du clavecin par IPROPI | Accepted | architecture, firmware, protocol, hardware, diagnostic | — | Keycodes REC_SOLE/REC_MUTE ; Core 1 Moteur balaie IPROPI, détecte les impulsions ; huit trames `LISTEN_*` ; option Serial Studio. |

## Historique (conservé pour trace — ne pas ouvrir par défaut)

Ces ADR ne portent plus la règle en vigueur, en totalité ou pour la partie
citée. Elles restent la trace de la décision et de sa justification.

| ADR | Titre | Statut | Pourquoi elle est ici |
|---|---|---|---|
| [0003](./adr-0003-bringup-can-phase3-lecture-uid-sysex.md) | Protocole de bring-up CAN Phase 3 — lecture UID Moteur et affichage SysEx MIDI | Proposed | Bring-up initial ; §6 (SysEx) remplacé par 0005. Les trames de bring-up restent décrites par `CAN.md`. |
| [0006](./adr-0006-mode-maintenance-push-only.md) | Mode maintenance push-only pour assemblage des solénoïdes | Accepted | §4 (durée/coupure) remplacé par 0012 ; le mode lui-même est décrit par 0012. Dérogation : invariant 4. |
| [0007](./adr-0007-hold-limite-20s.md) | Relèvement de la limite de durée de la phase HOLD à 20 s (jeu normal) | Accepted | Valeurs devenues des **défauts de profil** par 0014 ; plus de limite figée dans le firmware. Dérogation : invariant 4. |
| [0022](./adr-0022-precisions-banc-acquittement-tag-appui-court.md) | Précisions après le premier banc — acquittement répété inconditionnellement, refus de tag non signalé, appui court renommé | Superseded | Remplacée par 0023 (déc. 1) et 0025 (déc. 2–3). |

## Notes d'index

- **Collision 0026 corrigée le 2026-10-25** : l'ADR `probe` (Proposed, jamais
  implémentée — `bringup_console.c` ne connaît que `t`) est devenue **0027**.
   Le 0026 restant est celui des keycodes. Prochain numéro libre : **0033**.
- Les fichiers `impact-adr-*.md` de `docs/specs/` sont les fiches d'impact
  détaillées ; ils ne sont pas des ADR et ne doivent pas être ajoutés ici.
  L'état daté de l'implémentation est dans `docs/specs/historique-implementation.md`.

<!-- Exemple de ligne :
| [0000](./adr-0000-exemple.md) | Titre de la décision | Accepted | firmware | — | Résumé en une ligne. |
-->
-->
