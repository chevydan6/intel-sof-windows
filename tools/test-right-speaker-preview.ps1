# SPDX-License-Identifier: GPL-2.0-only
# Run an application WAV through the explicitly armed bounded preview.
param([Parameter(Mandatory=$true)][string]$WaveFile,
      [string]$OutputPrefix=(Join-Path $PSScriptRoot 'right-preview'),
      [switch]$Powered)
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
 $armed | ConvertTo-Json | Set-Content ($OutputPrefix+'.armed.json')
 Start-Service AudioEndpointBuilder
 Start-Service Audiosrv
 $player.Play()
 # Capture repeatedly: a single snapshot may fall between packet submission
 # and consumed-data clearing, or during the engine's initial silence.
 for($i=0; $i-lt 8; $i++) {
  Start-Sleep -Milliseconds 100
  & (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxRingOutput ($OutputPrefix+'.ring'+$i+'.bin') | Out-Host
  & (Join-Path $PSScriptRoot 'right-speaker-preview.ps1') | ConvertTo-Json | Set-Content ($OutputPrefix+'.phase'+$i+'.json')
 }
 Start-Sleep -Milliseconds ($armed.MaximumMs+200)
 $player.Stop()
 $state=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1')
 $state | ConvertTo-Json | Set-Content ($OutputPrefix+'.state.json')
 & (Join-Path $PSScriptRoot 'right-speaker-preview.ps1') -Diagnostics | ConvertTo-Json | Set-Content ($OutputPrefix+'.diagnostics.json')
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
