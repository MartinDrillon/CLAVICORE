# Procédure — régler le profil d'activation des solénoïdes

Procédure à l'usage des humains et des agents (Codeur, Validateur), issue des
[ADR-0013](../adr/adr-0013-profil-activation-solenoides-centralise-clavier.md),
[ADR-0014](../adr/adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md)
et [ADR-0019](../adr/adr-0019-phase-impact-impulsion-courte-avant-pull.md)
(phase IMPACT, format v2).
Elle couvre : modifier le profil, compiler, flasher, propager aux cartes Moteur,
régler à chaud, vérifier la persistance, revenir en arrière.

Format et sémantique des champs : [`specs/profil-solenoides.md`](./specs/profil-solenoides.md).
Trames : [`specs/CAN.md`](./specs/CAN.md). Fichiers à revoir :
[`specs/impact-adr-0013.md`](./specs/impact-adr-0013.md) et
[`specs/impact-adr-0019.md`](./specs/impact-adr-0019.md).

> **Règle d'or** : un seul fichier de réglage,
> `firmware/clavier/src/solenoid_profile_config.h`. Les cartes Moteur ne sont
> **plus** reflashées pour ajuster un paramètre — **sauf la mise à jour unique
> qui introduit la phase IMPACT (ADR-0019)**, à faire une fois sur les 5 cartes.
> Après modification, il faut **flasher le Clavier puis propager** : auto-test
> IPROPI ou réglage à chaud à la console (ADR-0015).

## 1. Pré-requis

* Chaînes de build validées : [`../tests/build_clavier_windows.md`](../tests/build_clavier_windows.md)
  (Clavier + flash SWD Moteur, §6) et [`../tests/build_moteur_windows.md`](../tests/build_moteur_windows.md).
* Dossiers `build/clavier` et `build/moteur` configurés une fois par poste.
* Pour un premier déploiement (firmware Moteur modifié) : sonde CMSIS-DAP,
  carte alimentée, **24 V coupé ou solénoïdes débranchés pendant le flash**.
* Pour la mise à jour IMPACT (ADR-0019) : les **5** cartes Moteur et le Clavier
  doivent passer en v2 **ensemble** (voir §5).

## 2. Le fichier de réglage

`firmware/clavier/src/solenoid_profile_config.h` — contenu de référence (valeurs
par défaut ; **ce ne sont plus des bornes**, ADR-0014) :

```c
// SEUL fichier à modifier pour régler les solénoïdes.
// Après modification : flasher le Clavier, puis propager (auto-test ou console).
// Le profil est transmis aux cartes Moteur et persisté en flash (ADR-0013).
// Aucune enveloppe de sécurité n'est imposée par le firmware Moteur (ADR-0014).
// Depuis ADR-0019 (format v2) : phase IMPACT avant PULL.
#ifndef CLAVIER_SOLENOID_PROFILE_CONFIG_H
#define CLAVIER_SOLENOID_PROFILE_CONFIG_H

#include "solenoid_profile.h"

static const solenoid_profile_t k_solenoid_profile = {
    .pwm_freq_hz = 20000u,            // porteuse PWM, au-dessus du domaine audible
    .impact_duration_us = 800u,       // IMPACT : impulsion 100 % très courte (ADR-0019)
    .impact_duty_pct = 100u,
    .impact_max_us = 2000u,           // plafond de cohérence de l'impulsion
    .pull_duration_us = 15000u,       // PULL : attraction, après IMPACT
    .pull_duty_pct = 50u,
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

#endif  // CLAVIER_SOLENOID_PROFILE_CONFIG_H
```

> **À noter** : le fichier réel compile aujourd'hui `pull_duty_pct = 50` alors
> que les valeurs de repli Moteur documentent 100 (divergence signalée dans
> `specs/profil-solenoides.md` §10, non introduite par ADR-0019).

## 3. Modifier une valeur

1. Éditer `firmware/clavier/src/solenoid_profile_config.h` (une valeur, ou
   plusieurs).
2. Vérifier la cohérence structurelle (rejet en bloc sinon, `PROFILE_STATUS
   result=4`) :
   * duty ≤ 100 %, `hold_duty_pct <= hold_duty_max_pct`,
     `maint_duty_pct <= maint_duty_max_pct`, `probe_duty_pct <=
     probe_duty_max_pct`, et `duty_max_pct >=` chaque duty, y compris
     `impact_duty_pct` ;
   * `pull_duration_us <= pull_max_us`, `probe_duration_us <= probe_max_us`,
     **`impact_duration_us <= impact_max_us`** ;
   * polarités `dir_level_pull != dir_level_release` ; durées en ms ≤ 4 000 000.
3. Contrôler le bon sens physique (ADR-0014 : **aucune borne ne vous arrête**) :
   une durée ou un duty trop élevé peut détruire une bobine ou un driver.
   `impact_duration_us = 0` désactive la phase IMPACT.

## 4. Régler à chaud (ADR-0015 D1)

Sans recompiler : la console PuTTY du Clavier modifie le profil actif et le
propage. Les trois champs IMPACT sont réglables comme les autres.

```
p set impact_duration_us 1000
p set impact_max_us 5000 impact_duration_us 3000   // en une seule ligne
p diff                                              // lignes à recopier dans le .h
```

Procédure détaillée (PuTTY, refus, échecs d'acquittement, persistance) :
[`../tests/phase3b_profil_tuning.md`](../tests/phase3b_profil_tuning.md).

## 5. Compiler, flasher, mettre à jour en v2

### 5.1 Réglage ordinaire (profil v2 déjà en place)

```powershell
powershell -File tests\host\run_host_tests.ps1 *> "build\host_tests.log"   # 0 échec
cmake --build "build/clavier" *> "build\clavier\build.log"                 # -> clavier.uf2
```

Copier `build/clavier/clavier.uf2` sur le Pico 2 en mode BOOTSEL. **Ne pas
reflasher les cartes Moteur** pour un simple ajustement de valeur.

### 5.2 Mise à jour unique IMPACT (ADR-0019) — Clavier + 5 cartes

```powershell
cmake --build "build/moteur" *> "build\moteur\build.log"                   # -> moteur.elf
# flash SWD des 5 cartes : tests/build_clavier_windows.md §6.4, 24 V coupé
cmake --build "build/clavier"                                              # -> clavier.uf2
```

Puis, sous tension, **propager** (auto-test ou `p send`, §6). Attendu avant la
première propagation : les cartes annoncent `source = DEFAULTS` (les slots v1 ne
sont plus relus) et le Clavier signale `PROFILE_FALLBACK_IN_USE` ; la phase IMPACT
est alors **inactive**. Elle s'active après le premier transfert v2.

Une carte restée en v1 rejette le profil v2 : `PROFILE_STATUS result=7`
(`UNSUPPORTED_VERSION`) → la reflasher.

## 6. Propager et vérifier

1. Mettre sous tension. Ouvrir la console CDC (PuTTY).
2. Au boot, lire les lignes profil :
   * `[STATUS] code=PROFILE_DIVERGENT` : les cartes ont un profil différent du
     Clavier — normal après un changement, tant qu'aucune propagation n'a eu lieu ;
   * `[STATUS] code=PROFILE_FALLBACK_IN_USE` : une carte n'a pas de profil
     persistant valide (flash effacée ou slot v1) et joue avec les valeurs de repli.
3. Propager : **bouton Moteur maintenu 3 s** (auto-test) ou `p send` (console).
4. Le Clavier diffuse le profil v2 (66 o, 11 chunks) puis, en auto-test, `0x401`.
   Attendu :
   * pas de `PROFILE_SYNC_FAILED` ;
   * les `PROFILE_STATUS` de toutes les cartes présentes sont OK ;
   * l'auto-test se déroule normalement (bitmap, `[SELFTEST]`).
5. Rejouer une note et vérifier le comportement attendu. Mesure IMPACT : à
   l'oscilloscope sur le canal PWM, on doit voir une marche courte à
   `impact_duty_pct` (100 %) pendant `impact_duration_us`, puis le duty PULL.

## 7. Vérifier la persistance

1. Couper l'alimentation, remettre sous tension.
2. Au boot, les cartes doivent annoncer `source = NVM` et le **même**
   `profile_tag32` que le Clavier : aucune ligne `PROFILE_DIVERGENT` ni
   `PROFILE_FALLBACK_IN_USE`.
3. (Optionnel) Reflasher le firmware Moteur en SWD **sans** effacer les derniers
   secteurs, puis vérifier que le profil survit. Un effacement complet
   (`flash erase_sector ... last`) ramène aux valeurs de repli.

## 8. Revenir en arrière

1. Restaurer les valeurs précédentes dans `solenoid_profile_config.h`.
2. Recompiler et reflasher le Clavier.
3. Propager (auto-test ou `p send`) : les cartes adoptent l'ancien profil
   (écriture flash).
4. Pour neutraliser seulement IMPACT sans recompiler : `p set impact_duration_us 0`
   puis `p send`.

## 9. Dépannage

| Symptôme | Cause probable |
|---|---|
| `PROFILE_SYNC_FAILED` au test | un Moteur n'a pas acquitté : ancien firmware, carte absente, bus. Aucun `0x401` n'est émis. |
| `PROFILE_STATUS result=7` | version de profil non supportée : carte Moteur restée en v1 → reflasher (ADR-0019). |
| `PROFILE_STATUS result=4` | profil incohérent ou incompatible avec la mesure (fenêtre ADC/PROBE). |
| `PROFILE_STATUS result=6` | échec d'écriture flash Moteur (secteurs réservés ou `__flash_binary_end`). |
| `PROFILE_FALLBACK_IN_USE` persistant | la flash Moteur n'a jamais reçu de profil valide (ou slot v1) ; vérifier la position des slots et lancer une propagation. |
| Le comportement ne change pas | le profil n'est appliqué qu'à une propagation explicite : auto-test après le flash Clavier, ou `p send`. |
| PAS d'impulsion IMPACT observée | carte Moteur non reflashée en v2, ou `impact_duration_us = 0`, ou profil non propagé (repli `DEFAULTS`). |

## 10. État validé

* **v1 (ADR-0013/ADR-0014) — validée le 2026-10-21**, compilation + tests hôte
  (260 vérifications, 0 échec) :

  ```powershell
  powershell -File tests\host\run_host_tests.ps1   # 260 verifications, 0 echec (exit 0)
  cmake --build "build/moteur"                     # -> build/moteur/moteur.elf, exit 0
  cmake --build "build/clavier"                    # -> build/clavier/clavier.uf2, exit 0
  ```

  - Pour forcer la recompilation des unités touchées (Ninja affiche sinon
    `no work to do`), supprimer leurs `.obj` sous `build/<cible>/CMakeFiles/`.
  - Passe d'avertissements stricts sur les unités modifiées : rejouer la commande
    de `build/moteur/build.ninja` en retirant `-o`, `-MF`, `-MT` et en ajoutant
    `-Wall -Wextra -fsyntax-only`.
  - Empreinte : Moteur text 37 800 o / bss 6 612 o ; Clavier text 72 200 o /
    bss 11 088 o ; `__flash_binary_end` = `0x100083A8` (≈ 33,6 Ko).

* **v2 (ADR-0019) — implémentée et validée le 2026-10-22** (compilation + tests
  hôte ; validation matérielle à faire) :

  ```powershell
  powershell -File tests\host\run_host_tests.ps1   # 356 + 19 verifications, 0 echec
  cmake --build "build/moteur"                     # -> build/moteur/moteur.elf
  cmake --build "build/clavier"                    # -> build/clavier/clavier.uf2
  ```

  Passe stricte `-Wall -Wextra -Wpedantic` propre sur les unités modifiées ;
  layout v2 (66 o, 11 chunks) vérifié octet par octet. Mesure au banc d'IMPACT
  à faire : `tests/phase3b_profil_tuning.md` §2.4.

Validation **matérielle** non acquise (voir `specs/profil-solenoides.md` §8).
