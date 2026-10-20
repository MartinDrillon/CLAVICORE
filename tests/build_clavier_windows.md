# Procédure de build — firmware Clavier (Windows)

Note de procédure à l'usage des agents (Codeur, Validateur) et des humains du
projet pour générer `build/clavier/clavier.uf2` sur un poste Windows.

Statut : procédure validée — build réussi sans warning/erreur, artefact
`build/clavier/clavier.uf2` généré (59 392 octets).

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
