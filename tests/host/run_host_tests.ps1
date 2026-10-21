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
    (Join-Path $root "firmware\clavier\src\diag_log.c") `
    (Join-Path $root "firmware\moteur\src\ipropi_ohm.c") `
    -o $exe
if ($LASTEXITCODE -ne 0) { throw "Compilation hote en echec" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "Tests hote en echec" }
