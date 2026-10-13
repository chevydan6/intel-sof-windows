# SPDX-License-Identifier: GPL-2.0-only
# One-shot development test. Run elevated; interrupts Windows audio services.
# Supply -RestartDevice -FirmwarePath for a fresh, repeatable test session.
# Without those options the firmware must already be staged on a fresh device.
param([string]$FirmwarePath, [switch]$RestartDevice)
$ErrorActionPreference = 'Stop'
$client = Join-Path $PSScriptRoot 'read-live-snapshot.ps1'
$prepare = Join-Path $PSScriptRoot 'test-speaker-tone.ps1'
$wav = Join-Path $PSScriptRoot 'acx-test-tone.wav'
$prepared = $false
$player = $null
$errors = @()
$id = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$audioWasRunning = (Get-Service Audiosrv).Status -eq 'Running'
$builderWasRunning = (Get-Service AudioEndpointBuilder).Status -eq 'Running'
if ($RestartDevice -and !$FirmwarePath) {
    throw 'RestartDevice requires FirmwarePath because restarting clears staged firmware'
}
if ($FirmwarePath -and !(Test-Path -LiteralPath $FirmwarePath -PathType Leaf)) {
    throw 'FirmwarePath does not exist'
}
if ((Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverProvider).Data -ne
    'Open SOF Windows Project') { throw 'The expected N100 development driver is not installed' }
try {
    # Endpoint setup probes a real stream. Prepare the SOF ring before allowing
    # the audio services to probe it.
    Stop-Service Audiosrv -Force
    Stop-Service AudioEndpointBuilder -Force
    if ($RestartDevice) {
        pnputil /restart-device $id | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'N100 development device restart failed' }
    }
    if ($FirmwarePath) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $client -StageFirmware $FirmwarePath
        if ($LASTEXITCODE -ne 0) { throw 'Firmware staging failed' }
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $prepare -LeavePrepared
    if ($LASTEXITCODE -ne 0) { throw 'SOF pipeline preparation failed' }
    $prepared = $true
    Start-Service AudioEndpointBuilder
    Start-Service Audiosrv
    Start-Sleep -Seconds 3

    $rate = 48000
    $frames = $rate * 3
    $dataBytes = $frames * 4
    $stream = [IO.File]::Create($wav)
    try {
        $writer = [IO.BinaryWriter]::new($stream)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
        $writer.Write([uint32](36 + $dataBytes))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
        $writer.Write([uint32]16)
        $writer.Write([uint16]1)
        $writer.Write([uint16]2)
        $writer.Write([uint32]$rate)
        $writer.Write([uint32]($rate * 4))
        $writer.Write([uint16]4)
        $writer.Write([uint16]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
        $writer.Write([uint32]$dataBytes)
        for ($i = 0; $i -lt $frames; $i++) {
            $sample = [int16]([Math]::Round([Math]::Sin(2 * [Math]::PI *
                             440 * $i / $rate) * 3000))
            $writer.Write($sample)
            $writer.Write($sample)
        }
        $writer.Flush()
    } finally { $stream.Dispose() }

    Add-Type -AssemblyName System
    $player = [System.Media.SoundPlayer]::new($wav)
    Write-Host 'Playing a 3-second 440 Hz WAV through the Windows default audio endpoint.'
    $player.PlaySync()
    $player.Dispose()
    $player = $null
    Start-Sleep -Milliseconds 500
    Write-Host 'Windows playback call returned.'
    $status = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $client -AcxStatus | Out-String
    Write-Host $status
    if ($LASTEXITCODE -ne 0) { throw 'ACX status read failed' }
    foreach ($field in @('StreamsCreated', 'RunCalls', 'PacketsSubmitted', 'PacketsCompleted')) {
        if ($status -notmatch "(?m)^$field\s*:\s*(\d+)" -or
            [uint32]$Matches[1] -eq 0) {
            throw "Windows playback did not reach ACX: $field is zero"
        }
    }
} finally {
    if ($player) { $player.Stop(); $player.Dispose() }
    # Close all service-owned streams before freeing the shared DMA ring.
    $streamsClosed = $false
    try {
        Stop-Service Audiosrv -Force
        Stop-Service AudioEndpointBuilder -Force
        $streamsClosed = $true
    } catch { $errors += $_.ToString() }
    if ($prepared -and $streamsClosed) {
        foreach ($arguments in @(@('-StopTone'),
                                @('-IpcHex','0000000C,60030000,00000000'),
                                @('-StopSession'))) {
            try {
                & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $client @arguments
                if ($LASTEXITCODE -ne 0) { throw "Cleanup failed: $($arguments -join ' ')" }
            } catch {
                $errors += $_.ToString()
            }
        }
    }
    if ($prepared -and !$streamsClosed) {
        $errors += 'SOF ring left allocated because audio services did not stop; restart Windows before another test'
    }
    if ($builderWasRunning -or $audioWasRunning) {
        try { Start-Service AudioEndpointBuilder } catch { $errors += $_.ToString() }
    }
    if ($audioWasRunning) {
        try { Start-Service Audiosrv } catch { $errors += $_.ToString() }
    }
    if (Test-Path $wav) { Remove-Item $wav -Force }
    if ($errors.Count -ne 0) { throw ($errors -join '; ') }
}
