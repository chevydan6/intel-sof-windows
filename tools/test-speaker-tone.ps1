# SPDX-License-Identifier: GPL-2.0-only
# One-shot hardware test. Requires the test probe and firmware staged already.
param([switch]$LeavePrepared)
$ErrorActionPreference = 'Stop'
$client = Join-Path $PSScriptRoot 'read-live-snapshot.ps1'
function Invoke-Probe([string]$name, [string]$argument) {
    Write-Host "== $name =="
    $output = if ($argument) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $client -IpcHex $argument 2>&1 | Out-String
    } else {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $client "-$name" 2>&1 | Out-String
    }
    Write-Host $output
    if ($LASTEXITCODE -ne 0) { throw "$name failed with exit code $LASTEXITCODE" }
    return $output
}
function Send-Ipc([string]$name, [uint32[]]$words) {
    if ($words[0] -ne 4 * $words.Count) { throw "Invalid message length: $name" }
    $hex = ($words | ForEach-Object { '{0:X8}' -f $_ }) -join ','
    Invoke-Probe $name $hex | Out-Null
}
function New-Words([int]$bytes, [uint32]$command) {
    $words = New-Object uint32[] ($bytes / 4)
    $words[0] = $bytes
    $words[1] = $command
    return ,$words
}

$session = $false
$tonePrepared = $false
try {
    Invoke-Probe 'StartSession' '' | Out-Null
    $session = $true
    Send-Ipc 'Pipeline' ([uint32[]]@(48,0x30100000,5,1,0,0,1000,0,5000,0,0,1))
    Send-Ipc 'Buffer' ([uint32[]]@(44,0x30200000,2,12,1,0,0,768,0x71,0,0))
    $msg = New-Words 76 0x30010000
    $msg[2]=0; $msg[3]=1; $msg[4]=1; $msg[7]=36
    $msg[9]=2; $msg[10]=2; $msg[12]=2
    Send-Ipc 'Host component' $msg
    $msg = New-Words 80 0x30010000
    $msg[2]=4; $msg[3]=2; $msg[4]=1; $msg[7]=36
    $msg[10]=2; $msg[12]=2; $msg[17]=1; $msg[18]=1
    Send-Ipc 'SSP1 DAI component' $msg
    $msg = New-Words 112 ([Convert]::ToUInt32('80010000',16))
    $msg[2]=1; $msg[3]=1; $msg[4]=0x4001
    $msg[13]=60; $msg[15]=24000000; $msg[16]=48000
    $msg[17]=3072000; $msg[18]=2; $msg[19]=3; $msg[20]=3
    $msg[21]=32; $msg[22]=32; $msg[23]=1
    Send-Ipc 'SSP1 DAI configuration' $msg
    Send-Ipc 'Host to buffer' ([uint32[]]@(16,0x30030000,0,2))
    Send-Ipc 'Buffer to DAI' ([uint32[]]@(16,0x30030000,2,4))
    Send-Ipc 'Complete pipeline' ([uint32[]]@(12,0x30130000,5))
    $prepared = Invoke-Probe 'PrepareTone' ''
    $tonePrepared = $true
    if ($prepared -notmatch 'PageTableAddress\s*:\s*0x([0-9A-Fa-f]{8})') {
        throw 'The probe did not report the tone page table address'
    }
    $table = [Convert]::ToUInt32($Matches[1],16)
    $msg = New-Words 108 0x60010000
    $msg[6]=84; $msg[8]=$table; $msg[9]=12; $msg[10]=49152
    $msg[15]=2; $msg[17]=48000; $msg[18]=0x00020001
    $msg[19]=0x00040004; $msg[20]=12288; $msg[21]=1
    Send-Ipc 'PCM parameters' $msg
    if ($LeavePrepared) {
        Write-Host 'Speaker PCM is prepared for an ACX stream.'
        $tonePrepared = $false
        $session = $false
        return
    }
    Invoke-Probe 'RunTone' '' | Out-Null
    Send-Ipc 'Start speaker stream' ([uint32[]]@(12,0x60040000,0))
    Start-Sleep -Seconds 3
    Send-Ipc 'Stop speaker stream' ([uint32[]]@(12,0x60050000,0))
    $stopped = Invoke-Probe 'StopTone' ''
    $tonePrepared = $false
    if ($stopped -notmatch 'StreamPosition\s*:\s*(\d+)' -or
        [uint32]$Matches[1] -eq 0) {
        throw 'The HDA DMA stream did not advance'
    }
    Send-Ipc 'Free PCM parameters' ([uint32[]]@(12,0x60030000,0))
    Write-Host 'Tone transfer completed; stream DMA advanced.'
} finally {
    if ($tonePrepared) {
        try { Invoke-Probe 'StopTone' '' | Out-Null } catch { Write-Warning $_ }
    }
    if ($session) {
        try { Invoke-Probe 'StopSession' '' | Out-Null } catch { Write-Warning $_ }
    }
}
