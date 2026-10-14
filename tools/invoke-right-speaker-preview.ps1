# SPDX-License-Identifier: GPL-2.0-only
# Invoke the bounded preview in the logged-in desktop, including from SSH.
param([Parameter(Mandatory=$true)][string]$WaveFile,
      [Parameter(Mandatory=$true)][string]$OutputPrefix,
      [switch]$Powered)
$ErrorActionPreference='Stop'
$user=(Get-CimInstance Win32_ComputerSystem).UserName
if(!$user){throw 'Sign into the Windows desktop first; no interactive user is present.'}
$WaveFile=(Resolve-Path -LiteralPath $WaveFile).Path
$OutputPrefix=[IO.Path]::GetFullPath($OutputPrefix)
if(Test-Path -LiteralPath ($OutputPrefix+'.armed.json')){throw 'Use a fresh output prefix so old evidence cannot pass the test.'}
$task='SofRightPreview-'+[Guid]::NewGuid().ToString('N')
$runner=Join-Path $PSScriptRoot 'test-right-speaker-preview.ps1'
$arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$runner+'" -WaveFile "'+$WaveFile+'" -OutputPrefix "'+$OutputPrefix+'"'
if($Powered){$arguments+=' -Powered'}
$registered=$false
try {
 $action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $arguments
 $principal=New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
 $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 60) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings | Out-Null
 $registered=$true
 $started=Get-Date
 Start-ScheduledTask -TaskName $task
 $finished=$false
 for($i=0;$i-lt 120;$i++) {
  Start-Sleep -Milliseconds 500
  $info=Get-ScheduledTaskInfo -TaskName $task
  if($info.LastRunTime-ge $started.AddSeconds(-1) -and (Get-ScheduledTask -TaskName $task).State-ne 'Running' -and $info.LastTaskResult-ne 267009){$finished=$true;break}
 }
 if(!$finished -or $info.LastTaskResult-ne 0){throw "Desktop test failed or did not run: $($info.LastTaskResult)"}
 Get-Content -LiteralPath ($OutputPrefix+'.state.json')
 Get-Content -LiteralPath ($OutputPrefix+'.amp.json')
} finally {
 if($registered){
  Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
  Unregister-ScheduledTask -TaskName $task -Confirm:$false
 }
 # Also clean up if the desktop process failed before reaching its finally.
 try {
  $amp=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1') -Stop
  $amp | Out-Host
  if(!$amp.Ready -or $amp.Active -or $amp.OutputRequest-ne 1 -or $amp.Faulted -or $amp.CleanupStatus-ne '0x00000000'){throw 'Outer cleanup could not verify amplifier off'}
 } finally {
  Stop-Service Audiosrv -Force
  Stop-Service AudioEndpointBuilder -Force
 }
}
