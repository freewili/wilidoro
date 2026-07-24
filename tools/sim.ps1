# Prereq: SDL2 must be installed in MSYS2: pacman -S mingw-w64-x86_64-SDL2
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$gcc  = "C:/msys64/mingw64/bin/gcc.exe"
$gxx  = "C:/msys64/mingw64/bin/g++.exe"
cmake -G Ninja -B "$root/build-sim" -S "$root/src/sim" `
  -DCMAKE_C_COMPILER="$gcc" -DCMAKE_CXX_COMPILER="$gxx" `
  -DCMAKE_PREFIX_PATH="C:/msys64/mingw64"
cmake --build "$root/build-sim"
& "$root/build-sim/wilidoro_sim.exe"
