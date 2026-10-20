---
title: "ADR-0007: Relèvement de la limite de durée de la phase HOLD à 20 s (jeu normal)"
status: "Accepted"
date: "2026-10-19"
authors: "Agent Architecte (rédigé sur décision humaine)"
tags: ["architecture", "firmware", "hardware"]
supersedes: ""
superseded_by: ""
---

### Status
**Dérogation à un invariant — validation humaine requise.**

Cette ADR assouplit la composante « durée » de l'invariant 4 d'`AGENTS.md`
pour la phase HOLD du **jeu normal** : la coupure autonome passe de 2 s à
**20 s**. Décision et validation de l'utilisateur le **2026-10-19**.

**Accepted**

Amende partiellement (sans les superséder) :

* ADR-0006 §4 : la ligne « coupure sécurité HOLD à 2 s » du profil normal ;
* ADR-0004 (section Moteur) : « un note off perdu est couvert par
  `SOLENOID_HOLD_MAX_MS` » reste vrai, avec la nouvelle valeur.

*Historique : une première version proposait de supprimer toute limite. Elle a
été écartée par l'utilisateur au profit d'un filet de sécurité à 20 s.*

### Context

* Le profil normal est PULL 15 ms / 100 %, puis HOLD à 25 % jusqu'au `note_off`
  (ADR-0001, ADR-0004). Le driver relâchait seul après
  `SOLENOID_HOLD_MAX_MS = 2000 ms` (invariant 4).
* Exigence musicale : tant que l'interprète maintient une touche, la note doit
  tenir. 2 s est trop court ; aucune note jouée ne dure 20 s.
* Justification thermique fournie par l'utilisateur : en HOLD à 25 %, la
  chauffe de la bobine est négligeable. Conditions connues : duty 25 %.
  Fréquence PWM du test, durée et élévation de température non relevées
  (l'utilisateur les juge superflues).
* Une limite très grande n'est pas une option : `SOLENOID_HOLD_MAX_MS * US_PER_MS`
  est calculé en `uint32_t` et déborde dès ≈ 4 294 967 ms (≈ 71,6 min).
  20 000 ms × 1000 = 20 000 000 µs est sans risque de débordement.
* Le profil MAINTENANCE (ADR-0006) reste inchangé (coupure à 10 s).

### Decision

1. **`SOLENOID_HOLD_MAX_MS` passe de 2000 à 20000.** Le mécanisme de coupure
   autonome du driver (relâchement forcé, drapeau `hold_timed_out`, message
   `HOLD max atteint`) est **conservé tel quel**.
2. **Plafond de duty HOLD** : ajout de `SOLENOID_HOLD_DUTY_MAX_PERCENT = 25`,
   ré-appliqué par le driver (comme `SOLENOID_DUTY_MAX_PERCENT`), avec
   `_Static_assert(SOLENOID_HOLD_DUTY_PERCENT <= SOLENOID_HOLD_DUTY_MAX_PERCENT)`.
   La durée de 20 s n'est validée que dans cette enveloppe thermique ; relever
   ce plafond exige une nouvelle validation et une ADR.
3. **Garde de compilation** : `_Static_assert(SOLENOID_HOLD_MAX_MS <= 4000000u)`
   (borne anti-débordement de `* US_PER_MS` sur 32 bits).
4. **Comportement après coupure** : le solénoïde est relâché alors que la
   touche peut rester pressée ; un nouveau cycle exige un relâchement puis un
   nouvel appui (le `note_off` tardif est sans effet).
5. Le profil MAINTENANCE et toutes les autres bornes (PULL 50 ms max, duty,
   repos minimal, protections DRV8874) sont inchangés.

### Consequences

#### Positive
* **POS-001**: Toute note jouée en pratique est tenue sans coupure intempestive.
* **POS-002**: Le filet de sécurité de l'invariant 4 subsiste : un `note_off`
  perdu, un Clavier planté ou un bug logiciel ne maintient pas la bobine plus
  de 20 s.
* **POS-003**: Changement minimal (une constante, deux garde-fous), sans nouvelle
  interface.

#### Negative
* **NEG-001**: Un `note_off` perdu laisse la note bloquée jusqu'à 20 s au lieu
  de 2 s.
* **NEG-002**: Validation thermique incomplète (fréquence PWM, durée,
  température non relevées) ; risque résiduel accepté par l'utilisateur.
* **NEG-003**: Le plafond duty HOLD de 25 % bride l'ajustement empirique du
  profil (relèvement = ADR).
* **NEG-004**: Point ouvert, antérieur à cette ADR : un `note_on` reçu pendant
  RELEASE ou REST est refusé (`BUSY`/`REST`) sans nouvelle tentative ; la touche
  reste pressée sans que la note joue en cas de réattaque rapide. À traiter par
  une ADR dédiée si observé en jeu.

### Alternatives Considered

#### Suppression complète de la limite (première version de cette ADR)
* **ALT-001**: **Description**: Retirer le test d'expiration, `hold_timed_out`
  et sa constante ; borne de sécurité reportée sur le seul duty.
* **ALT-002**: **Rejection Reason**: L'utilisateur préfère conserver un filet
  de sécurité contre un bug grave ; aucune note ne dure 20 s.

#### Valeur sentinelle (`0xFFFFFFFF`)
* **ALT-003**: **Description**: Conserver la constante avec sa valeur maximale.
* **ALT-004**: **Rejection Reason**: Déborde dans `* US_PER_MS` (coupure
  silencieuse à ≈ 71,6 min), comportement trompeur.

#### Watchdog de vivacité Clavier → Moteur (heartbeat CAN)
* **ALT-005**: **Description**: Relâcher le HOLD si le Clavier ne répète plus
  un message de présence.
* **ALT-006**: **Rejection Reason**: Nouveau message partagé (ADR préalable,
  invariant 5), non demandé.

### Implementation Notes
* **IMP-001**: `firmware/moteur/src/solenoid_profile.h` : `SOLENOID_HOLD_MAX_MS 20000u` ;
  ajouter `SOLENOID_HOLD_DUTY_MAX_PERCENT 25u` ; supprimer le commentaire de
  dérogation provisoire (« Durée de maintien illimitée… ») ; commentaire
  pointant vers cette ADR.
* **IMP-002**: `firmware/moteur/src/solenoid_driver.c` : clamp du duty HOLD par
  `SOLENOID_HOLD_DUTY_MAX_PERCENT` ; `_Static_assert` des points 2 et 3. Le
  test d'expiration HOLD reste inchangé.
* **IMP-003**: `tests/moteur_phase2a.md` : remplacer « HOLD max 2 s » /
  « 2000 ms » par 20 s (l. ≈ 249 et 266).
* **IMP-004**: Le Documentaliste reporte l'amendement dans ADR-0006 §4
  (HOLD 2 s → 20 s), `AGENTS.md` invariant 4 (mention de cette dérogation) et
  `docs/specs` si concerné.
* **IMP-005**: Test manuel recommandé : maintien d'une touche ≥ 20 s, relevé de
  température bobine/DRV8874, puis vérification du relâchement forcé à 20 s.

### References
* **REF-001**: [ADR-0001](./adr-0001-hal-solenoide-drv8874-phen.md) — machine PULL/HOLD/RELEASE.
* **REF-002**: [ADR-0004](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) — `SOLENOID_COMMAND`, absence d'acquittement.
* **REF-003**: [ADR-0006](./adr-0006-mode-maintenance-push-only.md) — profil MAINTENANCE borné.
* **REF-004**: `AGENTS.md`, invariant 4.
