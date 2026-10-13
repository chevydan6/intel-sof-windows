# SPDX-License-Identifier: GPL-2.0-only
# Test an automatically initialized driver without preparing firmware or
# stopping/restarting Windows audio services. A child player has a time limit.
param([ValidateRange(1,10)][int]$Repeat = 2, [string]$PlayFile,
      [switch]$RequireNewStream)
$ErrorActionPreference = 'Stop'
if ($PlayFile) {
    $player = [System.Media.SoundPlayer]::new($PlayFile)
    try { $player.PlaySync() } finally { $player.Dispose() }
    exit 0
}
$client = Join-Path $PSScriptRoot 'read-live-snapshot.ps1'
function Read-Status {
    $text = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $client -AcxStatus | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'ACX status query failed' }
    Write-Host $text
    $result = @{}
    foreach ($field in @('StartupCount','StartupStage','HardwareReady','RunCalls',
                         'PacketsSubmitted','PacketsCompleted','CreateAttempts','PcmResets','StartPosition')) {
        if ($text -notmatch "(?m)^$field\s*:\s*(\d+)") { throw "Missing $field in ACX status" }
        $result[$field] = [uint64]$Matches[1]
    }
    if ($result.HardwareReady -ne 1 -or $result.StartupStage -ne 6 -or
        $text -notmatch '(?m)^StartupStatus\s*:\s*0x00000000\s*$') {
        throw 'Driver did not initialize firmware and speaker hardware automatically'
    }
    return $result
}
$before = Read-Status
$wav = Join-Path $env:TEMP ('sofplay-' + [guid]::NewGuid().ToString('N') + '.wav')
try {
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
            $sample = [int16]([Math]::Round([Math]::Sin(2 * [Math]::PI * 440 * $i / $rate) * 3000))
            $writer.Write($sample)
            $writer.Write($sample)
        }
        $writer.Flush()
    } finally { $stream.Dispose() }
    for ($n = 1; $n -le $Repeat; $n++) {
        Write-Host "Playback $n/$Repeat : three-second Windows WAV, no hardware preparation"
        $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -PlayFile "' + $wav + '"'
        $info = [Diagnostics.ProcessStartInfo]::new('powershell.exe', $arguments)
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $info
        if (!$process.Start()) { throw 'Could not start the WAV player' }
        try {
            if (!$process.WaitForExit(15000)) {
                $process.Kill()
                $process.WaitForExit()
                throw 'Windows playback exceeded the 15-second deadline'
            }
            if ($process.ExitCode -ne 0) { throw "Player failed: $($process.ExitCode)" }
        } finally { $process.Dispose() }
        $after = Read-Status
        if ($after.StartPosition -ne 0 -or
            $after.StartupCount -ne $before.StartupCount -or
            $after.RunCalls -lt $before.RunCalls -or
            ($RequireNewStream -and $after.RunCalls -eq $before.RunCalls) -or
            $after.PacketsSubmitted - $before.PacketsSubmitted -lt 250 -or
            $after.PacketsCompleted - $before.PacketsCompleted -lt 250) {
            throw 'Playback did not advance the expected driver/DMA counters'
        }
        if ($after.RunCalls -eq $before.RunCalls) {
            Write-Host 'Windows reused an existing shared stream; counters include other active audio clients.'
        }
        Write-Host "Playback $n completed with DMA progress. Audible confirmation is separate."
        $before = $after
        Start-Sleep -Seconds 1
    }
} finally {
    if (Test-Path $wav) { Remove-Item $wav -Force }
}
