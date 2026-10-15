# SPDX-License-Identifier: GPL-2.0-only
# Two ordinary app streams, including silence and playback past the preview cap.
param([Parameter(Mandatory=$true)][string]$WaveFile,
      [Parameter(Mandatory=$true)][string]$OutputPrefix,[switch]$Powered)
$ErrorActionPreference='Stop'
if([Diagnostics.Process]::GetCurrentProcess().SessionId-eq 0){throw 'Use the desktop-session wrapper'}
Start-Transcript -LiteralPath ($OutputPrefix+'.log') -Force | Out-Null
$player=$null
function Read-State { & (Join-Path $PSScriptRoot 'right-speaker-preview.ps1') }
function Read-Amp { & (Join-Path $PSScriptRoot 'read-speaker-amp.ps1') }
function Wait-At([long]$Milliseconds) {
 $remaining=$Milliseconds-$clock.ElapsedMilliseconds
 if($remaining-gt 0){Start-Sleep -Milliseconds $remaining}
}
function Assert-Active {
 $s=Read-State
 if($s.MaximumMs-ne 0 -or $s.Phase-ne 3 -or $s.Error-ne '0x00000000' -or $s.Powered-ne [bool]$Powered){throw 'Ordinary application stream is not active in the requested trial mode'}
}
try {
 Stop-Service Audiosrv -Force
 Stop-Service AudioEndpointBuilder -Force
 $before=Read-Amp
 if(!$before.Ready -or $before.Active -or $before.Faulted -or $before.OutputRequest-ne 1){throw 'Expected ready/off amplifier'}
 $initial=Read-State
 if($initial.MaximumMs-ne 0 -or $initial.Phase-ne 0){throw 'Expected freshly restarted trial driver'}
 $initial | ConvertTo-Json | Set-Content ($OutputPrefix+'.armed.json')
 $player=[System.Media.SoundPlayer]::new((Resolve-Path $WaveFile).Path)
 $player.Load()
 Start-Service AudioEndpointBuilder
 Start-Service Audiosrv
 $stops=@()
 for($cycle=1;$cycle-le 2;$cycle++) {
  $player.Play()
  $clock=[Diagnostics.Stopwatch]::StartNew()
  Wait-At 2000
  Assert-Active
  if($cycle-eq 1) {
   for($i=0;$i-lt 4;$i++) {
    & (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxRingOutput ($OutputPrefix+'.ring'+$i+'.bin') | Out-Host
    Start-Sleep -Milliseconds 100
   }
   # Fixture contains three seconds of silence starting at three seconds.
   Wait-At 4500
   Assert-Active
   $quiet=Read-Amp
   $quiet | ConvertTo-Json | Set-Content ($OutputPrefix+'.silence.json')
   if($quiet.Active -or $quiet.OutputRequest-ne 1 -or $quiet.Faulted){throw 'Silent running stream did not turn amplifier off'}
   Wait-At 8000
   Assert-Active
   $resumed=Read-Amp
   $resumed | ConvertTo-Json | Set-Content ($OutputPrefix+'.resumed.json')
   if($resumed.Active-ne [int][bool]$Powered -or $resumed.Faulted){throw 'Audio did not resume as expected after silence'}
   Wait-At 35000
   Assert-Active
   $beyond=Read-State
   $beyond | ConvertTo-Json | Set-Content ($OutputPrefix+'.beyond30.json')
  }
  $stopClock=[Diagnostics.Stopwatch]::StartNew()
  $player.Stop()
  do {
   $state=Read-State;$amp=Read-Amp
   if($state.Phase-eq 4 -and !$amp.Active -and $amp.OutputRequest-eq 1){break}
   Start-Sleep -Milliseconds 100
  } while($stopClock.ElapsedMilliseconds-lt 5000)
  $diag=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1') -Diagnostics
  $stops+=[pscustomobject]@{Cycle=$cycle;AfterStopMs=$stopClock.ElapsedMilliseconds;State=$state;Amp=$amp;Diagnostics=$diag}
  if($state.Phase-ne 4 -or $state.Error-ne '0x00000000' -or $amp.Active -or $amp.OutputRequest-ne 1 -or $amp.Faulted -or $diag.Reason-ne 'Pause'){throw 'Application stop failed'}
  & (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxStatus | Out-String | Set-Content ($OutputPrefix+'.cycle'+$cycle+'.acx.txt')
  Start-Sleep -Seconds 1
 }
 $stops | ConvertTo-Json -Depth 5 | Set-Content ($OutputPrefix+'.stops.json')
 $state | ConvertTo-Json | Set-Content ($OutputPrefix+'.state.json')
 if($amp.OnWrites-ne ($before.OnWrites+3*[int][bool]$Powered)){throw 'Unexpected amplifier enable count across silence and two streams'}
 $nonzero=0L
 for($i=0;$i-lt 4;$i++) {
  $bytes=[IO.File]::ReadAllBytes($OutputPrefix+'.ring'+$i+'.bin')
  if($bytes.Length-ne 49184 -or [BitConverter]::ToUInt32($bytes,0)-ne 1){throw 'Invalid ring data'}
  for($j=32;$j-lt $bytes.Length;$j+=8) {
   if([BitConverter]::ToInt32($bytes,$j)-ne 0){throw 'Left slot was not silent'}
   $sample=[BitConverter]::ToInt32($bytes,$j+4)
   if([Math]::Abs([long]$sample)-gt 536870911){throw 'Peak ceiling exceeded'}
   if($sample-ne 0){$nonzero++}
  }
 }
 if(!$nonzero){throw 'No application PCM captured'}
 "PASS: repeated app playback, beyond 30 seconds, silence gate, resume and stop"
} finally {
 if($player){$player.Stop();$player.Dispose()}
 $amp=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1') -Stop
 $amp | ConvertTo-Json | Set-Content ($OutputPrefix+'.amp.json')
 Stop-Service Audiosrv -Force
 Stop-Service AudioEndpointBuilder -Force
 if($amp.Active -or $amp.OutputRequest-ne 1 -or $amp.Faulted -or $amp.CleanupStatus-ne '0x00000000'){throw 'Final amplifier off verification failed'}
 Stop-Transcript | Out-Null
}
