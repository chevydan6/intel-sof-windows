# SPDX-License-Identifier: GPL-2.0-only
# Run an application WAV through the explicitly armed bounded preview.
param([Parameter(Mandatory=$true)][string]$WaveFile,
      [string]$OutputPrefix=(Join-Path $PSScriptRoot 'right-preview'),
      [switch]$Powered)
$ErrorActionPreference='Stop'
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
 Start-Sleep -Milliseconds 350
 & (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxRingOutput ($OutputPrefix+'.ring.bin') | Out-Host
 Start-Sleep -Milliseconds 2500
 $player.Stop()
 $state=& (Join-Path $PSScriptRoot 'right-speaker-preview.ps1')
 $state | ConvertTo-Json | Set-Content ($OutputPrefix+'.state.json')
 if($state.Phase-ne 4 -or $state.Powered-ne [bool]$Powered){throw 'Preview did not finish as requested'}
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
Write-Host 'Preview finished; amplifier off and audio services stopped.'
