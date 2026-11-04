# =============================================================================
# Crash Bash: Remaster+ — Script di Setup Ambiente per il Team
# =============================================================================
# Questo script clona/aggiorna i 3 repository di riferimento dentro original/repos/
# e verifica la presenza della ROM originale in original/disc/.

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\..\.."
$ReposDir = Join-Path $RepoRoot "original\repos"
$DiscDir  = Join-Path $RepoRoot "original\disc"

Write-Host "=== Setup Ambiente Crash Bash: Remaster+ ===" -ForegroundColor Cyan

$repos = @(
    @{ Name = "crashbash";       Url = "https://github.com/mateusfavarin/crashbash.git" },
    @{ Name = "CrashBashEx";     Url = "https://github.com/xan1242/CrashBashEx.git" },
    @{ Name = "Crash-Resources"; Url = "https://github.com/Helias/Crash-Bandicoot-Resources.git" }
)

foreach ($r in $repos) {
    $target = Join-Path $ReposDir $r.Name
    if (Test-Path (Join-Path $target ".git")) {
        Write-Host "[OK] Repository '$($r.Name)' gia' presente. Aggiornamento in corso..." -ForegroundColor Green
        git -C $target pull --ff-only
    } else {
        Write-Host "[CLONE] Clonazione di $($r.Url) in original/repos/$($r.Name)..." -ForegroundColor Yellow
        git clone $r.Url $target
    }
}

Write-Host "`n=== Verifica Disco Originale (SCUS-94570) ===" -ForegroundColor Cyan
$binPath = Join-Path $DiscDir "bash.bin"
if (Test-Path $binPath) {
    $hash = (Get-FileHash -Path $binPath -Algorithm SHA1).Hash
    Write-Host "[OK] Trovato original/disc/bash.bin (SHA1: $hash)" -ForegroundColor Green
} else {
    Write-Host "[INFO] File 'original/disc/bash.bin' non ancora presente." -ForegroundColor Yellow
    Write-Host "       Quando sei pronto, copia l'immagine della versione NTSC-U (SCUS-94570) in original/disc/bash.bin"
}

Write-Host "`nSetup completato!" -ForegroundColor Cyan

