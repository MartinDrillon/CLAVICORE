# Tests hÃ´te (PC) des helpers purs de l'ADR-0003. NÃ©cessite gcc (MinGW-w64/WinLibs) dans le PATH.
# Usage (depuis n'importe oÃ¹) : powershell -File tests\host\run_host_tests.ps1
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

# Console de réglage à chaud (ADR-0015) : bringup_console.c compilé tel quel avec
# des doublures de can_link et usb_log.
$exe2 = Join-Path $out "test_console_tuning.exe"
& gcc -std=c11 -Wall -Wextra -Werror -pedantic `
    -I (Join-Path $root "shared\protocol") -I (Join-Path $root "firmware\clavier\src") `
    (Join-Path $PSScriptRoot "test_console_tuning.c") `
    (Join-Path $root "firmware\clavier\src\bringup_console.c") `
    (Join-Path $root "firmware\clavier\src\profile_tuning.c") `
    (Join-Path $root "shared\protocol\solenoid_profile.c") `
    -o $exe2
if ($LASTEXITCODE -ne 0) { throw "Compilation hote (console) en echec" }

& $exe2
if ($LASTEXITCODE -ne 0) { throw "Tests hote (console) en echec" }
