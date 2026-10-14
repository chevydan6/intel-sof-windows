# SPDX-License-Identifier: GPL-2.0-only
# Query by default. -Arm permits one stream; -Powered additionally permits amp ON.
param([switch]$Arm, [switch]$Powered)
$ErrorActionPreference='Stop'
if($Powered -and !$Arm){throw 'Powered requires Arm'}
if(!('SofProbeClient' -as [type])) {
 . (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxStatus | Out-Host
}
$paths=@([SofProbeClient]::Paths())
if($paths.Count-ne 1){throw 'Expected one SOF interface'}
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
if($v[3]-ne 0){throw 'Preview faulted; inspect status before another test'}
if($Arm -and $v[1]-ne 1){throw 'Preview did not arm'}
