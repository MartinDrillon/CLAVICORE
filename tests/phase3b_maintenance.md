# Phase 3b — Mode maintenance push-only

Procédure manuelle **PuTTY + matériel réel**.

Références :
- [ADR-0006](../docs/adr/adr-0006-mode-maintenance-push-only.md)
- [docs/specs/CAN.md](../docs/specs/CAN.md)

## Préparation

1. Flasher `build\clavier\clavier.uf2`.
2. Flasher `build\moteur\moteur.elf` par **SWD** (OpenOCD,
   `tests/moteur_phase2a.md` §E–F) : aucun `.uf2` Moteur n'existe (carte sans USB).
3. Ouvrir **PuTTY** sur le port **CDC** du Clavier.
4. Vérifier qu'une table de commissioning valide est déjà présente.

## Test 1 — Entrée en maintenance

1. Mettre le système sous tension.
2. Vérifier les lignes de boot habituelles :
   * `[BOOT] ...`
   * `[TABLE] loaded ...`
3. Appuyer sur la touche de contrôle **MAINT** (position 84).

**Attendu :**
- les notes éventuellement actives sont coupées ;
- ligne `[MODE] maintenance=ON` ;
- aucun message MIDI USB de note n'est envoyé tant que ce mode reste actif.

## Test 2 — Refus sans table

Précondition : effacer ou invalider la table de commissioning.

1. Redémarrer.
2. Vérifier `[TABLE] invalid ...` et `[STATUS] code=FLASH_TABLE_INVALID`.
3. Appuyer sur **MAINT**.

**Attendu :**
- refus d'entrée ;
- ligne `[STATUS] code=MAINTENANCE_NO_TABLE` ;
- absence de ligne `[MODE] maintenance=ON`.

## Test 3 — Poussée simple

Précondition : mode maintenance déjà actif.

1. Appuyer sur une touche routable, par exemple celle correspondant à MIDI 84
   si la carte aiguë est commissionnée.
2. Maintenir la touche moins de 10 s.
3. Relâcher.

**Attendu :**
- à l'appui :
  * le solénoïde correspondant est poussé en **PULL seul** ;
  * ligne `[PUSH] midi=M key=K pos=P ch=C start`
- au relâchement :
  * coupure CAN envoyée ;
  * ligne `[PUSH] midi=M key=K pos=P ch=C stop`
- aucun `[NOTE]` MIDI USB ne doit être observé pendant ce test.

## Test 4 — Timeout 10 s

1. Appuyer sur une touche routable.
2. La maintenir plus de **10 s**.

**Attendu :**
- la carte Moteur coupe seule la poussée au plus tard vers 10 s ;
- ligne `[PUSH] ... timeout` ;
- tant que la touche n'est pas relâchée, un nouvel appui sur une autre touche
  reste refusé.

## Test 5 — Une seule touche à la fois

1. Appuyer sur une première touche routable.
2. Sans relâcher, appuyer sur une seconde touche routable.

**Attendu :**
- la seconde demande est refusée ;
- ligne `[STATUS] code=MAINTENANCE_BUSY` ;
- seule la première touche reste active.

## Test 6 — Sortie du mode maintenance

1. Depuis le mode maintenance, laisser si possible toutes les touches relâchées.
2. Appuyer sur **MAINT**.

**Attendu :**
- tous les canaux actifs sont coupés avant retour ;
- ligne `[MODE] maintenance=OFF` ;
- le jeu normal redevient possible au prochain appui.

## Test 7 — Refus pendant auto-test

1. Déclencher un auto-test Moteur (bouton 3 s).
2. Pendant la session d'auto-test, appuyer sur **MAINT**.

**Attendu :**
- pas de bascule ;
- statut de refus (`BUSY` avec auto-test en cours) ;
- absence de ligne `[MODE] maintenance=ON`.

## Non vérifiable sans matériel réel

* effort mécanique réel du duty 50 % sous chaque touche ;
* coupure thermique effective à 10 s sur la carte Moteur ;
* confort opérateur lors d'une séance complète (~49 écritures flash).
