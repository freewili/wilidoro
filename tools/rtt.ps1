$ErrorActionPreference = "Stop"
$sdk = "$env:USERPROFILE/.pico-sdk"
$OpenOcd = (Get-ChildItem "$sdk/openocd" -Recurse -Filter "openocd.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$Scripts = Join-Path (Split-Path (Split-Path $OpenOcd -Parent) -Parent) "scripts"
& $OpenOcd -s $Scripts -f "interface/cmsis-dap.cfg" -c "adapter speed 5000" -f "target/rp2350.cfg" `
  -c "init" -c "rtt setup 0x20000000 0x82000 `"SEGGER RTT`"" -c "rtt start" -c "rtt server start 9090 0" &
Start-Sleep 1; $c = New-Object System.Net.Sockets.TcpClient("localhost",9090)
$s = $c.GetStream(); $b = New-Object byte[] 4096
while ($true) { $n = $s.Read($b,0,4096); if ($n -gt 0) { [Console]::Write([Text.Encoding]::ASCII.GetString($b,0,$n)) } }
