# Prereq: SDL2 must be installed in MSYS2: pacman -S mingw-w64-x86_64-SDL2
param(
    [ValidateSet("fw2", "og")]
    [string]$Board = "fw2"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$gcc  = "C:/msys64/mingw64/bin/gcc.exe"
$gxx  = "C:/msys64/mingw64/bin/g++.exe"
# One CMake cache cannot hold both boards -- same constraint as the device
# builds (build/ vs build-og/) -- so each board gets its own binary dir.
$buildDir = if ($Board -eq "og") { "$root/build-sim-og" } else { "$root/build-sim" }
cmake -G Ninja -B "$buildDir" -S "$root/src/sim" `
  -DCMAKE_C_COMPILER="$gcc" -DCMAKE_CXX_COMPILER="$gxx" `
  -DCMAKE_PREFIX_PATH="C:/msys64/mingw64" `
  -DWILIDORO_BOARD="$Board"
cmake --build "$buildDir"
& "$buildDir/wilidoro_sim.exe"
