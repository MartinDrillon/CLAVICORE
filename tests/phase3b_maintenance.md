# Phase 3b — Mode maintenance (poussée en polarité inverse)

Procédure manuelle **PuTTY + matériel réel**. Cible : la poussée maintenance est
en **polarité inverse** du jeu normal (« REPEL » : l'aimant est repoussé), tenue
tant que la touche est pressée, coupée à **20 s**.

Références :
- [ADR-0006](../docs/adr/adr-0006-mode-maintenance-push-only.md) (§4 remplacé par ADR-0012)
- [ADR-0012](../docs/adr/adr-0012-mode-maintenance-force-inverse-duree-20s.md)
- [docs/specs/CAN.md](../docs/specs/CAN.md)
- [docs/specs/impact-adr-0012.md](../docs/specs/impact-adr-0012.md)

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

## Test 3 — Poussée en polarité inverse

Précondition : mode maintenance déjà actif.

1. Appuyer sur une touche routable, par exemple celle correspondant à MIDI 84
   si la carte aiguë est commissionnée.
2. Maintenir la touche moins de 20 s.
3. Relâcher.

**Attendu :**
- à l'appui :
  * le solénoïde correspondant est poussé en **polarité INVERSE** du jeu normal
    (l'aimant est repoussé, pas attiré) — **c'est le critère principal du
    test** : comparer le sens du geste avec celui d'une note jouée en jeu
    normal sur la même touche ;
  * poussée **continue**, sans à-coup ni changement de phase ;
  * ligne `[PUSH] midi=M key=K pos=P ch=C start`
- au relâchement :
  * coupure CAN envoyée ;
  * ligne `[PUSH] midi=M key=K pos=P ch=C stop`
- aucun `[NOTE]` MIDI USB ne doit être observé pendant ce test.

## Test 4 — Timeout 20 s

1. Appuyer sur une touche routable.
2. La maintenir plus de **20 s**.

**Attendu :**
- la coupure se produit vers **20 s** (et non 10 s) : ligne
  `[PUSH] ... timeout` envoyée par le Clavier ;
- la carte Moteur coupe de toute façon seule au plus tard vers 20 s : sa
  coupure locale est la **dernière barrière**, elle doit rester au même niveau
  que la constante du Clavier ;
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

## Test 8 — Retour au jeu normal après maintenance

1. En mode maintenance, pousser une touche (Test 3), puis relâcher.
2. Sortir du mode maintenance (Test 6).
3. Jouer la **même** touche en jeu normal.

**Attendu :**
- le geste du jeu normal est dans le **sens habituel** (attraction), pas inversé :
  la sortie de maintenance a bien restauré la polarité de jeu (`DIR` au niveau
  PULL) avec duty nul et délai de sécurité ;
- aucune poussée parasite ni maintien résiduel au repos.

Ce test couvre la régression la plus probable de l'ADR-0012 (une sortie de
maintenance qui laisserait `dir_channel` au niveau inverse rendrait le jeu
normal suivant inversé).

## Dépannage

| Symptôme | Cause probable |
|---|---|
| Aucune ligne `[MODE] maintenance=ON` et le journal affiche `position 84 behavior MAINT pressed` | Le dispatch de `main.c` est cassé : le bloc `KEYCODE_MAINTENANCE_TOGGLE` est imbriqué dans le test `KEYCODE_GET_UID` et inatteignable (voir `impact-adr-0012.md` §3). |
| `[STATUS] code=MAINTENANCE_NO_TABLE` | Table de commissioning absente ou invalide : lancer d'abord un auto-test (bouton Moteur 3 s). |
| `[STATUS] code=BUSY` | Un auto-test est en cours. |
| La poussée va dans le **même** sens qu'une note du jeu normal | `dir_level_maint` du profil actif n'inverse pas la polarité (ADR-0012), ou la carte Moteur n'a pas reçu le profil (lancer un auto-test, ADR-0013). |
| La poussée s'arrête vers 10 s | Une des deux bornes n'a pas été mise à jour : `MAINTENANCE_TIMEOUT_MS` (Clavier, `main.c`) ou `maint_max_ms` du profil actif (Moteur, `solenoid_profile_store`/profil Clavier). |
| Le jeu normal est inversé après un passage en maintenance | Sortie de MAINTENANCE qui ne repasse pas par `DIR_TO_PULL` (voir `impact-adr-0012.md` §2.1). |

## Non vérifiable sans matériel réel

* effort mécanique réel du duty 50 % sous chaque touche ;
* coupure thermique effective à 20 s sur la carte Moteur (à surveiller à la
  main : c'est le seul contrôle du budget thermique `durée × duty`) ;
* confort opérateur lors d'une séance complète (~49 écritures flash).
