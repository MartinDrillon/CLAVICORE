# Moteur Phase 2a — Protocole de bring-up manuel (canaux 0/11, sans CAN)

Couvre `solenoid_driver.c`, `pwm_hal.c`, `clock_test.c` (convention AGENTS.md :
test manuel documenté pour tout chemin touchant aux solénoïdes).

## Build et flash (SWD via Pico Probe / Raspberry Pi Debug Probe)

Légende : **[V]** = vérifié sur ce poste/dépôt ; **[À VÉRIFIER]** = non vérifié, à
confirmer avant de s'y fier.

La carte moteur n'a **ni USB ni UART accessible** (USB_DM/USB_DP du RP2354B non
connectés dans `docs/specs/pinmap.generated_drive.yaml` **[V]**). Il n'y a donc ni
BOOTSEL/UF2 ni console série : on flashe le `.elf` par **SWD** avec un probe
CMSIS-DAP. Le CMake Moteur ne produit **aucun `.uf2`** (pas d'appel à
`pico_add_extra_outputs()`) : seul `moteur.elf` est généré.
Les `printf` ne sont **pas visibles** (pas de stdio) : se fier aux mesures
électriques décrites aux sections 1 à 7.

### A. Avertissements avant le premier flash

- **Sécurité** : le firmware n'active le solénoïde que sur appui du bouton GPIO34
  (`firmware/moteur/src/main.c` **[V]** : au boot, nSLEEP est réveillé, les canaux
  de test 0 et 11 restent en IDLE, PWM à 0 ; chaque appui court alterne 0 puis 11,
  sans avancer sur l'appui long 3 s de self-test). On conseille néanmoins de flasher
  avec l'**alimentation
  24 V coupée** et/ou le solénoïde **débranché**, car :
  (a) la première version flashée n'a jamais tourné sur cette carte ;
  (b) pendant le flash/reset, les GPIO sont flottants/en entrée avant que le firmware
  ne configure les broches : l'état des DRV8874 (PWM/DIR/nSLEEP) pendant ce laps n'est
  **pas garanti par le firmware** ; et le bouton pourrait être actionné par erreur.
  Ne mettre le 24 V qu'après la fin du flash, en suivant la section 0.
- **Flash 2 Mo et boot stage2** (`firmware/moteur/boards/moteur_rp2354b.h`) :

  > **Pour un débutant — deux sujets distincts**
  >
  > **Résolu : la taille de la flash.** `PICO_FLASH_SIZE_BYTES` = 2 Mo est
  > **confirmé par le propriétaire de la carte** (son RP2354B a 2 Mo de flash interne ;
  > pas de datasheet du dépôt pour l'appuyer). Cette valeur sert à l'éditeur de liens
  > (taille de la région flash disponible) et au SDK. Le firmware actuel fait
  > ~34 Ko, très loin de 2 Mo : la taille n'est **pas** un risque pratique.
  >
  > **Reste à vérifier : le « boot stage2 » (ce n'est PAS un problème de taille).**
  > Sur RP2350, l'image flashée commence par un petit bloc de 256 octets qui
  > configure le mode de lecture QSPI de la flash. Le board header choisit
  > `PICO_BOOT_STAGE2_CHOOSE_W25Q080` (valeur par défaut du Pico 2), qui utilise des
  > commandes SPI génériques des flash Winbond/compatibles. Si la flash interne du
  > RP2354B comprend ces commandes (**probable** : standard QSPI très répandu, mais
  > **non confirmé** par une datasheet du dépôt), tout est bon. Sinon, le firmware
  > flashé pourrait ne pas démarrer (« flash OK mais rien ne se passe »). C'est une
  > hypothèse vraisemblable, **non prouvée**.
  >
  > **Comment lever le doute :** un flash OpenOCD avec `verify` OK (section F) suivi
  > de la mesure de GPIO15/GPIO35/GPIO0 (section G) prouve que le boot fonctionne.
  > Si le programme ne démarre pas, regarder cette piste en premier, et demander une
  > ADR/décision de l'Architecte avant de changer le stage2.

### B. Matériel et câblage

Matériel : un Raspberry Pi Debug Probe ou un Pico (« Picoprobe ») flashé avec le
firmware debugprobe, câble USB vers le PC, 3 fils (4 avec RUN).

| Probe | Carte moteur (IC1 = RP2354B) | Remarque |
|---|---|---|
| SWCLK | pin 33, net `/SDCLK` | **[V]** pinmap |
| SWDIO | pin 34, net `/SWDIO` | **[V]** pinmap |
| GND | GND de la carte | masse commune, obligatoire |
| RUN (optionnel) | pin 35, net `/RUN` | permet le reset par le probe |
| 3V3 du probe | **ne pas brancher** | voir ci-dessous |

- **Connecteur de debug** : aucun header de debug n'a été identifié dans les
  fichiers lus (`pinmap.generated_drive.yaml`). Le contenu de
  `Netlist/Solenoide_drive.xml` (refdes, pins d'un éventuel header/points de test)
  **n'a pas pu être exploré : [À VÉRIFIER]**. À défaut, souder/clipser des fils sur
  les points de test ou directement sur les nets `/SDCLK`, `/SWDIO`, `/RUN`, GND.
- **Masse commune** : SWD est référencé à la masse ; sans GND commun, aucune
  communication.
- **Alimentation de la carte** : la carte doit être alimentée pour que le RP2354B
  réponde (le probe ne l'alimente pas). Le MCU est alimenté par le net `3.3VIN`
  (IOVDD, VREG_VIN, etc.) **[V]** pinmap ; **comment `3.3VIN` est produit depuis le 24 V
  n'est pas confirmé : [À VÉRIFIER]** dans le netlist. Donc **« 3,3 V logique seul
  suffit-il, 24 V coupé ? » : [À VÉRIFIER]** avant d'affirmer. Règle prudente :
  alimenter la carte comme prévu (régulateur) mais **solénoïde débranché**, ou
  injecter 3,3 V seulement si le netlist confirme que c'est possible sans backfeed.
  Ne **jamais** relier le 3V3 du probe à `3.3VIN` si la carte a sa propre source.
- Le probe adapte son niveau de signal à 3,3 V : la carte est en 3,3 V (IOVDD = `3.3VIN`) **[V]**.

### C. Outils installés et Installation

- Pico SDK **2.3.1** : `C:\Users\marti\.pico-sdk\sdk\2.3.1` **[V]**.
- `C:\Users\marti\.pico-sdk\openocd` et `...\picotool` : **absents [V]**. L'OpenOCD
  utilisé est celui installé par winget (xPack 0.12.0-7) **[V]** :

```powershell
$OCD_DIR = "C:\Users\marti\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7"
$OCD     = "$OCD_DIR\bin\openocd.exe"
$SCRIPTS = "$OCD_DIR\openocd\scripts"
Test-Path $OCD, "$SCRIPTS\interface\cmsis-dap.cfg", "$SCRIPTS\target\rp2350.cfg"   # 3 x True attendu [V]
& $OCD --version
```

- `interface\cmsis-dap.cfg` et `target\rp2350.cfg` existent dans `$SCRIPTS` **[V]**.
- Si un chemin est absent : `winget install xpack-dev-tools.openocd-xpack`
  (ou extension VS Code « Raspberry Pi Pico », qui télécharge OpenOCD dans
  `C:\Users\marti\.pico-sdk\openocd`) **[À VÉRIFIER : nom exact du paquet]**.
- **Support RP2350 de cet OpenOCD [À VÉRIFIER]** : la présence de `rp2350.cfg` ne
  garantit pas que le pilote flash RP2350 est complet. Au premier essai, une erreur
  du type « unknown flash » ou « target not found » indique qu'il faut un OpenOCD plus
  récent (build Raspberry Pi, ex. via l'extension VS Code Pico).
- `picotool` : non installé et **non proposé** (il ne flashe pas par SWD de
  façon vérifiée ici ; BOOTSEL/USB impossible).

### D. Vérifier que Windows voit le probe

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.FriendlyName -match "CMSIS|Debug Probe|Picoprobe" } | Format-Table Status, Class, FriendlyName, InstanceId
```

- Attendu : une entrée « Debug Probe (CMSIS-DAP) » (VID `2E8A`, PID `000C`) avec
  `Status = OK` **[À VÉRIFIER sur le poste]**. Le driver est en général WinUSB natif,
  sans installation.
- Rien ? Changer de câble USB (câble « data », pas « charge seule »), de port,
  regarder le Gestionnaire de périphériques (« Périphériques inconnus »).
  Un Pico « Picoprobe » doit d'abord avoir reçu le firmware debugprobe.

### E. Build du .elf

```powershell
cd "D:\Documents\MUSIQUE\EPINETTE\Kit Heugel_Bedard"
cmake --build build\moteur                       # clock-test ON (défaut)
# Variante sans clock-test : -DMOTEUR_ENABLE_CLOCK_TEST=OFF (dossier de build séparé)
Test-Path build\moteur\moteur.elf                # True attendu [V : fichier présent]
```

ELF à flasher : `D:\Documents\MUSIQUE\EPINETTE\Kit Heugel_Bedard\build\moteur\moteur.elf`.
Aucun `.uf2` n'est produit pour le Moteur (carte sans USB) : l'ELF est le seul
artefact de flash.

### F. Flash avec OpenOCD

Carte alimentée selon B, **24 V/solénoïde coupé ou débranché**, probe câblé :

```powershell
$ELF = "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/moteur/moteur.elf"
& $OCD -s $SCRIPTS -f interface/cmsis-dap.cfg -f target/rp2350.cfg `
  -c "adapter speed 5000" `
  -c "program `"$ELF`" verify reset exit"
```

Explications :

- `& $OCD` : `&` lance l'exécutable stocké dans la variable (chemins avec espaces).
- `-s $SCRIPTS` : dossier des scripts de configuration d'OpenOCD.
- `-f interface/cmsis-dap.cfg` : adaptateur = probe CMSIS-DAP. Slashs `/` (pas `\`)
  dans les chemins OpenOCD.
- `-f target/rp2350.cfg` : cible RP2350 (famille du RP2354B).
- `-c "adapter speed 5000"` : horloge SWD 5000 kHz (5 MHz). Descendre à `1000` ou
  `500` en cas d'erreurs.
- `program ... verify reset exit` : écrit l'ELF en flash, relit pour comparer
  (`verify`), redémarre la cible (`reset`), quitte OpenOCD (`exit`).
- `` `" `` : guillemets doubles échappés en PowerShell (chemin avec espace dans `$ELF`).

Sortie attendue (extraits, **[À VÉRIFIER]** sur ce poste) :

```text
Info : [rp2350.dap.core0] ... 
** Programming Started **
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```

Code retour : `$LASTEXITCODE` doit valoir `0`.

### G. Vérifier que le firmware tourne

Pas de console. Alimenter le 24 V (section 0), puis mesurer : `GLOBAL_SLEEP`
GPIO15 haut, GPIO35 bas, GPIO0 au niveau PULL (section 1) ; PWM section 2 ;
GPIO41 clock-test section 4.

### H. Dépannage

| Symptôme | Causes / actions |
|---|---|
| `Error: Failed to connect` / `DAP init failed` / `No CMSIS-DAP device` | Probe non vu (section D) ; carte non alimentée ; GND absent ; SWCLK/SWDIO inversés ou sur la mauvaise pin (33/34) ; mauvais contact. |
| Erreurs sporadiques, timeout | `adapter speed 1000` puis `500` ; fils plus courts. |
| `target not found`, `unknown flash`, `rp2350.cfg` non reconnu | OpenOCD sans support RP2350 complet : mettre à jour (section C). |
| Flash OK mais le firmware ne démarre pas (aucune mesure) | Piste à examiner en premier : boot stage2 `W25Q080` non confirmé pour la flash interne du RP2354B (voir A ; la taille 2 Mo, elle, est confirmée et n'est pas en cause). Vérifier aussi l'alimentation, puis reset via RUN. Ne pas changer le stage2 sans ADR/décision de l'Architecte. |
| Cible bloquée | Brancher RUN, ou couper/remettre l'alimentation de la carte (24 V coupé !). |
| Re-flash / effacement | Relancer la commande F (réécrit les secteurs concernés). Effacement complet : `-c "init; flash erase_sector 0 0 last; exit"` **[À VÉRIFIER : syntaxe/banc]**. |
| Reset seul | `& $OCD -s $SCRIPTS -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000" -c "init; reset; exit"` ; ou impulsion de `/RUN` à GND si câblé. |

### I. Option : débogage

Le débogage gdb (OpenOCD en serveur sur le port 3333) est possible mais **non requis**
pour ce protocole.

## 0. Précautions

- Première mise sous tension : alimentation de labo limitée en courant (≈ 3 A),
  solénoïde canal 0 branché ou remplacé par une charge résistive/inductive de test.
- Toutes les valeurs de `solenoid_profile.h` sont des valeurs initiales prudentes,
  à ajuster sur prototype ; noter les valeurs finales retenues dans le dépôt.

## 1. Alimentation et nSLEEP

1. Alimenter la carte (24 V puis 3,3 V logique selon la carte). Aucune sortie
   solénoïde ne doit être active au repos.
2. Mesurer `GLOBAL_SLEEP` (GPIO15, /nSLEEP) : **haut** après le boot (réveil au démarrage
   après init des canaux). Attendu : niveau haut ~3,3 V.
3. `PWM_CHANNEL[0]` (GPIO35) : bas, `DIR_CHANNEL[0]` (GPIO0) : niveau PULL
   (`SOLENOID_DIR_LEVEL_PULL`, 0 par défaut).

## 2. Mesure PWM et courant (note courte)

1. Oscilloscope sur GPIO35, sonde de courant (ou shunt) sur le solénoïde.
2. Appui court (< 1 s) puis relâchement. Le **premier** appui court après boot teste
   le canal 0 (GPIO35/GPIO0), le **deuxième** le canal 11, puis l'alternance reprend
   0, 11, 0... L'appui long 3 s de self-test IPROPI ne fait pas avancer cette
   alternance. Attendu sur GPIO35 quand l'appui cible le canal 0 :
   - PULL : 100 % (niveau haut fixe) pendant ~15 ms ;
   - HOLD : 20 kHz (période 50 µs), duty ~25 % tant que le bouton est maintenu ;
   - au relâchement : duty 0 ; ≥ `SOLENOID_DIR_SAFETY_DELAY_US` (200 µs) plus tard
     GPIO0 change ; puis ~5 ms de RELEASE à 50 % / 20 kHz ; duty 0 ; ≥ 200 µs plus
     tard GPIO0 revient au niveau PULL ; repos ≥ 10 ms.
3. **Critère de sécurité** : sur analyseur logique (GPIO35 + GPIO0), GPIO0 ne
   change JAMAIS pendant que GPIO35 n'est pas bas (ADR-0001).
4. Courant crête PULL et courant HOLD : dans la fenêtre 2–3 A de la bobine ;
   sinon corriger `solenoid_profile.h` (invariant 4).

## 3. Sens du courant

1. Pendant PULL/HOLD, noter le sens du courant dans la bobine (OUT1/OUT2).
2. Pendant RELEASE, le sens doit être inversé. Si l'inversion n'est pas celle
   attendue ou si la bobine s'attire au lieu de se relâcher, permuter
   `SOLENOID_DIR_LEVEL_PULL` / `SOLENOID_DIR_LEVEL_RELEASE`.

## 4. Fréquence (test fréquence)

1. **Porteuse** : GPIO35 en HOLD → **20 kHz** ± 1 % (`SOLENOID_PWM_FREQ_HZ`).
2. **Horloge (clock-test)** : maintenir le bouton GPIO34 pendant **5,0 s** (± 0,1 s,
   +20 ms d'anti-rebond). GPIO41 (/FREQ OSC) → carré 50 % **1 MHz** (période 1 µs).
   Un écart traduit une clk_sys ≠ 150 MHz (trace série : clk_sys, wrap, div — non visible sans stdio).
   Slew lent, 2 mA : fronts volontairement lents.
3. Tout nouvel appui : GPIO41 passe en haute impédance (plus de signal ; pas
   de niveau forcé). Le solénoïde ne doit PAS s'activer sur cet appui ni à son
   relâchement.
4. Pendant que le clock-test est actif, un appui ne déclenche pas le solénoïde.
5. Appui < 5 s : pas d'activation du clock-test.
6. Remarque : l'appui long déclenche d'abord un note_on (le solénoïde sera
   relâché de force à HOLD max 20 s, ou au passage en clock-test).
7. Variante `MOTEUR_ENABLE_CLOCK_TEST=OFF` : l'appui 5 s n'a aucun effet sur GPIO41
   (reste haute impédance) ; le solénoïde fonctionne normalement.

## 5. NFAULT

1. Provoquer un défaut : relier brièvement GPIO39 (/FAULT0) à GND (ou court-circuit
   contrôlé de sortie sur banc approprié) pendant une note tenue.
2. Attendu : PWM coupé immédiatement (GPIO35 bas), état FAULT (trace
   `DEFAUT NFAULT`, non visible sans stdio). Après suppression du défaut, le canal RESTE en FAULT
   (pas de reprise silencieuse) : un appui n'active pas le solénoïde.
3. Reset explicite : NFAULT toujours bas → l'appui est refusé (`FAULT_PERSISTS`) ;
   NFAULT relâché → l'appui effectue le reset (FAULT → DIR_TO_PULL → REST → IDLE)
   sans activer le solénoïde ; l'appui suivant fait un note_on normal.

## 6. HOLD max

1. Maintenir le bouton > 20 s. Attendu : à `SOLENOID_HOLD_MAX_MS` (20000 ms, ADR-0007) le driver
   coupe seul (duty 0, séquence RELEASE) ; trace `HOLD max atteint` (non visible sans stdio). Le
   solénoïde ne se réactive pas avant relâchement + nouvel appui.
2. Durant le maintien, aucune réactivation même bouton toujours enfoncé.

## 7. Cas limites

- Appui très bref (< 20 ms) : ignoré (anti-rebond).
- Appui pendant REST/RELEASE : refusé `BUSY`, aucune activation.
- `GLOBAL_SLEEP` forcé bas pendant une note : coupure PWM + FAULT (SLEEP).
