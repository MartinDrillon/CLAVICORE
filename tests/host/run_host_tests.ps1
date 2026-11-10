# Tests hôte (PC) des helpers purs de l'ADR-0003. Nécessite gcc (MinGW-w64/WinLibs) dans le PATH.
# Usage (depuis n'importe où) : powershell -File tests\host\run_host_tests.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$out = Join-Path $root "build\host_tests"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$exe = Join-Path $out "test_phase3_helpers.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") -I (Join-Path $root "firmware\clavier\src") `
    -I (Join-Path $root "firmware\moteur\src") `
    (Join-Path $PSScriptRoot "test_phase3_helpers.c") `
    (Join-Path $root "shared\protocol\can_protocol.c") `
    (Join-Path $root "shared\protocol\commissioning_table.c") `
    (Join-Path $root "shared\protocol\solenoid_profile.c") `
    (Join-Path $root "firmware\clavier\src\diag_log.c") `
    (Join-Path $root "firmware\clavier\src\profile_tuning.c") `
    (Join-Path $root "firmware\moteur\src\ipropi_ohm.c") `
    -o $exe
if ($LASTEXITCODE -ne 0) { throw "Compilation hote en echec" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "Tests hote en echec" }

# Console de r�glage � chaud (ADR-0015) : bringup_console.c compil� tel quel avec
# des doublures de can_link et usb_log.
$exe2 = Join-Path $out "test_console_tuning.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") -I (Join-Path $root "firmware\clavier\src") `
    (Join-Path $PSScriptRoot "test_console_tuning.c") `
    (Join-Path $root "firmware\clavier\src\bringup_console.c") `
    (Join-Path $root "firmware\clavier\src\profile_tuning.c") `
    (Join-Path $root "firmware\clavier\src\rec_mode.c") `
    (Join-Path $root "shared\protocol\solenoid_profile.c") `
    (Join-Path $root "shared\protocol\commissioning_table.c") `
    (Join-Path $root "shared\protocol\can_protocol.c") `
    (Join-Path $root "shared\protocol\listen_params.c") `
    -o $exe2
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (console) en echec" }

& $exe2
if ($LASTEXITCODE -ne 0) { throw "Tests hote (console) en echec" }

# R�gles pures de synchronisation du profil (ADR-0023).
$exe3 = Join-Path $out "test_profile_sync.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") -I (Join-Path $root "firmware\clavier\src") `
    (Join-Path $PSScriptRoot "test_profile_sync.c") `
    -o $exe3
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (profile sync) en echec" }

& $exe3
if ($LASTEXITCODE -ne 0) { throw "Tests hote (profile sync) en echec" }

# Pilote r�el de r�veil (ADR-0024), avec GPIO/PWM simul�s.
$exe4 = Join-Path $out "test_solenoid_wake.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $PSScriptRoot "stubs") `
    -I (Join-Path $root "shared\protocol") -I (Join-Path $root "firmware\moteur\src") `
    (Join-Path $PSScriptRoot "test_solenoid_wake.c") `
    -o $exe4
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (wake) en echec" }

& $exe4
if ($LASTEXITCODE -ne 0) { throw "Tests hote (wake) en echec" }

# Point d'�mission journal ADR-0025 avec horloge Pico simul�e.
$exe5 = Join-Path $out "test_diag_out.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $PSScriptRoot "stubs") `
    -I (Join-Path $root "firmware\clavier\src") `
    (Join-Path $PSScriptRoot "test_diag_out.c") `
    (Join-Path $root "firmware\clavier\src\diag_out.c") `
    -o $exe5
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (diag_out) en echec" }

& $exe5
if ($LASTEXITCODE -ne 0) { throw "Tests hote (diag_out) en echec" }

# Resolution d'alteration a deux modificateurs maintenus (ADR-0026).
$exe6 = Join-Path $out "test_keycode_alteration.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "firmware\clavier\src") `
    (Join-Path $PSScriptRoot "test_keycode_alteration.c") `
    (Join-Path $root "firmware\clavier\src\keycode.c") `
    (Join-Path $root "firmware\clavier\src\keymap.c") `
    (Join-Path $root "firmware\clavier\src\note_state.c") `
    -o $exe6
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (alteration) en echec" }

& $exe6
if ($LASTEXITCODE -ne 0) { throw "Tests hote (alteration) en echec" }

# Moteur pur des combos de positions (ADR-0031), horloge factice.
$exe7 = Join-Path $out "test_combo.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "firmware\clavier\src") `
    (Join-Path $PSScriptRoot "test_combo.c") `
    (Join-Path $root "firmware\clavier\src\combo.c") `
    (Join-Path $root "firmware\clavier\src\keymap.c") `
    -o $exe7
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (combo) en echec" }

& $exe7
if ($LASTEXITCODE -ne 0) { throw "Tests hote (combo) en echec" }

# Protocole d'écoute ADR-0032 + paramètres canoniques.
$exe8 = Join-Path $out "test_listen_protocol.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") `
    (Join-Path $PSScriptRoot "test_listen_protocol.c") `
    (Join-Path $root "shared\protocol\can_protocol.c") `
    (Join-Path $root "shared\protocol\listen_params.c") `
    -o $exe8
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (listen protocol) en echec" }
& $exe8
if ($LASTEXITCODE -ne 0) { throw "Tests hote (listen protocol) en echec" }

# Detecteur d'impulsion IPROPI (ADR-0032 Lot 2), module pur sans SDK.
$exe9 = Join-Path $out "test_pulse_detector.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") -I (Join-Path $root "firmware\moteur\src") `
    (Join-Path $PSScriptRoot "test_pulse_detector.c") `
    (Join-Path $root "firmware\moteur\src\pulse_detector.c") `
    -o $exe9
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (pulse detector) en echec" }
& $exe9
if ($LASTEXITCODE -ne 0) { throw "Tests hote (pulse detector) en echec" }

# Anneau de bruit 1 ms x 256 par canal (ADR-0032 Lot 2), module pur sans SDK.
$exe10 = Join-Path $out "test_listen_noise_ring.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "firmware\moteur\src") `
    (Join-Path $PSScriptRoot "test_listen_noise_ring.c") `
    (Join-Path $root "firmware\moteur\src\listen_noise_ring.c") `
    -o $exe10
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (noise ring) en echec" }
& $exe10
if ($LASTEXITCODE -ne 0) { throw "Tests hote (noise ring) en echec" }

# Mode REC Clavier (ADR-0032 Lot 3a) : alternance, fusion, voisins, velocite.
$exe11 = Join-Path $out "test_rec_mode.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") -I (Join-Path $root "firmware\clavier\src") `
    (Join-Path $PSScriptRoot "test_rec_mode.c") `
    (Join-Path $root "firmware\clavier\src\rec_mode.c") `
    (Join-Path $root "shared\protocol\commissioning_table.c") `
    (Join-Path $root "shared\protocol\can_protocol.c") `
    (Join-Path $root "shared\protocol\listen_params.c") `
    -o $exe11
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (rec mode) en echec" }
& $exe11
if ($LASTEXITCODE -ne 0) { throw "Tests hote (rec mode) en echec" }

# Relance de l'ecoute ADR-0032 section 3 (e trace / e stream en REC) : politiques
# pures Clavier (jeu deja envoye) et Moteur (CRC deja applique, carte non ciblee).
$exe12 = Join-Path $out "test_listen_control_logic.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") `
    -I (Join-Path $root "firmware\clavier\src") -I (Join-Path $root "firmware\moteur\src") `
    (Join-Path $PSScriptRoot "test_listen_control_logic.c") `
    -o $exe12
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (listen control logic) en echec" }
& $exe12
if ($LASTEXITCODE -ne 0) { throw "Tests hote (listen control logic) en echec" }
