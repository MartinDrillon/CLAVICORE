# Setup, build et flash — Clavier et Moteur

Commandes de référence pour installer l’environnement, compiler et flasher les
deux firmwares sous Windows. Sources : `tests/build_clavier_windows.md`,
`tests/build_moteur_windows.md`, `tests/moteur_phase2a.md`.

## 0. Version de format du profil : dernière version (v3)

- L’arbre courant compile **`SOLENOID_PROFILE_FORMAT_VERSION = 3`**
  (`shared/protocol/solenoid_profile.h`), payload **54 octets**.
- **Il n’existe aucun `-D...` ni case CMake pour choisir une autre version.** La
  version est une constante compilée, protégée par l’invariant 5.
- Tout autre numéro de version vu dans un journal provient d’un **ancien
  binaire**. La correction normale est de recompiler et reflasher **Clavier et
  toutes les cartes Moteur avec l’arbre courant**.
- Un enregistrement flash d’une version antérieure n’est pas relu, la carte
  repart en `source=DEFAULTS`, puis le premier `p reset`/`p send`/auto-test écrit
  la version courante.

## 1. Pré-requis (une fois par poste)

- **Pico SDK 2.3.1** : `C:/Users/marti/.pico-sdk/sdk/2.3.1`.
- **Toolchain ARM GNU** `arm-none-eabi` 14.2 rel1.
- **Ninja**.
- **Compilateur hôte** MinGW-w64/WinLibs (le SDK compile `picotool` et `pioasm`) :

  ```powershell
  winget install --id BrechtSanders.WinLibs.POSIX.UCRT --scope user
  ```

- **OpenOCD RP2350** (flash Moteur par SWD) :

  ```powershell
  winget install xpack-dev-tools.openocd-xpack
  $OCD_DIR = "C:\Users\marti\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7"
  $OCD     = "$OCD_DIR\bin\openocd.exe"
  $SCRIPTS = "$OCD_DIR\openocd\scripts"
  Test-Path $OCD, "$SCRIPTS\interface\cmsis-dap.cfg", "$SCRIPTS\target\rp2350.cfg"  # 3 x True
  ```

- **Sonde SWD** : Raspberry Pi Debug Probe (CMSIS-DAP `2E8A:000C`) ou Picoprobe.
  Câblage Moteur : `SWCLK`=GPIO33, `SWDIO`=GPIO34, **GND**, `RUN`=GPIO35
  (optionnel). Ne jamais relier le 3V3 du probe au 3.3VIN d’une carte déjà
  alimentée.

## 2. Préparation du dépôt

```powershell
$ROOT = "D:\Documents\MUSIQUE\EPINETTE\Kit Heugel_Bedard\.kilo\worktrees\nettoyage-log-putty"
Set-Location $ROOT
git submodule update --init --recursive
```

Dans un **worktree**, les caches `build/` d’un autre dossier contiennent des
chemins absolus : configurer un dossier de build **propre au worktree** plutôt que
réutiliser `build/` tel quel.

## 3. Configuration CMake

Clavier (une fois, ou après suppression de `build/clavier`) :

```powershell
cmake -S "firmware/clavier" -B "build/clavier" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2 -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
```

Moteur (ne pas passer `PICO_BOARD=pico2`, le CMake impose `moteur_rp2354b`) :

```powershell
cmake -S "firmware/moteur" -B "build/moteur" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="C:/Users/marti/.pico-sdk/sdk/2.3.1" -DCMAKE_C_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe" -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-g++.exe" -DCMAKE_ASM_COMPILER="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-gcc.exe"
```

### Options utiles

| Option | Défaut bring-up | Effet |
|---|---|---|
| `CLAVIER_DIAG_DEFAULT_LEVEL` | `1` | `l 0` verdicts, `1` bring-up, `2` trace |
| `CLAVIER_DIAG_TRACE` | `ON` | inclut niveau 2, RAW, notes, `[KEY]` |
| `MOTEUR_DIAG_TRACE` | `ON` | événements ordinaires, compteurs, `0x403`/`0x425` |
| `MOTEUR_ENABLE_CLOCK_TEST` | `ON` | diagnostic d’horloge GPIO41 |

Ajouter par exemple `-DCLAVIER_DIAG_DEFAULT_LEVEL=2` pour voir directement les
ADC/ohm par canal à l’auto-test.

## 4. Build et tests hôte

```powershell
New-Item -ItemType Directory -Force build\logs | Out-Null
powershell -File tests\host\run_host_tests.ps1 *> build\logs\host.log
cmake --build "build/clavier" *> build\logs\clavier.log
cmake --build "build/moteur" *> build\logs\moteur.log
Select-String -Path build\logs\*.log -Pattern "error:|warning:|FAILED"
```

Artefacts :

- Clavier : `build/clavier/clavier.uf2`
- Moteur : `build/moteur/moteur.elf` (aucun `.uf2` : pas d’USB sur cette carte)

## 5. Flash Clavier (UF2, USB)

1. Maintenir BOOTSEL sur le Pico 2, brancher l’USB, relâcher : un disque
   `RP2350`/`RPI-RP2` apparaît.
2. Copier le fichier :

   ```powershell
   Copy-Item "$ROOT\build\clavier\clavier.uf2" "X:\"
   ```

   (`X:` = lettre du disque BOOTSEL ; la carte redémarre seule après copie.)

## 6. Flash Moteur (SWD, OpenOCD)

24 V coupé ou solénoïdes débranchés ; carte alimentée par son régulateur.

```powershell
$ELF = (Resolve-Path "$ROOT\build\moteur\moteur.elf").Path -replace '\\','/'
& $OCD -s $SCRIPTS -f interface/cmsis-dap.cfg -f target/rp2350.cfg `
  -c "adapter speed 5000" `
  -c "program `"$ELF`" verify reset exit"
$LASTEXITCODE   # 0 attendu
```

Sortie attendue : `** Programming Finished **`, `** Verified OK **`,
`** Resetting Target **`. En cas d’erreurs sporadiques, essayer
`adapter speed 1000` puis `500`. Effacement complet :
`-c "init; flash erase_sector 0 0 last; exit"`.

Répéter le flash sur **chaque carte Moteur** : un mélange de versions provoque
`UNSUPPORTED_VERSION` (7) et `PROFILE_SYNC_FAILED`.

## 7. Vérification après flash

1. Clavier : PuTTY sur CDC, `l 1` (ou `l 2` pour les RAW).
2. Attendre le boot et le scan UID.
3. `m` → table valide, `selftest=repos`, `calib=ON`.
4. `p reset` (profil v3 du fichier compilé) ou auto-test :
   attendre `[PROFILE] sync ok … fmt=3`, sans `PROFILE_FALLBACK_IN_USE`.
5. `t` → auto-test ; si refus, voir `docs/specs/commandes-putty.md` § diagnostic.

## 8. Dépannage court

| Symptôme | Action |
|---|---|
| `No CMAKE_C_COMPILER` pour `picotool` | MinGW absent du `PATH` : nouvelle session ou ajouter `mingw64\bin`. |
| CMakeCache chemin différent | supprimer `build/clavier` ou `build/moteur` et reconfigurer. |
| `No CMSIS-DAP device` | probe non vu, carte non alimentée, GND absent, SWCLK/SWDIO inversés (33/34). |
| `target not found`, `unknown flash` | OpenOCD sans support RP2350 complet : mettre à jour. |
| `UNSUPPORTED_VERSION` après flash | toutes les cartes Moteur ne sont pas au même format ; reflasher toutes les cartes avec l’arbre courant. |
| `PROFILE_FALLBACK_IN_USE` après flash | normal au premier boot : lancer `p reset`/auto-test pour écrire le v3 en NVM. |
