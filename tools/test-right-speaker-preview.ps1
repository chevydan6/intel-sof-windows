# SPDX-License-Identifier: GPL-2.0-only
# Run an application WAV through the explicitly armed bounded preview.
param([Parameter(Mandatory=$true)][string]$WaveFile,
      [string]$OutputPrefix=(Join-Path $PSScriptRoot 'right-preview'),
      [switch]$Powered,[switch]$StopEarly)
$ErrorActionPreference='Stop'
if([Diagnostics.Process]::GetCurrentProcess().SessionId-eq 0) {
 throw 'Run in the signed-in desktop session using invoke-right-speaker-preview.ps1; SSH session playback can be silent.'
}
Start-Transcript -LiteralPath ($OutputPrefix+'.log') -Force | Out-Null
$player=$null
try {
 Stop-Service Audiosrv -Force
 Stop-Service AudioEndpointBuilder -Force
 $before=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1')
 if(!$before.Ready -or $before.Active -or $before.Faulted -or $before.OutputRequest-ne 1){throw 'Amplifier must be ready and off'}
 $player=[System.Media.SoundPlayer]::new((Resolve-Path $WaveFile).Path)
 $player.Load()
 $armed=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1') -Arm -Powered:$Powered
 if($StopEarly -and $armed.MaximumMs-ne 30000){throw 'Early-stop test requires the thirty-second preview'}
 $armed | ConvertTo-Json | Set-Content ($OutputPrefix+'.armed.json')
 Start-Service AudioEndpointBuilder
 Start-Service Audiosrv
 $player.Play()
 $playClock=[Diagnostics.Stopwatch]::StartNew()
 # Capture repeatedly: a single snapshot may fall between packet submission
 # and consumed-data clearing, or during the engine's initial silence.
 for($i=0; $i-lt 8; $i++) {
  Start-Sleep -Milliseconds 100
  & (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxRingOutput ($OutputPrefix+'.ring'+$i+'.bin') | Out-Host
  & (Join-Path $PSScriptRoot 'right-speaker-preview.ps1') | ConvertTo-Json | Set-Content ($OutputPrefix+'.phase'+$i+'.json')
 }
 if($StopEarly) {
  $remaining=5000-$playClock.ElapsedMilliseconds
  if($remaining-gt 0){Start-Sleep -Milliseconds $remaining}
  $active=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1')
  if($active.Phase-ne 3 -or $active.Error-ne '0x00000000'){throw 'Preview was not active before application stop'}
  $stopClock=[Diagnostics.Stopwatch]::StartNew()
  $player.Stop()
  $stopCallMs=$stopClock.ElapsedMilliseconds
  $observations=@()
  do {
   $state=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1')
   $amp=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1')
   $observations+=[pscustomobject]@{AfterStopMs=$stopClock.ElapsedMilliseconds;StopCallMs=$stopCallMs;State=$state;Amp=$amp}
   if($state.Phase-eq 4 -and !$amp.Active -and $amp.OutputRequest-eq 1){break}
   Start-Sleep -Milliseconds 100
  } while($stopClock.ElapsedMilliseconds-lt 5000)
  $observations | ConvertTo-Json -Depth 5 | Set-Content ($OutputPrefix+'.stop-observations.json')
  if($state.Phase-ne 4 -or $amp.Active -or $amp.OutputRequest-ne 1){throw 'Application stop did not shut down the preview within five seconds'}
  # Audio services remain running throughout idle observation. Cleanup STOP
  # must not be the action responsible for the off-state under test.
  $idle=@()
  for($i=0;$i-lt 6;$i++) {
   Start-Sleep -Seconds 1
   $amp=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1')
   $idle+=[pscustomobject]@{AfterStopMs=$stopClock.ElapsedMilliseconds;Amp=$amp}
   if($amp.Active -or $amp.OutputRequest-ne 1 -or $amp.Faulted -or $amp.OnWrites-ne ($before.OnWrites+[int][bool]$Powered)){throw 'Amplifier changed state during idle observation'}
  }
  $idle | ConvertTo-Json -Depth 4 | Set-Content ($OutputPrefix+'.idle.json')
 } else {
  Start-Sleep -Milliseconds ($armed.MaximumMs+200)
  $player.Stop()
 }
 $state=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1')
 $state | ConvertTo-Json | Set-Content ($OutputPrefix+'.state.json')
 $diagnostics=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1') -Diagnostics
 $diagnostics | ConvertTo-Json | Set-Content ($OutputPrefix+'.diagnostics.json')
 if($StopEarly) {
  if($diagnostics.Reason-ne 'Pause' -or $diagnostics.ElapsedMs-ge $armed.MaximumMs){throw 'Expected application pause before the preview deadline'}
 } elseif($diagnostics.Reason-ne 'Deadline' -or $diagnostics.ElapsedMs-lt $armed.MaximumMs -or $diagnostics.ElapsedMs-gt ($armed.MaximumMs+80)){throw 'Preview did not run to its intended deadline within the callback allowance'}
 if($state.Phase-ne 4 -or $state.Powered-ne [bool]$Powered -or $state.Error-ne '0x00000000'){throw 'Preview did not finish successfully as requested'}
 & (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxStatus | Out-String | Set-Content ($OutputPrefix+'.acx.txt')
} finally {
 if($player){$player.Stop();$player.Dispose()}
 $after=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1') -Stop
 $after | ConvertTo-Json | Set-Content ($OutputPrefix+'.amp.json')
 Stop-Service Audiosrv -Force
 Stop-Service AudioEndpointBuilder -Force
 if($after.Active -or $after.OutputRequest-ne 1 -or $after.Faulted -or $after.CleanupStatus-ne '0x00000000'){throw 'Amplifier shutdown verification failed'}
 if($before -and $after.OnWrites-ne ($before.OnWrites+[int][bool]$Powered)){throw 'Unexpected amplifier enable count'}
}
$nonzero=0L
for($i=0; $i-lt 8; $i++) {
 $file=$OutputPrefix+'.ring'+$i+'.bin'
 if(!(Test-Path -LiteralPath $file)){continue}
 $bytes=[IO.File]::ReadAllBytes($file)
 if($bytes.Length-ne 49184 -or [BitConverter]::ToUInt32($bytes,0)-ne 1 -or [BitConverter]::ToUInt32($bytes,4)-ne 49152){throw 'Invalid ring snapshot'}
 for($j=32; $j-lt $bytes.Length; $j+=8) {
  if([BitConverter]::ToInt32($bytes,$j)-ne 0){throw 'Left channel was not silent'}
  $right=[BitConverter]::ToInt32($bytes,$j+4)
  if([Math]::Abs([long]$right)-gt 536870911){throw 'Right channel exceeded the preview ceiling'}
  if($right-ne 0){$nonzero++}
 }
}
if($nonzero-eq 0){throw 'No non-silent application audio captured; playback is not verified'}
Write-Host "Preview finished with $nonzero nonzero right samples; left silent, amplifier off, audio services stopped."
Stop-Transcript | Out-Null
