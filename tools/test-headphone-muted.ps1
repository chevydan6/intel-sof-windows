# SPDX-License-Identifier: GPL-2.0-only
# Bounded Windows playback test with RT5682 hardware mutes asserted throughout.
param([switch]$Player)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$wav = Join-Path $root 'muted-test.wav'
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
Start-Transcript -Path (Join-Path $root 'muted-test.log') -Force | Out-Null
try {
 . (Join-Path $root 'read-live-snapshot.ps1') -AcxStatus | Out-Host
 Assert-AmpOff
 & (Join-Path $root 'read-headphone-codec.ps1') | Format-List
 $before = Read-Transport
 # Quiet source for a transport-only test: silence is sufficient for DMA progress.
 $stream = [IO.File]::Create($wav)
 try {
  $w = [IO.BinaryWriter]::new($stream)
  $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([uint32](36+576000))
  $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $w.Write([uint32]16)
  $w.Write([uint16]1); $w.Write([uint16]2); $w.Write([uint32]48000); $w.Write([uint32]192000)
  $w.Write([uint16]4); $w.Write([uint16]16); $w.Write([Text.Encoding]::ASCII.GetBytes('data'))
  $w.Write([uint32]576000); $w.Write([byte[]]::new(576000)); $w.Flush()
 } finally { $stream.Dispose() }
 Start-Service AudioEndpointBuilder
 Start-Service Audiosrv
 $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Player')
 $principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\dan" -LogonType Interactive -RunLevel Highest
 $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 20) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Force | Out-Null
 $started = Get-Date
 Start-ScheduledTask -TaskName $task
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
 $codec = & (Join-Path $root 'read-headphone-codec.ps1')
 $codec | Format-List
 Assert-AmpOff
 if ($after[13] -ne 1 -or $after[11] -ne 6 -or $after[12] -ne 0 -or $after[2] -lt 1 -or $after[3]-$before[3] -lt 250 -or $after[4]-$before[4] -lt 250 -or $codec.Prepared -ne 1) { throw 'Muted transport progress not verified' }
 Write-Host 'PASS: muted headphone stream advanced at least 250 packets; this does not verify audible output.'
} catch {
 $failure = $_
 Write-Host "FAILED: $_"
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
