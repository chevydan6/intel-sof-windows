# SPDX-License-Identifier: GPL-2.0-only
# Normal Windows stream lifecycle test. A supplied local PCM WAV is played
# twice; the first pass can be captured by the physical built-in microphones.
param([switch]$Player, [string]$Wav, [string]$Record, [switch]$LeaveReady, [switch]$ControlTest, [string]$CaptureReady, [ValidateRange(1,2)][int]$Cycles=2)
$ErrorActionPreference='Stop'
if ($Player) {
 $p=[System.Media.SoundPlayer]::new($Wav)
 $level=$null
 try {
  if($ControlTest) {
   . (Join-Path $PSScriptRoot 'headphone-level.ps1')
   $level=[HeadphoneLevel]::new();$oldDb=$level.Db;$oldMute=$level.Muted
   $level.SetMuted($false)
   $p.Load();$p.Play();$watch=[Diagnostics.Stopwatch]::StartNew()
   for($i=0;$i -lt 400;$i++){if(Test-Path $CaptureReady){break};Start-Sleep -Milliseconds 25}
   if(!(Test-Path $CaptureReady)){throw 'Microphone readiness deadline expired'}
   $events=@()
   $events+=@{State='baseline';Db=$level.Db;Utc=(Get-Date).ToUniversalTime().ToString('o')}
   Start-Sleep -Milliseconds 1300
   $level.SetDb([float][Math]::Max(-60,$oldDb-12))
   $events+=@{State='lowered';Db=$level.Db;Utc=(Get-Date).ToUniversalTime().ToString('o')}
   Start-Sleep -Milliseconds 1300
   $level.SetMuted($true)
   $events+=@{State='muted';Muted=$level.Muted;Utc=(Get-Date).ToUniversalTime().ToString('o')}
   Start-Sleep -Milliseconds 1300
   $level.SetDb($oldDb);$level.SetMuted($false)
   $events+=@{State='restored';Db=$level.Db;Utc=(Get-Date).ToUniversalTime().ToString('o')}
   $events | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'volume-test.json')
   while($watch.Elapsed.TotalSeconds -lt 16){Start-Sleep -Milliseconds 100}
  } else {$p.PlaySync()}
 } finally {
  $p.Stop();$p.Dispose()
  if($level){try{$level.SetDb($oldDb);$level.SetMuted($oldMute)}finally{$level.Dispose()}}
 }
 exit 0
}
if ($ControlTest -and !$Record) {throw 'Control test needs microphone recording'}
if (!$Wav -or !(Test-Path $Wav)) {throw 'Supply the test WAV path'}
$root=$PSScriptRoot
$parent='PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$task='SofHeadphonePlaybackTest'
$failure=$null;$micProcess=$null
function Assert-AmpOff {
 $a=Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\MX98360A\0\Device Parameters'
 if($a.SofAmpStage -ne 3 -or $a.SofAmpStatus -ne 0 -or $a.SofAmpOffWrites -lt 1){throw 'Amplifier shutdown request not verified'}
}
function Read-Transport {
 $paths=@([SofProbeClient]::Paths())
 if($paths.Count -ne 1){throw 'Expected one headphone transport'}
 $v=[SofProbeClient]::Exchange($paths[0],0x22605C,$null)
 if($v[0] -ne 8 -or $v[24] -ne 1 -or $v[25] -ne 1){throw 'Headphone-only thermal hold required'}
 return ,$v
}
function Assert-CodecOff {
 $c=& (Join-Path $root 'read-headphone-codec.ps1')
 $c | Format-List | Out-Host
 if($c.Prepared -ne 0 -or $c.DigitalPower -ne '0x0000'){throw 'Codec did not power down on stream stop'}
 $r=Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\10EC5682\0\Device Parameters'
 if($r.CodecStreamRequested -ne 0 -or $r.CodecPlaying -ne 0){throw 'Codec stream ownership not released'}
}
Start-Transcript -Path (Join-Path $root 'playback-test.log') -Force | Out-Null
try {
 . (Join-Path $root 'read-live-snapshot.ps1') -AcxStatus | Out-Host
 Assert-AmpOff
 for($cycle=1;$cycle -le $Cycles;$cycle++) {
  $before=Read-Transport
  Start-Service AudioEndpointBuilder; Start-Service Audiosrv
  $readyFile=Join-Path $root 'microphone.ready';Remove-Item $readyFile -ErrorAction SilentlyContinue
  $playerArgs='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Player -Wav "'+$Wav+'"'
  if($ControlTest -and $cycle -eq 1){$playerArgs+=' -ControlTest -CaptureReady "'+$readyFile+'"'}
  $action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $playerArgs
  $principal=New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\dan" -LogonType Interactive -RunLevel Highest
  $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 45) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
  Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Force | Out-Null
  $started=Get-Date;Start-ScheduledTask -TaskName $task
  $ready=$false
  for($i=0;$i -lt 80;$i++) {
   Start-Sleep -Milliseconds 100
   $live=Read-Transport
   if($live[13] -eq 1 -and $live[2] -gt $before[2] -and $live[4]-$before[4] -ge 50){$ready=$true;break}
  }
  if(!$ready){throw 'Windows did not start a headphone stream'}
  $c=& (Join-Path $root 'read-headphone-codec.ps1') -AllowPlaying
  $c | Format-List | Out-Host
  if($c.Prepared -ne 1 -or $c.HpMute -ne '0x0000' -or $c.DacControl -ne '0x6000'){
   $jack=Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\10EC5682\0\Device Parameters'
   $jack | Select-Object CodecStreamRequested,CodecPlaying,CodecJackPresent,CodecJackPolls | Format-List | Out-Host
   if($jack.CodecStreamRequested-eq 1 -and $jack.CodecPlaying-eq 0 -and $jack.CodecJackPresent-eq 0 -and $c.LastStatus-eq '0x00000000'){
    throw 'Headphone stream is waiting for jack detection; connect headphones before this playback test'
   }
   throw 'Normal stream did not unmute the headphone codec'
  }
  if($Record -and $cycle -eq 1) {
   $readyFile=Join-Path $root 'microphone.ready';Remove-Item $readyFile -ErrorAction SilentlyContinue
   $args='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $root 'test-microphone.ps1')+'" -DuringAudition -Output "'+$Record+'" -ReadyFile "'+$readyFile+'"'
   $micProcess=Start-Process powershell.exe -ArgumentList $args -PassThru -WindowStyle Hidden -RedirectStandardOutput ($Record+'.log') -RedirectStandardError ($Record+'.err')
   $null=$micProcess.Handle
   if(!$micProcess.WaitForExit(15000)){throw 'Microphone capture timed out'}
   $micProcess.Refresh()
   Get-Content ($Record+'.log') | Write-Host
   Get-Content ($Record+'.err') | Write-Host
   if($null -eq $micProcess.ExitCode -or $micProcess.ExitCode -ne 0){throw 'Microphone capture failed'}
  }
  $finished=$false
  for($i=0;$i -lt 40;$i++) {
   Start-Sleep -Milliseconds 500
   $info=Get-ScheduledTaskInfo -TaskName $task
   $state=(Get-ScheduledTask -TaskName $task).State
   if($info.LastRunTime -ge $started.AddSeconds(-1) -and $state -ne 'Running' -and $info.LastTaskResult -ne 267009){$finished=$true;break}
  }
  if(!$finished -or $info.LastTaskResult -ne 0){throw "Player failed: $($info.LastTaskResult), $state"}
  $after=Read-Transport
  Write-Host "Cycle $cycle transport: $($after | ConvertTo-Json -Compress)"
  if($after[12] -ne 0 -or $after[4]-$before[4] -lt 1000 -or $after[16] -ne 0){throw 'Playback failed, was too short, or had late packets'}
  Assert-AmpOff
  # Exercise ordinary stream release, not a codec STOP command or PnP reset.
  Stop-Service Audiosrv -Force
  Stop-Service AudioEndpointBuilder -Force
  Assert-CodecOff
  Write-Host "PASS: cycle $cycle normal start, sustained playback and stream-owned shutdown"
 }
 if($Cycles -gt 1 -and (Read-Transport)[14] -lt 1){throw 'PCM rearm was not exercised'}
} catch {$failure=$_;Write-Host "FAILED: $_"}
finally {
 if($micProcess -and !$micProcess.HasExited){if(!$micProcess.WaitForExit(10000)){$micProcess.Kill();$failure='Microphone process timeout'}}
 Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
 Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
 Stop-Service Audiosrv -Force -ErrorAction Continue
 Stop-Service AudioEndpointBuilder -Force -ErrorAction Continue
 try {
  & (Join-Path $root 'read-headphone-codec.ps1') -Stop | Format-List | Out-Host
  & pnputil /restart-device $parent | Out-Host
  if($LASTEXITCODE -ne 0){throw 'Parent cleanup failed; no reboot performed'}
  $idle=Read-Transport;$path=@([SofProbeClient]::Paths())[0]
  $d=[SofProbeClient]::Exchange($path,0x226000,$null)
  if($idle[13] -ne 0 -or $d[4] -ne 0xf0f){throw 'DSP shutdown not verified'}
  Assert-CodecOff;Assert-AmpOff
  Write-Host 'CLEANUP VERIFIED: codec muted and powered down, DSP dormant, speaker hold retained'
 } catch {$failure=$_;Write-Host "CLEANUP FAILED: $_"}
 Stop-Transcript | Out-Null
}
if($failure){exit 1}
if($LeaveReady){Start-Service AudioEndpointBuilder;Start-Service Audiosrv;Write-Host 'Audio services running; normal headphone playback is available'}
