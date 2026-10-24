# Procédure de build — firmware Moteur (Windows)

Note de procédure à l'usage des agents (Codeur, Validateur) et des humains du
projet pour générer **`build/moteur/moteur.elf`** sur un poste Windows. La carte
Moteur (RP2354B) n'a **ni port USB ni mode BOOTSEL** : le firmware est flashé
par **SWD** avec OpenOCD (procédure : `tests/build_clavier_windows.md` §6 et
`tests/moteur_phase2a.md` §F). Le CMake Moteur **ne produit pas** de `.uf2`
(pas d'appel à `pico_add_extra_outputs()`) : seul le `.elf` est généré.

**Statut : build incrémental validé le 2026-10-21** — `cmake --build "build/moteur"`
enchaîne la recompilation des unités modifiées puis `Linking CXX executable
moteur.elf`, sans erreur ni avertissement (les options de build du projet
n'activent pas `-Wall` : l'absence d'avertissement n'est pas une preuve en soi,
voir §5). Depuis la suppression de `pico_add_extra_outputs(moteur)`
(2026-10-21, carte sans USB) : `.elf` à `text 32 684 / data 0 / bss 1 988`
(RP2354B : flash 2 Mo, 520 Ko SRAM ; `firmware/moteur/boards/moteur_rp2354b.h:57`).
Le chiffre précédent (`text 32 704`) comprenait une entrée `binary_info`
supplémentaire de 20 octets (en-tête 4 o + charge utile 16 o) liée à la
génération des sorties dérivées ; `.text` (0x62a8) est inchangé.

**Configuration initiale : à confirmer.** La commande de l'étape 2 n'a **pas**
été rejouée depuis zéro (le cache `build/moteur` existait déjà) ; elle est
reconstruite par analogie avec
[`build_clavier_windows.md`](build_clavier_windows.md) et devra être validée au
premier poste qui repart d'un dossier de build vide.

## 1. Pré-requis d'environnement

Identiques à [`build_clavier_windows.md`](build_clavier_windows.md) §1 : Pico SDK
**2.3.1** (`PICO_SDK_PATH`), toolchain **Arm GNU 14.2 rel1**, générateur
**Ninja**, et un compilateur **host** natif (MinGW-w64/WinLibs) car le SDK
compile `picotool` et `pioasm` pour l'hôte.

Spécifique au Moteur :

- le sous-module **can2040** (`firmware/common/can2040`, épinglé v1.7.0) doit être
  présent, sinon la configuration échoue avec un message explicite :
  `git submodule update --init --recursive` ;
- `PICO_BOARD` vaut **`moteur_rp2354b`** par défaut (`firmware/moteur/CMakeLists.txt:11-14`,
  en-têtes de carte dans `firmware/moteur/boards/`) : **ne pas** passer
  `-DPICO_BOARD=pico2` comme pour le Clavier ;
- **OpenOCD + sonde CMSIS-DAP** pour le flash SWD (aucun flash USB possible) :
  chemins xPack, câblage SWCLK/SWDIO/GND et commande dans
  `tests/build_clavier_windows.md` §6 ;
- le pinmap Moteur (`moteur_pinmap`, `Netlist/netlist_to_pinmap.py`) est une
  cible **manuelle** : les GPIO sont figés dans `src/config.h` et le build
  **n'en dépend pas**. À relancer seulement si le schéma change.

## 2. Configuration initiale (à confirmer)

```powershell
cmake -S "firmware/moteur" -B "build/moteur" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
```

## 3. Build courant (incrémental)

```powershell
cmake --build "build/moteur"
```

Artefact à flasher : `build/moteur/moteur.elf`, par SWD (OpenOCD). Ne pas
chercher de mode BOOTSEL : la carte Moteur n'a pas de port USB. Commande de
flash : `tests/build_clavier_windows.md` §6.4.

## 4. Options

- `MOTEUR_ENABLE_CLOCK_TEST` (**ON** par défaut, `CMakeLists.txt:38`) : compile la
  feature de diagnostic d'horloge (GPIO41). `OFF` n'inclut aucun code et
  n'utilise que les stubs de `src/clock_test.h`.
- `MOTEUR_DIAG_TRACE` (**ON** au bring-up, ADR-0025 implémentée) : inclut les
  événements ordinaires, les compteurs et le répondeur `0x403`/`0x425`. `OFF`
  retire ces outils d'un build final validé, mais conserve les événements de
  santé/action et toutes les trames fonctionnelles.

## 5. Tests hôte et validation (procédure exécutée le 2026-10-21)

```powershell
powershell -File tests\host\run_host_tests.ps1   # 221 vérifications, 0 échec
cmake --build "build/moteur"                     # recompilation + link
```

Les tests hôte compilent `firmware/moteur/src/ipropi_ohm.c` (calcul ohmique pur,
ADR-0009) mais **pas** le reste du Moteur : ils ne remplacent pas un build.
ADR-0024 (implémentée, validée hôte 2026-10-24) a ajouté le harnais dédié au
réveil (`solenoid_driver.c` avec doublures GPIO/PWM) et des tests de ventilation
des compteurs. Procédure complète, build, tests hôte et campagne uptime/24 h :
[`procedure_adr0024_reveil_uptime.md`](./procedure_adr0024_reveil_uptime.md).
ADR-0025 (implémentée, validée hôte 2026-10-24) a ajouté l'option
`MOTEUR_DIAG_TRACE` et la validation des événements ordinaires/santé : voir
[`procedure_adr0025_journal_niveaux.md`](./procedure_adr0025_journal_niveaux.md).

Rediriger les sorties vers un fichier (`*> "build\...\build.log"`) puis n'en
extraire que les erreurs et le résumé.

**Forcer une recompilation.** Ninja affiche `no work to do` si rien n'a changé :
toucher les sources (`(Get-Item <fichier>).LastWriteTime = Get-Date`) ou
supprimer les objets concernés, par exemple :

```
build/moteur/CMakeFiles/moteur.dir/src/can_link.c.obj
build/moteur/CMakeFiles/moteur.dir/src/ipropi_selftest.c.obj
build/moteur/CMakeFiles/moteur.dir/D_/<chemin-du-repo>/shared/protocol/commissioning_table.c.obj
```

**Contrôle des avertissements.** Les options du projet n'incluant ni `-Wall` ni
`-Wextra`, un build « propre » ne prouve rien sur ce plan. Pour vérifier un
fichier modifié, réutiliser la ligne de compilation de `build/moteur/build.ninja`
(variables `DEFINES`, `FLAGS`, `INCLUDES`) en y ajoutant `-Wall -Wextra
-fsyntax-only` : c'est ce qui a été fait le 2026-10-21 sur les fichiers modifiés
(0 avertissement), avec un fichier témoin produisant bien un warning pour
prouver que le contrôle fonctionne.

## 6. Ce qui n'est pas couvert

Tout le comportement **électrique et mécanique** : profil PROBE 5 ms, estimation
ohmique et tri des bobines, intégrité du bus CAN, activations simultanées et
répétées. Campagne : [`bringup_commissioning_provisoire.md`](bringup_commissioning_provisoire.md).
