# SPDX-License-Identifier: GPL-2.0-only
# Physical unplug/replug check during a bounded silent Windows stream.
param([string]$Wav)
$ErrorActionPreference='Stop';$root=$PSScriptRoot
$parent='PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$task='SofHeadphoneJackTest';$events=@();$failure=$null
Start-Transcript -Path (Join-Path $root 'jack-test.log') -Force | Out-Null
try {
 . (Join-Path $root 'read-live-snapshot.ps1') -AcxStatus | Out-Host
 $amp=Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\MX98360A\0\Device Parameters'
 if($amp.SofAmpStage -ne 3 -or $amp.SofAmpStatus -ne 0 -or $amp.SofAmpOffWrites -lt 1){throw 'Amplifier hold not verified'}
 Start-Service AudioEndpointBuilder;Start-Service Audiosrv
 $action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $root 'test-headphone-playback.ps1')+'" -Player -Wav "'+$Wav+'"')
 $principal=New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\dan" -LogonType Interactive -RunLevel Highest
 $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 150) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Force | Out-Null
 Start-ScheduledTask -TaskName $task
 $phase=0;$last='';$watch=[Diagnostics.Stopwatch]::StartNew()
 while($watch.Elapsed.TotalSeconds -lt 120) {
  $c=& (Join-Path $root 'read-headphone-codec.ps1') -AllowPlaying
  $r=Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\10EC5682\0\Device Parameters'
  $state="$($r.CodecStreamRequested)/$($r.CodecPlaying)/$($r.CodecJackPresent)/$($c.HpMute)/$($c.DigitalPower)"
  if($state -ne $last){$ev=@{Seconds=$watch.Elapsed.TotalSeconds;State=$state;Polls=$r.CodecJackPolls};$events+=$ev;$ev|ConvertTo-Json -Compress|Write-Host;$last=$state}
  if($phase -eq 0 -and $r.CodecPlaying -eq 1){$phase=1;Write-Host 'READY: unplug headphones, wait five seconds, then reconnect'}
  elseif($phase -eq 1 -and $r.CodecStreamRequested -eq 1 -and $r.CodecPlaying -eq 0 -and $r.CodecJackPresent -eq 0 -and $c.HpMute -eq '0x8080' -and $c.DigitalPower -eq '0x0000'){$phase=2;Write-Host 'REMOVAL VERIFIED: output muted and powered down; waiting for reconnection'}
  elseif($phase -eq 2 -and $r.CodecPlaying -eq 1 -and $r.CodecJackPresent -eq 1 -and $c.HpMute -eq '0x0000'){$phase=3;Write-Host 'REINSERTION VERIFIED: headphone output resumed automatically';break}
  Start-Sleep -Milliseconds 200
 }
 if($phase -ne 3){throw "Physical jack sequence not completed; phase=$phase"}
} catch {$failure=$_;Write-Host "FAILED: $_"}
finally {
 $events | ConvertTo-Json | Set-Content (Join-Path $root 'jack-test.json')
 Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
 Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
 Stop-Service Audiosrv -Force -ErrorAction Continue;Stop-Service AudioEndpointBuilder -Force -ErrorAction Continue
 try {
  & (Join-Path $root 'read-headphone-codec.ps1') -Stop | Format-List | Out-Host
  pnputil /restart-device $parent | Out-Host
  if($LASTEXITCODE -ne 0){throw 'Parent cleanup failed'}
  $path=@([SofProbeClient]::Paths())[0];$d=[SofProbeClient]::Exchange($path,0x226000,$null)
  if($d[4] -ne 0xf0f){throw 'DSP shutdown not verified'}
  Write-Host 'CLEANUP VERIFIED: codec muted and DSP dormant'
 } catch {$failure=$_;Write-Host "CLEANUP FAILED: $_"}
 Stop-Transcript | Out-Null
}
if($failure){exit 1}
