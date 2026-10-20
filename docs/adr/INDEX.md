# Index des ADR

Table de recherche rapide : lis cet index avant d'ouvrir une ADR, et n'ouvre que
celles dont le titre, les tags ou le résumé concernent ta tâche. Une ligne par
ADR, triées par numéro.

**Dernière vérification complète : 2026-10-20**

> Tant que cette date est « jamais », l'index peut être incomplet : liste aussi
> `docs/adr/` (noms de fichiers) avant de conclure qu'une ADR n'existe pas.

## Tenue de l'index

- **Architecte** : ajoute la ligne à la création de l'ADR (statut `Proposed`).
- **Documentaliste** : met à jour le statut quand l'ADR est acceptée (Mode A), ou
  reconstruit/contrôle l'index (Mode C).
- En cas de divergence, le frontmatter de l'ADR fait foi.
- Colonne « Relations » : `remplace 0003`, `remplacée par 0009`, ou `—`.
- Colonne « Résumé » : 20 mots maximum, avec les termes de l'ADR. Si l'ADR
  déroge à un invariant de `AGENTS.md`, le dire dans le résumé.

| ADR | Titre | Statut | Tags | Relations | Résumé |
|---|---|---|---|---|---|
| [0001](./adr-0001-hal-solenoide-drv8874-phen.md) | Contrat HAL de commande solénoïde DRV8874 (mode PH/EN) | Proposed | architecture, firmware, hardware | — | Contrat HAL solénoïde DRV8874 en mode PH/EN ; IPROPI via mux 74HC4067 (IC17) ; inversion de polarité pour RELEASE. |
| [0002](./adr-0002-integration-can2040.md) | Intégration de can2040 — vendoring, PIO/IRQ, GPIO, contrat callback | Proposed | architecture, firmware, protocol | — | Intégration can2040 (CAN par PIO) : vendoring, PIO/IRQ, GPIO, callback. Dérogation : licence GPLv3 à valider. |
| [0003](./adr-0003-bringup-can-phase3-lecture-uid-sysex.md) | Protocole de bring-up CAN Phase 3 — lecture UID Moteur et affichage SysEx MIDI | Proposed | architecture, firmware, protocol | §6 remplacé partiellement par 0005 | Bring-up CAN Phase 3 : lecture de l'UID Moteur, affichage SysEx MIDI, table uid_position.h d'amorçage (non source de vérité). |
| [0004](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) | Auto-test IPROPI, table d'assignation flash et SOLENOID_COMMAND | Accepted | architecture, firmware, protocol, hardware | Addendum SysEx remplacé partiellement par 0005 | Auto-test IPROPI (PROBE 20 %, un canal à la fois), table d'assignation flash, SOLENOID_COMMAND. Dérogation : invariant 1 reformulé. |
| [0005](./adr-0005-affichage-diagnostic-serie-cdc-remplace-sysex.md) | Affichage diagnostic sur journal série CDC, en remplacement du SysEx | Accepted | architecture, firmware, usb, diagnostic | Remplace partiellement 0003 (§6) et 0004 (addendum SysEx) | Diagnostic sur port série CDC (PuTTY) remplaçant tout SysEx de diagnostic ; trames CAN inchangées. |
| [0006](./adr-0006-mode-maintenance-push-only.md) | Mode maintenance push-only pour assemblage des solénoïdes | Accepted | architecture, firmware, protocol, maintenance | Amendée partiellement par 0007 (§4) | Mode MAINTENANCE push-only pour assemblage des solénoïdes, coupure à 10 s dans la couche pilote. Dérogation : invariant 4. |
| [0007](./adr-0007-hold-limite-20s.md) | Relèvement de la limite de durée de la phase HOLD à 20 s (jeu normal) | Accepted | architecture, firmware, hardware | Amende partiellement 0006 (§4) | Phase HOLD du jeu normal : coupure autonome relevée de 2 s à 20 s, duty HOLD plafonné à 25 %. Dérogation : invariant 4 (durée). |
<!-- Exemple de ligne :
| [0000](./adr-0000-exemple.md) | Titre de la décision | Accepted | firmware | — | Résumé en une ligne. |
-->
