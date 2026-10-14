# SPDX-License-Identifier: GPL-2.0-only
# Query by default. -Arm permits one stream; -Powered additionally permits amp ON.
param([switch]$Arm, [switch]$Powered, [switch]$Diagnostics)
$ErrorActionPreference='Stop'
if($Diagnostics -and ($Arm -or $Powered)){throw 'Diagnostics is read-only'}
if($Powered -and !$Arm){throw 'Powered requires Arm'}
if(!('SofProbeClient' -as [type])) {
 . (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxStatus | Out-Host
}
$paths=@([SofProbeClient]::Paths())
if($paths.Count-ne 1){throw 'Expected one SOF interface'}
if($Diagnostics) {
 $v=[SofProbeClient]::Exchange($paths[0],0x2260A4,$null)
 if($v.Length-ne 12 -or $v[0]-ne 1 -or $v[1]-gt 11){throw 'Unexpected preview diagnostic ABI'}
 $reasons=@('NotStopped','Deadline','LateTick','AdvanceError','InvalidPcm','NoProgress','Inactive','AmpStartError','AmpRenewError','PolicyError','Pause','AmpStopError')
 [pscustomobject]@{Version=$v[0];Reason=$reasons[$v[1]];PhaseAtStop=$v[2];Ticks=$v[3];ElapsedMs=$v[4];GapMs=$v[5];
  PreviousPosition=([uint64]$v[7]*4294967296+$v[6]);ObservedPosition=([uint64]$v[9]*4294967296+$v[8]);
  TransportFlags=$v[10];AdvanceStatus=('0x{0:X8}'-f $v[11])}
 return
}
$inputBytes=$null
$code=[uint32]0x2260A0
if($Arm) {
 $code=[uint32]0x22A09C
 $inputBytes=[BitConverter]::GetBytes([uint32][bool]$Powered)
}
$v=[SofProbeClient]::Exchange($paths[0],$code,$inputBytes)
if($v.Length-ne 6 -or $v[0]-ne 1 -or $v[1]-gt 4 -or $v[5]-ne 2000) {
 throw 'Unexpected right-speaker preview ABI'
}
[pscustomobject]@{Version=$v[0];Phase=$v[1];State=@('Unarmed','Armed','WaitingForDma','Active','Finished')[$v[1]];
 Powered=[bool]$v[2];Error=('0x{0:X8}'-f $v[3]);AmpOnWrites=$v[4];MaximumMs=$v[5]}
if($Arm -and $v[3]-ne 0){throw 'Preview faulted; inspect status before another test'}
if($Arm -and $v[1]-ne 1){throw 'Preview did not arm'}
