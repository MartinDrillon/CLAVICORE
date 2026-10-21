# Procédures de build — Clavier (UF2) et Moteur (ELF/SWD) sous Windows

Note de procédure à l'usage des agents (Codeur, Validateur) et des humains du
projet. Elle couvre les deux firmwares :

- **Clavier** (RP2350, USB natif) : `build/clavier/clavier.uf2`, copié en mode
  BOOTSEL — sections 1 à 5 ;
- **Moteur** (RP2354B, **sans port USB**) : `build/moteur/moteur.elf`, flashé par
  **SWD** avec OpenOCD — section 6. Aucun `.uf2` n'est utilisé pour cette carte.

Statut : procédures validées — builds réussis sans warning/erreur. Dernière
validation : **2026-10-21** (ADR-0010) ; artefact Clavier
`build/clavier/clavier.uf2` de **129 536 octets**, `.elf` à `text 68 440 /
data 0 / bss 11 060` (RP2350 : 4 Mo flash, 520 Ko SRAM). La taille dépend des
options de build (§5), volontairement activées pendant le bring-up.

## 1. Pré-requis d'environnement

- **Pico SDK 2.3.1**, avec `PICO_SDK_PATH` pointant vers
  `C:/Users/marti/.pico-sdk/sdk/2.3.1`.
- **Toolchain ARM** : Arm GNU Toolchain `arm-none-eabi` 14.2 rel1
  (utilisée pour `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` /
  `CMAKE_ASM_COMPILER`).
- **Générateur Ninja** (`-G Ninja`).
- **Un compilateur host natif** : MinGW-w64 / WinLibs GCC 16.1.0, installé
  via winget en scope utilisateur :

  ```powershell
  winget install --id BrechtSanders.WinLibs.POSIX.UCRT --scope user
  ```

  Requis car le Pico SDK compile `picotool` et `pioasm` pour l'hôte pendant
  le build (ce ne sont pas des binaires ARM). Cette installation n'est
  nécessaire **qu'une seule fois par poste** : `picotool` reste ensuite mis
  en cache dans `build/clavier/_deps`.

### Piège connu — déplacement du dossier racine du repo

Si le dossier racine du repo est renommé ou déplacé après une première
configuration CMake, `CMakeCache.txt` garde l'ancien chemin absolu et le
build échoue avec une erreur du type :

```
The current CMakeCache.txt directory ... is different than the directory ...
where CMakeCache.txt was created
```

Solution : supprimer `build/clavier` (ne contient que des artefacts
régénérables, aucun code source) puis reconfigurer depuis zéro (étape 2).

## 2. Configuration initiale

À faire une fois par poste, ou après suppression de `build/clavier` :

```powershell
cmake -S "firmware/clavier" -B "build/clavier" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2 -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
```

## 3. Build courant (incrémental)

```powershell
cmake --build "build/clavier"
```

Artefact produit : `build/clavier/clavier.uf2`, à copier sur le Pico 2 en
mode BOOTSEL.

## 4. Dépannage

- **Erreur `No CMAKE_C_COMPILER could be found` pendant la configuration de
  `picotoolBuild`** : le compilateur host (MinGW) n'est pas dans le `PATH`
  de la session courante. Deux options :
  - ajouter temporairement
    `C:\Users\marti\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`
    au `PATH` avant de relancer le build ;
  - ou simplement ouvrir un nouveau terminal après l'installation de MinGW
    (le `PATH` mis à jour par winget n'est pris en compte que dans les
    nouvelles sessions).

## 5. Tests hôte et validation (procédure exécutée)

Commandes validées le **2026-10-21** (ADR-0010), à rejouer identiquement :

```powershell
powershell -File tests\host\run_host_tests.ps1   # 221 vérifications, 0 échec
cmake --build "build/clavier"                    # Clavier (pico2) → clavier.uf2
cmake --build "build/moteur"                     # Moteur → moteur.elf (flash SWD, §6)
```

Rediriger les sorties vers un fichier (`*> "build\...\build.log"`) plutôt que de
les laisser dans la conversation, puis n'en extraire que les erreurs et le
résumé.

**Forcer une recompilation.** Ninja affiche `no work to do` si rien n'a changé :
pour être sûr qu'une unité modifiée est bien recompilée, supprimer son objet avant
le build, par exemple :

```
build/clavier/CMakeFiles/clavier.dir/src/can_link.c.obj
build/clavier/CMakeFiles/clavier.dir/D_/<chemin-du-repo>/shared/protocol/commissioning_table.c.obj
build/moteur/CMakeFiles/moteur.dir/D_/<chemin-du-repo>/shared/protocol/commissioning_table.c.obj
```

**Options actives pendant le bring-up** (défauts de
`firmware/clavier/CMakeLists.txt:17,19` et `can_link.c:35`) :
`CLAVIER_SELFTEST_CALIBRATION=ON`, `CLAVIER_CDC_CONSOLE=ON`,
`CLAVIER_MAP_LOG=1`. Elles expliquent la taille actuelle de l'artefact et sont à
repasser à `OFF`/`0` en configuration finale (ADR-0009 §7/§9).

**Non couvert par ces commandes** (à faire au banc) : PROBE 5 ms, estimation
ohmique et tri des bobines (`tests/bringup_commissioning_provisoire.md`),
résolution de notes bout en bout (`tests/phase3b_bout_en_bout.md`).

## 6. Carte Moteur — build du `.elf` et flash SWD (pas de UF2)

La carte Moteur (RP2354B) n'a **ni port USB ni console UART** : son CMake ne
produit **aucun `.uf2`** (pas d'appel à `pico_add_extra_outputs()`), seulement
`build/moteur/moteur.elf`. Le firmware est flashé en **SWD** avec OpenOCD, à
partir de cet ELF.

### 6.1 Pré-requis spécifiques

- Sous-module **can2040** présent : `git submodule update --init --recursive`
  (la configuration échoue explicitement sinon).
- **OpenOCD** avec support RP2350. Poste de référence : xPack 0.12.0-7 installé
  par winget :

  ```powershell
  winget install xpack-dev-tools.openocd-xpack
  $OCD_DIR = "C:\Users\marti\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7"
  $OCD     = "$OCD_DIR\bin\openocd.exe"
  $SCRIPTS = "$OCD_DIR\openocd\scripts"
  Test-Path $OCD, "$SCRIPTS\interface\cmsis-dap.cfg", "$SCRIPTS\target\rp2350.cfg"   # 3 x True attendu
  ```

  `target not found` ou `unknown flash` au flash = OpenOCD sans support RP2350
  complet : le mettre à jour (build Raspberry Pi récent).
- **Sonde** : Raspberry Pi Debug Probe (CMSIS-DAP, VID `2E8A` PID `000C`) ou Pico
  « Picoprobe » préalablement flashé en debugprobe. Vérifier sa présence :

  ```powershell
  Get-PnpDevice -PresentOnly | Where-Object { $_.FriendlyName -match "CMSIS|Debug Probe|Picoprobe" } | Format-Table Status, Class, FriendlyName
  ```

- **Câblage SWD** (carte Moteur) : `SWCLK` = GPIO33 (`/SDCLK`), `SWDIO` =
  GPIO34 (`/SWDIO`), **GND obligatoire** ; `RUN` = GPIO35 (optionnel, reset).
  Ne **jamais** relier le 3V3 du probe à `3.3VIN` si la carte a sa propre
  alimentation. Détail : `tests/moteur_phase2a.md` §B.
- **Alimentation pendant le flash** : carte alimentée par son régulateur,
  **24 V coupé ou solénoïdes débranchés** ; remettre le 24 V après le flash.

### 6.2 Configuration (une fois par poste, ou après suppression de `build/moteur`)

```powershell
cmake -S "firmware/moteur" -B "build/moteur" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
```

**Ne pas** passer `-DPICO_BOARD=pico2` : le CMake Moteur impose par défaut
`PICO_BOARD=moteur_rp2354b` (`firmware/moteur/CMakeLists.txt:11-14`, en-têtes de
carte dans `firmware/moteur/boards/`). Cette configuration n'a pas encore été
rejouée depuis un dossier de build vide (voir `tests/build_moteur_windows.md`).

### 6.3 Build

```powershell
cmake --build "build/moteur"
Test-Path "build/moteur/moteur.elf"   # True attendu
```

Artefact à flasher : `build/moteur/moteur.elf`. Le CMake Moteur ne produit ni
`.uf2`, ni `.bin`, ni `.hex` (pas de `pico_add_extra_outputs()`) : l'ELF suffit
au flash SWD.

### 6.4 Flash SWD

```powershell
$ELF = "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/moteur/moteur.elf"
& $OCD -s $SCRIPTS -f interface/cmsis-dap.cfg -f target/rp2350.cfg `
  -c "adapter speed 5000" `
  -c "program `"$ELF`" verify reset exit"
$LASTEXITCODE   # 0 attendu
```

`program ... verify reset exit` écrit l'ELF en flash, relit pour comparer
(`verify`), redémarre la cible (`reset`) puis quitte OpenOCD. Sortie attendue :
`** Programming Finished **`, `** Verified OK **`, `** Resetting Target **`. En
cas d'erreurs sporadiques, descendre `adapter speed` à `1000` puis `500`.

### 6.5 Vérification après flash

Pas de console : contrôler au banc `GLOBAL_SLEEP` (GPIO15 haut), GPIO35 bas,
GPIO0 au niveau PULL, la PWM et, avec `MOTEUR_ENABLE_CLOCK_TEST=ON` (défaut),
GPIO41. Campagne : `tests/moteur_phase2a.md` §G.

### 6.6 Dépannage

| Symptôme | Action |
|---|---|
| `No CMSIS-DAP device`, `DAP init failed` | Probe non vu, carte non alimentée, GND absent, SWCLK/SWDIO inversés ou sur la mauvaise pin (33/34). |
| Timeouts, erreurs sporadiques | `adapter speed 1000` puis `500` ; fils plus courts. |
| `target not found`, `unknown flash` | OpenOCD sans support RP2350 complet : le mettre à jour. |
| Flash OK mais firmware inactif | Vérifier l'alimentation puis reset via `RUN` ; piste boot stage2 `W25Q080` non confirmé pour la flash interne du RP2354B. Ne pas changer le stage2 sans décision d'architecture. |
| Re-flash / effacement | Relancer 6.4 ; effacement complet : `-c "init; flash erase_sector 0 0 last; exit"`. |
| Reset seul | `& $OCD -s $SCRIPTS -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000" -c "init; reset; exit"`. |
