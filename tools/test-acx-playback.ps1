# SPDX-License-Identifier: GPL-2.0-only
# One-shot development test. Stage firmware before running this script.
$ErrorActionPreference = 'Stop'
$client = Join-Path $PSScriptRoot 'read-live-snapshot.ps1'
$prepare = Join-Path $PSScriptRoot 'test-speaker-tone.ps1'
$wav = Join-Path $PSScriptRoot 'acx-test-tone.wav'
$prepared = $false
$errors = @()
try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $prepare -LeavePrepared
    if ($LASTEXITCODE -ne 0) { throw 'SOF pipeline preparation failed' }
    $prepared = $true

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
    if ($prepared) {
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
    if (Test-Path $wav) { Remove-Item $wav -Force }
    if ($errors.Count -ne 0) { throw ($errors -join '; ') }
}
