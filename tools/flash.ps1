$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$sdk  = "$env:USERPROFILE/.pico-sdk"
$OpenOcd = (Get-ChildItem "$sdk/openocd" -Recurse -Filter "openocd.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$Scripts = Join-Path (Split-Path (Split-Path $OpenOcd -Parent) -Parent) "scripts"
Get-Process openocd -ErrorAction SilentlyContinue | Stop-Process -Force
& $OpenOcd -s $Scripts -f "interface/cmsis-dap.cfg" -c "adapter speed 5000" `
  -f "target/rp2350.cfg" -c "program {$root/build/wilidoro.elf} verify reset exit"
