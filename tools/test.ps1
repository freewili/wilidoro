$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$gcc  = (Get-Command gcc -ErrorAction Stop).Source
$sdk  = "$env:USERPROFILE/.pico-sdk"
$NinjaDir = (Get-ChildItem "$sdk/ninja" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$env:Path = "$NinjaDir;$env:Path"
cmake -G Ninja -B "$root/build-tests" -S "$root/tests" -DCMAKE_C_COMPILER="$gcc"
cmake --build "$root/build-tests"
ctest --test-dir "$root/build-tests" --output-on-failure
