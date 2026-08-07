$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$SdkPath   = (Get-ChildItem "$sdk/sdk" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$Toolchain = (Get-ChildItem "$sdk/toolchain" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$SdkVersion = Split-Path $SdkPath -Leaf
$PicotoolDir = "$sdk/picotool/$SdkVersion/picotool"
$NinjaDir = (Get-ChildItem "$sdk/ninja" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$env:Path = "$NinjaDir;$env:Path"
if (-not (Test-Path "$PicotoolDir/picotoolConfig.cmake")) {
  throw "No Picotool matching Pico SDK $SdkVersion at $PicotoolDir"
}
if ($args -contains "-Clean") { Remove-Item "$root/build" -Recurse -Force -ErrorAction SilentlyContinue }
cmake -G Ninja -B "$root/build" -S "$root" -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DPICO_SDK_PATH="$SdkPath" -DPICO_TOOLCHAIN_PATH="$Toolchain" `
  -DPICO_PLATFORM=rp2350 -Dpicotool_DIR="$PicotoolDir"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build "$root/build"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
