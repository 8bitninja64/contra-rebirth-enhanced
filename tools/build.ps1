<#
.SYNOPSIS
  One-command Windows build of Contra ReBirth Enhanced XD.

.DESCRIPTION
  1. (optional) unpacks your WAD into .\game with tools\prepare_wad.py
  2. generates src\gen\*.cpp from game\main.dol
  3. configures and builds src\build-win with MSYS2 MinGW-w64 (gcc, posix threads) + SDL2
  4. copies contrarebirth.exe and SDL2.dll into .\dist (the game folder is NOT copied)

.PARAMETER Wad     Path to your Contra ReBirth (USA) WiiWare WAD. Only needed the first time.
.PARAMETER Msys    MSYS2 root. Default: $env:MSYS2_ROOT, else ..\..\_Tools\msys64, else C:\msys64.
.PARAMETER Jobs    Parallel compile jobs (each generated file needs ~1.5 GB RAM). Default 6.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Wad "D:\wads\Contra ReBirth (USA) (WiiWare).wad"

  MSYS2 packages: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-ninja
  Python 3.11+ with: pip install libWiiPy
#>
param(
    [string]$Wad,
    [string]$Msys,
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Msys) {
    foreach ($c in @($env:MSYS2_ROOT, (Join-Path $root '..\..\_Tools\msys64'), 'C:\msys64')) {
        if ($c -and (Test-Path (Join-Path $c 'mingw64\bin\g++.exe'))) { $Msys = (Resolve-Path $c).Path; break }
    }
}
if (-not $Msys) { throw 'MSYS2 with mingw-w64-x86_64-gcc not found. Pass -Msys <root> or set MSYS2_ROOT.' }
$mingw = Join-Path $Msys 'mingw64'
foreach ($t in 'g++.exe', 'windres.exe', 'ninja.exe', 'SDL2.dll') {
    if (-not (Test-Path (Join-Path $mingw "bin\$t"))) {
        throw "$t missing in $mingw\bin. Run in MSYS2: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-ninja"
    }
}
$env:PATH = "$mingw\bin;$env:PATH"

$game = Join-Path $root 'game'
if ($Wad) {
    Write-Host "== Unpacking $Wad"
    python (Join-Path $root 'tools\prepare_wad.py') $Wad $game
    if ($LASTEXITCODE) { throw 'prepare_wad.py failed' }
}
if (-not (Test-Path (Join-Path $game 'main.dol'))) { throw "No game\main.dol. Run once with -Wad <your WAD>." }

Write-Host '== Generating C++ from main.dol'
Push-Location (Join-Path $root 'src')
try {
    python recomp.py ..\game\main.dol ..\config\symbols.txt gen 300
    if ($LASTEXITCODE) { throw 'recomp.py failed' }

    Write-Host '== Configuring'
    $sdl = $mingw -replace '\\', '/'
    cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=g++ `
          -DCMAKE_RC_COMPILER=windres "-DSDL2_DIR=$sdl"
    if ($LASTEXITCODE) { throw 'cmake configure failed' }

    Write-Host "== Building ($Jobs jobs)"
    ninja -C build-win "-j$Jobs"
    if ($LASTEXITCODE) { throw 'build failed' }
} finally { Pop-Location }

$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $root 'src\build-win\contrarebirth.exe') $dist -Force
Copy-Item (Join-Path $mingw 'bin\SDL2.dll') $dist -Force
Copy-Item (Join-Path $root 'thirdparty\gamecontrollerdb.txt') $dist -Force   # mappings for pads SDL does not know
Write-Host "== Done: $dist\contrarebirth.exe"
Write-Host "   Run it from the repo (it finds ..\game), or put a 'game' folder next to the exe."
