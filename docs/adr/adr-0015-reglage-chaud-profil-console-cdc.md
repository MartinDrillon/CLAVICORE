---
title: "ADR-0015: Réglage à chaud du profil d'activation par la console CDC (PuTTY)"
status: "Proposed"
date: "2026-10-21"
authors: "Agent Maître"
tags: ["architecture", "firmware", "protocol", "diagnostic"]
supersedes: ""
superseded_by: ""
---

### Status
**Proposed** — la **décision D1** est déjà implémentée (Clavier seul, aucune
interface partagée modifiée) ; la **décision D2** touche `shared/protocol/`
(invariant 5) et **n'est pas implémentée** : validation humaine requise avant
tout code.

Aucune dérogation à un invariant. L'invariant 4 reste dérogé par ADR-0014 ; cette
ADR ne change aucune limite.

### Context

ADR-0013 centralise le profil d'activation dans
`firmware/clavier/src/solenoid_profile_config.h`. Il est transmis aux cartes
Moteur (`PROFILE_BEGIN/CHUNK/COMMIT`, `0x420`–`0x422`) **uniquement au lancement
d'un auto-test IPROPI**, puis persisté en flash Moteur. Pour régler finement la
force des solénoïdes, l'utilisateur doit donc modifier le fichier, reflasher le
Clavier et relancer un auto-test à chaque essai.

Besoin : modifier à la volée, depuis PuTTY, au minimum `pull_duration_us`,
`pull_duty_pct`, `pull_max_us`, `hold_duty_pct`, `hold_duty_max_pct`,
`hold_max_ms`, `min_rest_ms` ; le Clavier propage aux cartes ; les valeurs
réglées n'ont pas à survivre à une mise hors tension ; une fois la configuration
trouvée, l'utilisateur la recopie à la main dans le fichier.

Constats qui cadrent la décision :

1. **L'entrée CDC du Clavier n'a jamais fonctionné.** `bringup_console.c`
   (ADR-0009 §9) lit avec `getchar_timeout_us(0)`, mais le seul pilote stdio
   enregistré est celui de `usb_log.c`, qui n'a **pas de `in_chars`** ;
   `pico_stdio_usb` n'est pas lié (`PICO_STDIO_USB=OFF`, aucun symbole
   `stdio_usb`/`tud_cdc_read` dans l'ELF). Toute commande tapée dans PuTTY
   (`t`, `d`, `m`…) était ignorée. Toute couche de réglage bâtie sur cette
   console ne pouvait pas répondre : probable première cause de l'échec de la
   tentative précédente.
2. Côté Moteur, **le profil actif est relu à chaque transition** par le pilote
   (`solenoid_driver.c` : `profile()` = `solenoid_profile_active()`), et
   `solenoid_profile_store_write()` met à jour ce profil en RAM après l'écriture
   flash. Les sept champs demandés prennent donc effet **sans redémarrage**,
   sans modification du firmware Moteur.
3. Le Moteur **persiste toujours** un profil reçu (`handle_profile_commit`),
   et le format `PROFILE_BEGIN` ne permet pas de demander l'inverse
   (`flags` n'accepte que le bit `GLOBAL`).
4. Un `PROFILE_SYNC_FAILED` avec une seule carte a été observé lors d'une
   tentative antérieure (chemin `COMMIT` Moteur jamais validé sur matériel ;
   cause racine non établie, voir l'historique de session). Cette ADR ne le
   résout pas : elle rend les échecs diagnosticables (carte par carte).

### Decision

**D1 — Console de réglage côté Clavier, sur les trames existantes (implémentée).**

* `usb_log_read_char()` lit le FIFO CDC de TinyUSB ; la console l'utilise à la
  place de `getchar_timeout_us()` (réparation de l'entrée).
* Nouveau module pur `firmware/clavier/src/profile_tuning.[ch]` : table des
  champs de `solenoid_profile_t`, analyse d'affectations, application **atomique**
  sur une copie, contrôles **structurels** de `solenoid_profile_validate()`
  seulement (ADR-0014 : aucune borne de sécurité ajoutée).
* Syntaxes acceptées : `p set <champ> <valeur> [...]` et le copier-coller
  `.champ = 123u, .autre = 4u,`. Sous-commandes : `p get`, `p show`, `p diff`
  (lignes prêtes à recopier dans le `.h`), `p fields`, `p send` (re-diffuse le
  profil actif), `p reset` (diffuse le profil du fichier compilé).
* Le Clavier tient un **profil actif** (RAM, initialisé au fichier compilé) :
  dernier profil **acquitté** par toutes les cartes attendues. Le tag de
  comparaison au boot (`local_profile_tag`) le suit, pour que `PROFILE_DIVERGENT`
  ne se déclenche pas à tort après un réglage.
* Le réglage utilise le même transfert qu'à l'auto-test, avec un **but**
  `TUNING` (compte rendu `[PROFILE] reglage: OK|ECHEC`, délai 2 s, liste des
  cartes `acquitte`/`MANQUANT`) au lieu de `SELFTEST` (enchaînement sur `0x401`).
  Mêmes garde-fous qu'un auto-test : refus si requête CAN en cours ou jeu récent
  (< 500 ms) ; le Moteur refuse (`BUSY`) si un canal n'est pas `IDLE`.
* L'auto-test **continue d'envoyer le profil du fichier compilé** (comportement
  ADR-0013 inchangé) ; il le signale si cela remplace un profil réglé.
* Champs réglables : tous ceux dont l'effet est fiable à chaud (`pull_*`,
  `hold_*`, `release_*`, `maint_*`, `min_rest_ms`, `duty_max_pct`). **Verrouillés**
  avec une raison affichée : `pwm_freq_hz` (appliquée à l'init du Moteur
  seulement), `dir_level_*` (niveau non réécrit à l'arrêt : un `Note On`
  partirait en polarité inverse), `probe_*` (réservés à l'auto-test),
  `dir_safety_delay_us`, `wake_delay_us`. Ils restent modifiables par le fichier.
* **Limite assumée de D1 : la persistance côté Moteur est inévitable** (constat
  3). Chaque réglage écrit un secteur flash A/B sur chaque carte (≈ 0,1–0,4 s
  d'IRQ masquées, usure négligeable : 100 000 cycles × 2 secteurs). Après
  coupure des cartes, elles redémarrent donc avec le dernier profil réglé ; si le
  Clavier a aussi redémarré, `PROFILE_DIVERGENT` est signalé (attendu,
  jamais corrigé en silence, invariant 1). `p reset` ou un auto-test resynchronise.

**D2 — Transfert « volatil » : ne pas persister un profil de réglage
(NON implémentée, validation humaine requise).**

Pour satisfaire pleinement le besoin (réglage non persistant, pas d'écriture
flash à chaque essai, pas de fenêtre IRQ masquées ni de dépendance au chemin
flash pendant le réglage), proposer, dans `shared/protocol/` :

* `CAN_PROFILE_FLAG_VOLATILE = 0x02` dans `PROFILE_BEGIN.flags` (encodeur et
  décodeur acceptent `GLOBAL | VOLATILE`) ;
* `CAN_PROFILE_SOURCE_RAM = 2` dans `PROFILE_STATUS.source` (déjà logé dans les
  bits 3–4 de l'octet 3 : aucun changement de taille ni d'identifiant) ;
* Moteur : un transfert `VOLATILE` suit la même validation (CRC, structure,
  canaux `IDLE`) puis **applique le profil en RAM uniquement** ; le slot flash
  n'est pas touché ; au redémarrage, la carte retrouve le profil `NVM`/`DEFAULTS` ;
* Clavier : le but `TUNING` émet `VOLATILE` ; l'auto-test émet sans le drapeau
  (persistance conservée, ADR-0013) ;
* Sûreté vis-à-vis d'un Moteur non mis à jour : l'ancien décodeur rejette tout
  `flags` autre que `GLOBAL` (`can_protocol.c`), donc la carte **ignore** le
  transfert au lieu de le persister, et le Clavier signale l'absence
  d'acquittement.

Cette décision modifie un contrat partagé : elle exige l'acceptation de cette
ADR, la mise à jour de `shared/protocol/can_protocol.[ch]`, des tests hôte du
codec, du firmware Moteur (reflash SWD des cartes) et de
`docs/specs/profil-solenoides.md`.

### Consequences

**Positives**
- POS-001: réglage depuis PuTTY sans reflash du Clavier ni auto-test ; les
  profils en erreur ne sont jamais envoyés (rejet en bloc, message explicite).
- POS-002: `p diff` produit les lignes à recopier telles quelles dans
  `solenoid_profile_config.h`.
- POS-003: la console répond enfin (entrée CDC réparée) ; les commandes
  historiques `t c d m ?` fonctionnent aussi.
- POS-004: D1 ne demande aucun reflash des cartes Moteur ni changement de trame.

**Négatives**
- NEG-001: avec D1 seule, chaque réglage est persisté côté Moteur (écart par
  rapport au souhait « non persistant » ; sans conséquence sur le jeu).
- NEG-002: dépend du chemin `COMMIT` + `PROFILE_STATUS` du Moteur, non validé sur
  matériel ; en cas d'échec d'acquittement, le profil a pu être appliqué par
  certaines cartes (le compte rendu le dit, `p send` resynchronise).
- NEG-003: ADR-0014 : une valeur cohérente mais dangereuse est obéie ; la
  console rend l'erreur humaine plus facile (surveiller courant et température).

### Alternatives Considered

- ALT-001: **Réutiliser `getchar_timeout_us()` via `pico_stdio_usb`** — rejeté :
  deux pilotes stdio USB en conflit avec `usb_log.c` et le MIDI.
- ALT-002: **Modifier directement le firmware Moteur pour ne pas persister sur
  critère implicite** (par exemple `transfer_id` pair) — rejeté : contrat
  implicite non documenté (invariant 5) ; D2 le fait explicitement.
- ALT-003: **Trame CAN dédiée par champ** — rejeté : nouvel identifiant, nouveaux
  codecs, plus de surface pour une valeur qui doit rester atomique.
- ALT-004: **Autoriser aussi `pwm_freq_hz` et `dir_level_*` à chaud** — rejeté
  tant que le Moteur ne les ré-applique pas (D2 pourrait y associer une
  ré-application PWM contrôlée).

### Implementation Notes

- IMP-001: fichiers : `firmware/clavier/src/profile_tuning.[ch]`,
  `bringup_console.c`, `usb_log.[ch]`, `can_link.[ch]` ; tests hôte :
  `tests/host/test_phase3_helpers.c` (module), `tests/host/test_console_tuning.c`
  (console) ; test manuel : `tests/phase3b_profil_tuning.md`.
- IMP-002: la console est désactivable (`CLAVIER_CDC_CONSOLE=OFF`) pour la
  configuration finale ; le réglage disparaît avec elle.
- IMP-003: PuTTY : « Local echo » = *Force on* et « Local line editing » =
  *Force on* (le Clavier ne renvoie pas les caractères saisis).
- IMP-004: non vérifié sans matériel : réception CDC sur le Clavier réel,
  acquittements `0x423`, application effective de la force aux solénoïdes
  (mesure : courant PWM/IPROPI à l'oscilloscope sur un canal pendant un `Note On`
  avant/après un `p set pull_duty_pct`).
