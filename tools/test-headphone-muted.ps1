# SPDX-License-Identifier: GPL-2.0-only
# Bounded Windows playback test with RT5682 hardware mutes asserted throughout.
param([switch]$Player, [switch]$Audition)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$wav = Join-Path $root 'headphone-test.wav'
if ($Player) {
 $p = [System.Media.SoundPlayer]::new($wav)
 try { $p.PlaySync() } finally { $p.Dispose() }
 exit 0
}
$parent = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$task = 'SofHeadphoneMutedTest'
function Assert-AmpOff {
 $p = Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\MX98360A\0\Device Parameters'
 if ($p.SofAmpStage -ne 3 -or $p.SofAmpStatus -ne 0 -or $p.SofAmpOffWrites -lt 1) { throw 'Speaker shutdown request not verified' }
}
function Read-Transport {
 $paths = @([SofProbeClient]::Paths())
 if ($paths.Count -ne 1) { throw 'Expected one transport interface' }
 $v = [SofProbeClient]::Exchange($paths[0],0x22605C,$null)
 if ($v.Length -lt 26 -or $v[0] -ne 8 -or $v[24] -ne 1 -or $v[25] -ne 1) { throw 'Expected headphone-only transport with speaker hold' }
 return ,$v
}
$failure = $null
Start-Transcript -Path (Join-Path $root $(if ($Audition) {'audition-test.log'} else {'muted-test.log'})) -Force | Out-Null
try {
 . (Join-Path $root 'read-live-snapshot.ps1') -AcxStatus | Out-Host
 Assert-AmpOff
 & (Join-Path $root 'read-headphone-codec.ps1') | Format-List
 $before = Read-Transport
 # Muted default: silent 3-second stream. Explicit audition: 8-second tone,
 # with only a kernel-bounded 3-second physical output window.
 $dataBytes = if ($Audition) {1536000} else {576000}
 $stream = [IO.File]::Create($wav)
 try {
  $w = [IO.BinaryWriter]::new($stream)
  $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([uint32](36+$dataBytes))
  $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $w.Write([uint32]16)
  $w.Write([uint16]1); $w.Write([uint16]2); $w.Write([uint32]48000); $w.Write([uint32]192000)
  $w.Write([uint16]4); $w.Write([uint16]16); $w.Write([Text.Encoding]::ASCII.GetBytes('data'))
  $w.Write([uint32]$dataBytes)
  if ($Audition) {
   for ($i=0;$i -lt $dataBytes/4;$i++) {
    $sample=[int16]([Math]::Round([Math]::Sin(2*[Math]::PI*1000*$i/48000)*16384))
    $w.Write($sample);$w.Write($sample)
   }
  } else { $w.Write([byte[]]::new($dataBytes)) }
  $w.Flush()
 } finally { $stream.Dispose() }
 Start-Service AudioEndpointBuilder
 Start-Service Audiosrv
 $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Player')
 $principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\dan" -LogonType Interactive -RunLevel Highest
 $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 20) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Force | Out-Null
 $started = Get-Date
 Start-ScheduledTask -TaskName $task
 if ($Audition) {
  $ready = $false
  for ($i=0;$i -lt 40;$i++) {
   Start-Sleep -Milliseconds 100
   $live = Read-Transport
   if ($live[13] -eq 1 -and $live[2] -gt $before[2] -and $live[4]-$before[4] -ge 50) { $ready=$true;break }
  }
  if (!$ready) { throw 'No active headphone stream; refusing audition' }
  Assert-AmpOff
  $pre = & (Join-Path $root 'read-headphone-codec.ps1')
  if ($pre.Prepared -ne 1) { throw 'Codec is not prepared; refusing audition' }
  Write-Host 'Starting fixed low-gain headphone audition; automatic mute and power-down follows.'
  & (Join-Path $root 'read-headphone-codec.ps1') -Audition | Format-List
  Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\10EC5682\0\Device Parameters' | Select-Object CodecAuditionStage,CodecAuditionStatus | Format-List
 }
 $finished = $false
 for ($i=0;$i -lt 30;$i++) {
  Start-Sleep -Seconds 1
  $info = Get-ScheduledTaskInfo -TaskName $task
  $state = (Get-ScheduledTask -TaskName $task).State
  if ($info.LastRunTime -ge $started.AddSeconds(-1) -and $state -ne 'Running' -and $info.LastTaskResult -ne 267009) { $finished=$true; break }
 }
 if (!$finished -or $info.LastTaskResult -ne 0) { throw "Player did not finish successfully: $($info.LastTaskResult), state $state" }
 $after = Read-Transport
 $after | ConvertTo-Json -Compress | Write-Host
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'read-live-snapshot.ps1') -AcxStatus
 if ($after[12] -ne 0) { Get-ItemProperty -LiteralPath "Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\$parent\Device Parameters" | Select-Object SofPlayBootResult,SofPlayIpcCommand,SofPlayIpcComponent,SofPlayIpcStatus,SofPlayIpcResult,SofPlayIpcReplyError | Format-List }
 $codec = & (Join-Path $root 'read-headphone-codec.ps1')
 $codec | Format-List
 Assert-AmpOff
 if ($after[13] -ne 1 -or $after[11] -ne 6 -or $after[12] -ne 0 -or $after[2] -lt 1 -or $after[3]-$before[3] -lt 250 -or $after[4]-$before[4] -lt 250 -or (!$Audition -and $codec.Prepared -ne 1) -or ($Audition -and $codec.Prepared -ne 0)) { throw 'Muted transport progress not verified' }
 Write-Host 'PASS: headphone test and packet progress completed; audible confirmation is separate.'
} catch {
 $failure = $_
 Write-Host "FAILED: $_"
 Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\10EC5682\0\Device Parameters' | Select-Object CodecAuditionStage,CodecAuditionStatus | Format-List
} finally {
 Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
 Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
 Stop-Service Audiosrv -Force -ErrorAction Continue
 Stop-Service AudioEndpointBuilder -Force -ErrorAction Continue
 try {
  & (Join-Path $root 'read-headphone-codec.ps1') -Stop | Format-List
  # Device restart only: D0Exit stops DSP and D0Entry returns to dormant state.
  pnputil.exe /restart-device $parent
  if ($LASTEXITCODE -ne 0) { throw 'Parent device restart failed' }
  $idle = Read-Transport
  $path = @([SofProbeClient]::Paths())[0]
  $dma = [SofProbeClient]::Exchange($path,0x226010,$null)
  $dsp = [SofProbeClient]::Exchange($path,0x226000,$null)
  if ($idle[13] -ne 0 -or ($dma[0] -band 2) -ne 0 -or $dsp[4] -ne 0xf0f) { throw 'Dormant DSP/DMA cleanup not verified' }
  Assert-AmpOff
  & (Join-Path $root 'read-headphone-codec.ps1') | Format-List
  Write-Host 'CLEANUP VERIFIED: audio services stopped, DSP dormant, DMA stopped, codec muted, amplifier shutdown requested.'
 } catch { $failure = $_; Write-Host "CLEANUP FAILED: $_" }
 Remove-Item $wav -Force -ErrorAction SilentlyContinue
 Stop-Transcript | Out-Null
}
if ($failure) { exit 1 }
