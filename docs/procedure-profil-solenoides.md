# Procédure — régler le profil d'activation des solénoïdes

Procédure à l'usage des humains et des agents (Codeur, Validateur), issue des
[ADR-0013](../adr/adr-0013-profil-activation-solenoides-centralise-clavier.md)
et [ADR-0014](../adr/adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md).
Elle couvre : modifier le profil, compiler, flasher, propager aux cartes Moteur,
vérifier la persistance, revenir en arrière.

Format et sémantique des champs : [`specs/profil-solenoides.md`](./specs/profil-solenoides.md).
Trames : [`specs/CAN.md`](./specs/CAN.md). Fichiers à revoir :
[`specs/impact-adr-0013.md`](./specs/impact-adr-0013.md).

> **Règle d'or** : un seul fichier de réglage,
> `firmware/clavier/src/solenoid_profile_config.h`. Les cartes Moteur ne sont
> **plus** reflashées pour ajuster un paramètre. Après modification, il faut
> **flasher le Clavier puis lancer un auto-test IPROPI** pour propager.

## 1. Pré-requis

* Chaînes de build validées : [`../tests/build_clavier_windows.md`](../tests/build_clavier_windows.md)
  (Clavier + flash SWD Moteur, §6) et [`../tests/build_moteur_windows.md`](../tests/build_moteur_windows.md).
* Dossiers `build/clavier` et `build/moteur` configurés une fois par poste.
* Pour un premier déploiement (firmware Moteur modifié) : sonde CMSIS-DAP,
  carte alimentée, **24 V coupé ou solénoïdes débranchés pendant le flash**.

## 2. Le fichier de réglage

`firmware/clavier/src/solenoid_profile_config.h` — contenu initial (valeurs
validées au banc ; **ce ne sont plus des bornes**, ADR-0014) :

```c
// SEUL fichier à modifier pour régler les solénoïdes.
// Après modification : flasher le Clavier, puis lancer un auto-test IPROPI.
// Le profil est transmis aux cartes Moteur et persisté en flash (ADR-0013).
// Aucune enveloppe de sécurité n'est imposée par le firmware Moteur (ADR-0014).
#ifndef CLAVICORE_SOLENOID_PROFILE_CONFIG_H
#define CLAVICORE_SOLENOID_PROFILE_CONFIG_H

#include "solenoid_profile.h"

static const solenoid_profile_t k_solenoid_profile = {
    .pwm_freq_hz = 20000u,            // porteuse PWM, au-dessus du domaine audible
    .pull_duration_us = 15000u,       // PULL : attraction, très court
    .pull_duty_pct = 100u,
    .pull_max_us = 50000u,            // plafond opératoire du PULL
    .hold_duty_pct = 25u,             // HOLD : maintien jusqu'au note off
    .hold_duty_max_pct = 25u,
    .hold_max_ms = 20000u,            // coupure de sécurité du HOLD
    .release_duration_us = 5000u,     // RELEASE : polarité inversée, court
    .release_duty_pct = 50u,
    .probe_duration_us = 5000u,       // PROBE : auto-test IPROPI
    .probe_duty_pct = 20u,
    .probe_max_us = 10000u,
    .probe_duty_max_pct = 20u,
    .maint_duty_pct = 50u,            // MAINTENANCE : poussée inverse tenue
    .maint_duty_max_pct = 100u,
    .maint_max_ms = 20000u,           // coupure de sécurité de la MAINTENANCE
    .maint_min_rest_ms = 2000u,       // repos après une poussée maintenance
    .min_rest_ms = 10u,               // repos minimal entre deux cycles
    .dir_safety_delay_us = 200u,      // délai duty=0 -> changement de polarité
    .wake_delay_us = 2000u,           // tWAKE du DRV8874 après réveil
    .dir_level_pull = 0u,             // niveaux PH/IN2 (à confirmer au banc)
    .dir_level_release = 1u,
    .dir_level_maint = 1u,            // poussée inverse du jeu normal
    .duty_max_pct = 100u,             // plafond absolu appliqué par le driver
};

#endif  // CLAVICORE_SOLENOID_PROFILE_CONFIG_H
```

## 3. Modifier une valeur

1. Éditer `firmware/clavier/src/solenoid_profile_config.h` (une valeur, ou
   plusieurs).
2. Vérifier la cohérence structurelle : duty ≤ 100 %, `hold_duty_pct <=
   hold_duty_max_pct`, `pull_duration_us <= pull_max_us`,
   `probe_duration_us <= probe_max_us`, polarités `dir_level_pull !=
   dir_level_release`, durées en ms ≤ 4 000 000. Un profil incohérent est rejeté
   en bloc par le Moteur (`PROFILE_STATUS result=4`) et le Clavier abandonne
   l'auto-test.
3. Contrôler le bon sens physique (ADR-0014 : **aucune borne ne vous arrête**) :
   une durée ou un duty trop élevé peut détruire une bobine ou un driver.

## 4. Compiler et flasher

```powershell
powershell -File tests\host\run_host_tests.ps1 *> "build\host_tests.log"   # 221+ vérifications, 0 échec
cmake --build "build/clavier" *> "build\clavier\build.log"                 # -> build/clavier/clavier.uf2
```

Copier `build/clavier/clavier.uf2` sur le Pico 2 en mode BOOTSEL. **Ne pas
reflasher les cartes Moteur** pour un simple ajustement de valeur.

Premier déploiement seulement (le firmware Moteur change) :

```powershell
cmake --build "build/moteur" *> "build\moteur\build.log"                   # -> build/moteur/moteur.elf
# flash SWD : tests/build_clavier_windows.md §6.4, 24 V coupé
```

## 5. Propager et vérifier

1. Mettre sous tension. Ouvrir la console CDC (PuTTY).
2. Au boot, lire les lignes profil :
   * `[STATUS] code=PROFILE_DIVERGENT` : les cartes ont un profil différent du
     Clavier — normal après un changement, tant qu'aucun auto-test n'a été lancé ;
   * `[STATUS] code=PROFILE_FALLBACK_IN_USE` : une carte n'a pas de profil
     persistant (flash effacée) et joue avec les valeurs de repli.
3. Lancer l'auto-test : **bouton Moteur maintenu 3 s**.
4. Le Clavier diffuse le profil puis `0x401`. Attendu :
   * pas de `PROFILE_SYNC_FAILED` ;
   * les `PROFILE_STATUS` de toutes les cartes présentes sont OK ;
   * l'auto-test se déroule normalement (bitmap, `[SELFTEST]`).
5. Rejouer une note et vérifier le comportement attendu (durée, sens, maintien).

## 6. Vérifier la persistance

1. Couper l'alimentation, remettre sous tension.
2. Au boot, les cartes doivent annoncer `source = NVM` et le **même**
   `profile_tag32` que le Clavier : aucune ligne `PROFILE_DIVERGENT` ni
   `PROFILE_FALLBACK_IN_USE`.
3. (Optionnel) Reflasher le firmware Moteur en SWD **sans** effacer les derniers
   secteurs, puis vérifier que le profil survit. Un effacement complet
   (`flash erase_sector ... last`) ramène aux valeurs de repli.

## 7. Revenir en arrière

1. Restaurer les valeurs précédentes dans `solenoid_profile_config.h`.
2. Recompiler et reflasher le Clavier.
3. Lancer un auto-test : les cartes adoptent l'ancien profil (écriture flash).

## 8. Dépannage

| Symptôme | Cause probable |
|---|---|
| `PROFILE_SYNC_FAILED` au test | un Moteur n'a pas acquitté : ancien firmware, carte absente, bus. Aucun `0x401` n'est émis. |
| `PROFILE_STATUS result=4` | profil incohérent ou incompatible avec la mesure (fenêtre ADC/PROBE). |
| `PROFILE_STATUS result=6` | échec d'écriture flash Moteur (secteurs réservés ou `__flash_binary_end`). |
| `PROFILE_FALLBACK_IN_USE` persistant | la flash Moteur n'a jamais reçu de profil valide ; vérifier la position des slots. |
| Le comportement ne change pas | le profil n'est appliqué qu'à un auto-test explicite : le lancer après le flash Clavier. |

## 9. État validé (2026-10-21)

Procédure exécutée par le Validateur, à rejouer après toute modification :

```powershell
powershell -File tests\host\run_host_tests.ps1   # 260 verifications, 0 echec (exit 0)
cmake --build "build/moteur"                     # -> build/moteur/moteur.elf, exit 0
cmake --build "build/clavier"                    # -> build/clavier/clavier.uf2, exit 0
```

- Pour forcer la recompilation des unités touchées (Ninja affiche sinon
  `no work to do`), supprimer leurs `.obj` sous `build/<cible>/CMakeFiles/`
  avant le build.
- Passe d'avertissements stricts sur les unités modifiées : rejouer la commande
  de `build/moteur/build.ninja` (ou `build/clavier/build.ninja`) en retirant
  `-o`, `-MF`, `-MT` et en ajoutant `-Wall -Wextra -fsyntax-only`. Un défaut
  réel y a été trouvé et corrigé (`shared/protocol/can_protocol.c` : comparaison
  toujours vraie sur une énumération non signée, non vue par le gcc hôte).
- Empreinte : Moteur text 37 800 o / bss 6 612 o ; Clavier text 72 200 o /
  bss 11 088 o ; `__flash_binary_end` = `0x100083A8` (≈ 33,6 Ko), très en
  dessous des slots réservés.

Validation **matérielle** non acquise (voir `specs/profil-solenoides.md` §8).
