# Flash the FreeWili OG. ONLY the main app may be flashed.
#
# A display app's UF2 links at 0x10021000 and a raw copy writes only that --
# not the app metadata sector at 0x10020000, which only the serial update path
# writes. The bootloader's app_valid check then fails against stale metadata,
# the app never runs, and the display CPU (the one CPU with no BOOTSEL button)
# stops enumerating. The main app carries the display image over the link WITH
# its metadata, which is why this is the only supported route.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$app  = if ($args.Count -ge 1) { $args[0] } else { "wilidoro_main" }

if ($app -like "*_display*") {
    Write-Error @"
Refusing to flash '$app'.

Flashing a display application by UF2 takes the display CPU off USB and it has
no BOOTSEL button. Flash the main app instead -- it pushes the display image
over the inter-CPU link, with the metadata sector the bootloader needs:

    powershell -File tools/flash_og.ps1 wilidoro_main
"@
    exit 1
}

$uf2 = "$root/build-og/$app.uf2"
if (-not (Test-Path $uf2)) { Write-Error "not built: $uf2"; exit 1 }

# Reboot the MAIN CPU into BOOTSEL by opening its USB CDC port at 1200 baud.
# Identifying by CPU means only one RPI-RP2 volume ever appears -- both RP2040s
# present indistinguishable volumes, so two at once cannot be told apart.
python "$root/wiliOGbsp/tools/fw.py" bootsel --cpu main

$deadline = (Get-Date).AddSeconds(30)
do {
    Start-Sleep -Milliseconds 500
    $vols = @(Get-Volume | Where-Object { $_.DriveType -eq 'Removable' -and $_.DriveLetter } |
              Where-Object { Test-Path "$($_.DriveLetter):\INFO_UF2.TXT" })
} while ($vols.Count -eq 0 -and (Get-Date) -lt $deadline)

if ($vols.Count -eq 0) { Write-Error "no RPI-RP2 volume appeared within 30 s"; exit 1 }
if ($vols.Count -gt 1) {
    Write-Error "multiple RPI-RP2 volumes ($($vols.DriveLetter -join ', ')). Both RP2040s look identical; put only one in BOOTSEL."
    exit 1
}

Copy-Item $uf2 "$($vols[0].DriveLetter):\" -Force
Write-Host "flashed $app to $($vols[0].DriveLetter): -- the board reboots and pushes the display image"
