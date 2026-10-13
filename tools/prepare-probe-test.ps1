$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$expectedSys = 'DCDC50755292ECEE3D1B1ECB4FE342EBA202CFE1BFB1E14A527C628EA79B4885'
$expectedInf = 'B13B38276CE8B34B40AD9383665D617E07962D076F6B6E62C25C955BB19D157E'
$expectedCat = 'F0AA05564DE22C56A2F893548508CBD2473E6670E18865BF39E9BB1AD45EE4FF'
$expectedCert = '67B15485B1D3C680B09B6990CC689C35213D4939'
$root = Join-Path $env:USERPROFILE 'sofprobe-test'
$sys = Join-Path $root 'sofprobe.sys'
$inf = Join-Path $root 'sofprobe.inf'
$cat = Join-Path $root 'sofprobe.cat'

foreach ($item in @(@($sys, $expectedSys), @($inf, $expectedInf), @($cat, $expectedCat))) {
    if ((Get-FileHash $item[0] -Algorithm SHA256).Hash -ne $item[1]) {
        throw "Package hash mismatch: $($item[0])"
    }
}
if (Confirm-SecureBootUEFI) { throw 'Secure Boot is enabled' }
if ((Get-BitLockerVolume -MountPoint 'C:').ProtectionStatus -ne 'Off') { throw 'BitLocker is enabled' }
$device = @(Get-PnpDevice | Where-Object InstanceId -Like 'PCI\VEN_8086&DEV_54C8*')
if ($device.Count -ne 1 -or $device[0].Status -ne 'Error') { throw 'Unexpected controller state' }

$sysSignature = Get-AuthenticodeSignature $sys
$catSignature = Get-AuthenticodeSignature $cat
$cert = $sysSignature.SignerCertificate
if ($cert.Thumbprint -ne $expectedCert -or
    $catSignature.SignerCertificate.Thumbprint -ne $expectedCert -or
    $cert.Subject -ne $cert.Issuer -or
    $cert.NotAfter -lt (Get-Date)) {
    throw 'Unexpected or expired signing certificate'
}

$backup = Join-Path $root 'bcd-before-sofprobe.bak'
if (Test-Path $backup) { throw "Existing BCD backup: $backup" }
bcdedit /export $backup | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'BCD export failed' }
$cer = Join-Path $root 'sofprobe-test-signer.cer'
[IO.File]::WriteAllBytes($cer, $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))

try {
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
    $sysStatus = (Get-AuthenticodeSignature $sys).Status
    $catStatus = (Get-AuthenticodeSignature $cat).Status
    if ($sysStatus -ne 'Valid' -or $catStatus -ne 'Valid') {
        throw "Signatures remain invalid: sys=$sysStatus cat=$catStatus"
    }
    bcdedit /set '{current}' testsigning on | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Could not enable test signing' }
    Write-Host "Prepared: BCD backup=$backup; signer=$expectedCert; sys=$sysStatus cat=$catStatus"
} catch {
    bcdedit /deletevalue '{current}' testsigning 2>$null | Out-Null
    Remove-Item "Cert:\LocalMachine\TrustedPublisher\$expectedCert" -ErrorAction SilentlyContinue
    Remove-Item "Cert:\LocalMachine\Root\$expectedCert" -ErrorAction SilentlyContinue
    throw
}
