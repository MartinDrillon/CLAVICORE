# Diagnostic SWD du chemin profil de la carte Moteur (incident PROFILE_SYNC_FAILED).
#
# La carte Moteur n'a ni USB ni UART : ce script lit par SWD (OpenOCD, sonde
# CMSIS-DAP, procédure tests/build_clavier_windows.md §6) l'état du profil et la
# trace du chemin COMMIT, SANS reset ni arrêt (halt) de la cible : avec
# OpenOCD xPack 0.12.0-7 / rp2350.cfg, `resume` après `halt` échoue et laisse le
# cœur 0 arrêté (minuterie figée, solénoïdes non pilotés). Ne pas ajouter de halt.
#
#   .\tests\tools\moteur_swd_profile.ps1                       # lecture seule
#   .\tests\tools\moteur_swd_profile.ps1 -InjectPullUs 11000   # rejoue un COMMIT
#
# -InjectPullUs : injecte dans le rx_ring du Moteur la séquence BEGIN/CHUNK/COMMIT
# du profil de firmware/clavier/src/solenoid_profile_config.h avec
# pull_duration_us remplacé, puis lit la trace 3 s plus tard. Aucune activation
# de solénoïde ; une écriture flash (un secteur) si le tag diffère du profil
# actif. Le PROFILE_STATUS émis sur le bus porte -TransferId : le Clavier l'ignore
# (hors synchronisation). Remettre ensuite le profil du Clavier (`p send`, ou
# -InjectPullUs avec la valeur du fichier) pour éviter PROFILE_DIVERGENT au boot.
param(
    [string]$Elf = "$PSScriptRoot\..\..\build\moteur\moteur.elf",
    [int]$InjectPullUs = 0,
    [int]$TransferId = 200,
    [int]$AdapterKhz = 1000,
    [string]$OcdDir = "C:\Users\marti\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7",
    [string]$ToolchainBin = "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$tmp = Join-Path $env:TEMP 'clavicore_swd'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# --- Adresses depuis l'ELF (l'ELF doit être celui flashé sur la carte) -------
$want = 'g_profile_commit_trace', 'stats', 'rx_ring', 'rx_head', 'rx_tail', 'g_source', 'g_tag',
        'g_seq', 'g_slot', 'cbus', 'board_uid', 'profile_status_pending'
$sym = @{}
foreach ($line in (& "$ToolchainBin/arm-none-eabi-nm.exe" $Elf)) {
    $p = $line -split '\s+'
    if ($p.Count -eq 3 -and $want -contains $p[2]) {
        if ($sym.ContainsKey($p[2]) -and $sym[$p[2]] -ne "0x$($p[0])") {
            throw "Symbole '$($p[2])' ambigu dans $Elf"
        }
        $sym[$p[2]] = "0x$($p[0])"
    }
}
foreach ($n in $want) {
    if (-not $sym.ContainsKey($n)) { throw "Symbole '$n' absent de $Elf (ELF trop ancien ?)" }
}

$inject = ''
if ($InjectPullUs -gt 0) {
    $gen = Join-Path $tmp 'gen_profile_inject.exe'
    & gcc -std=c11 -Wall -Wextra -Werror -I "$repo/shared/protocol" -I "$repo/firmware/clavier/src" `
        "$repo/tests/tools/gen_profile_inject.c" "$repo/shared/protocol/solenoid_profile.c" `
        "$repo/shared/protocol/can_protocol.c" -o $gen
    if ($LASTEXITCODE -ne 0) { throw 'Compilation de gen_profile_inject échouée' }
    $frames = & $gen $InjectPullUs $TransferId
    if ($LASTEXITCODE -ne 0) { throw 'gen_profile_inject a échoué' }
    $count = @($frames).Count
    # Écrire en RAM avec les adresses d'un autre ELF corromprait la cible : on
    # compare d'abord 4 mots de code (lus en XIP, sans arrêter la cible) avec l'ELF.
    $poll = ((& "$ToolchainBin/arm-none-eabi-nm.exe" $Elf) | Where-Object { $_ -match ' T can_link_poll$' }) -split '\s+' | Select-Object -First 1
    $bin = Join-Path $tmp 'moteur.bin'
    & "$ToolchainBin/arm-none-eabi-objcopy.exe" -O binary $Elf $bin
    $bytes = [System.IO.File]::ReadAllBytes($bin)
    $off = [Convert]::ToInt64($poll, 16) - 0x10000000
    $checks = for ($w = 0; $w -lt 4; $w++) {
        "if {[lindex `$code $w] != 0x{0:X8}} {{ echo `"ABANDON: firmware flashe different de l'ELF`"; shutdown error }}" -f [BitConverter]::ToUInt32($bytes, [int]($off + 4 * $w))
    }
    # Sans halt : la reprise après halt échoue avec cette config OpenOCD
    # (cœur 0 laissé arrêté, minuterie figée). Les trames sont écrites, puis
    # rx_head en dernier ; on abandonne si une trame CAN arrive entre-temps.
    $inject = @"
set code [read_memory 0x$poll 32 4]
$($checks -join "`n")
proc inj {i id w0 w1} {
  global h
  set a [expr {$($sym.rx_ring) + ((`$h + `$i) & 15) * 20}]
  mww `$a 0x00100000
  mww [expr {`$a + 4}] `$id
  mww [expr {`$a + 8}] 8
  mww [expr {`$a + 12}] `$w0
  mww [expr {`$a + 16}] `$w1
}
set h [lindex [read_memory $($sym.rx_head) 32 1] 0]
set t [lindex [read_memory $($sym.rx_tail) 32 1] 0]
if {`$h != `$t} {
  echo "ABANDON: rx_ring non vide (head=`$h tail=`$t)"
  shutdown error
}
$($frames -join "`n")
if {[lindex [read_memory $($sym.rx_head) 32 1] 0] != `$h} {
  echo "ABANDON: trame CAN recue pendant l'injection, relancer"
  shutdown error
}
mww $($sym.rx_head) [expr {`$h + $count}]
echo "INJECTION: $count trames, transfer_id=$TransferId, pull_duration_us=$InjectPullUs"
sleep 3000
"@
}

$tcl = @"
init
$inject
proc show {label addr width count} {
  echo "`$label [read_memory `$addr `$width `$count]"
}
set names {rx_overrun rx_ext_or_rtr rx_bad_dlc rx_bad_value rx_unknown_id rx_ignored can_errors tx_fail announces_sent requests_served triggers_sent raw_reports_sent raw_reports_deferred ohm_reports_sent ohm_reports_deferred solenoid_commands_ok solenoid_ignored_tag solenoid_bad_channel solenoid_unoccupied solenoid_rejected solenoid_selftest_blocked profile_begins profile_chunks profile_commits_ok profile_commits_rejected profile_status_sent profile_bad_sequence}
set vals [read_memory $($sym.stats) 32 27]
set out "stats:"
foreach n `$names v `$vals { if {`$v != 0} { append out " `$n=[expr {`$v}]" } }
echo `$out
set tn {magic stage transfer_id result commit_us write_us status_sent_us status_retries commits_seen}
set tv [read_memory $($sym.g_profile_commit_trace) 32 9]
set out "trace:"
foreach n `$tn v `$tv { append out " `$n=[expr {`$v}]" }
echo `$out
show "board_uid" $($sym.board_uid) 8 8
show "g_source(0=DEFAULTS,1=NVM)" $($sym.g_source) 8 1
show "g_tag" $($sym.g_tag) 32 1
show "g_seq" $($sym.g_seq) 32 1
show "g_slot" $($sym.g_slot) 8 1
show "profile_status_pending" $($sym.profile_status_pending) 8 1
show "can2040 rx_total/tx_total/tx_attempt/parse_error" [expr {$($sym.cbus) + 20}] 32 4
show "slotA" 0x101FE000 32 5
show "slotB" 0x101FF000 32 5
show "uptime_us" 0x400b0028 32 1
show "chip_reset(0x10000=POR)" 0x4010002c 32 1
shutdown
"@
$script = Join-Path $tmp 'moteur_swd_profile.tcl'
Set-Content -Encoding ascii -Path $script -Value $tcl
# OpenOCD écrit sur stderr : ne pas le traiter comme une exception PowerShell.
$ErrorActionPreference = 'Continue'
& "$OcdDir\bin\openocd.exe" -s "$OcdDir\openocd\scripts" -f interface/cmsis-dap.cfg -f target/rp2350.cfg `
    -c "adapter speed $AdapterKhz" -f $script 2>&1 |
    ForEach-Object { "$_" } |
    Where-Object { $_ -match '^(stats|trace|board_uid|g_|profile_|can2040|slot|uptime|chip_reset|pc=|INJECTION|ABANDON|Error)' }
