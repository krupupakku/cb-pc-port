# =============================================================================
# Crash Bash: Remaster+ — Script di Build Automatica (CMake + LLVM-MinGW)
# =============================================================================

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\..\.."

# Trova CMake e LLVM-MinGW installati tramite winget se non sono già nel PATH
$cmakePkg = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Kitware.CMake*" -Recurse -Filter "cmake.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
$llvmPkg  = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW*" -Recurse -Filter "mingw32-make.exe" -ErrorAction SilentlyContinue | Select-Object -First 1

if ($cmakePkg) { $env:PATH = "$($cmakePkg.DirectoryName);$env:PATH" }
if ($llvmPkg)  { $env:PATH = "$($llvmPkg.DirectoryName);$env:PATH" }

$BuildDir = Join-Path $RepoRoot "build"
$SrcDir   = Join-Path $RepoRoot "remaster"

Write-Host "=== Configurazione CMake ===" -ForegroundColor Cyan
cmake -G "MinGW Makefiles" -S $SrcDir -B $BuildDir

Write-Host "`n=== Compilazione Eseguibili (cb_extract, cb_model2obj, crashbash_remaster) ===" -ForegroundColor Cyan
cmake --build $BuildDir --parallel

Write-Host "`n[OK] Build completata in: $BuildDir" -ForegroundColor Green
Get-ChildItem $BuildDir -Filter "*.exe" | Select-Object Name, Length, LastWriteTime

