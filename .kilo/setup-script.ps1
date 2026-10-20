# Setup Agent Manager — prépare un worktree Kilo pour ce projet.
# Lancé depuis le worktree, avec WORKTREE_PATH et REPO_PATH fournis par Agent Manager.

$ErrorActionPreference = 'Stop'

git submodule update --init --recursive

if (-not $env:PICO_SDK_PATH) {
    Write-Host "PICO_SDK_PATH non défini. Renseigne-le (voir tests/build_clavier_windows.md : C:/Users/marti/.pico-sdk/sdk/2.3.1) avant de configurer CMake."
}

$worktree = if ($env:WORKTREE_PATH) { (Resolve-Path -LiteralPath $env:WORKTREE_PATH).Path } else { $null }
$repo = if ($env:REPO_PATH) { (Resolve-Path -LiteralPath $env:REPO_PATH).Path } else { $null }

if ($worktree -and $repo -and $worktree -ne $repo) {
    Write-Host "Worktree détecté : les caches CMake de build/ contiennent des chemins absolus. Reconfigure dans un dossier de build propre (ex. build/clavier-worktree) au lieu de réutiliser build/."
}
