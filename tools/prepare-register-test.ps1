$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$expectedSys = '1929C872C9F478096A4F0694FA16156E3237F3EA29631A492A6DEE57BFAE9331'
$expectedInf = '51D2E14672FF1D509154BD5A351B80FEF0A4D4912E01C90A3ED3A58600858E04'
$expectedCat = 'FBE0683E8973476A9371B281D64E63048D88B43CE7ED5020BF770D58B1370427'
$expectedCert = 'A5D29821CEA710677446ABFF98239CFCD81885B0'
$root = Join-Path $env:USERPROFILE 'sofprobe-register-test'
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
$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$device = @(Get-PnpDevice | Where-Object InstanceId -EQ $instance)
$driver = Get-CimInstance Win32_PnPSignedDriver | Where-Object DeviceID -EQ $instance
$details = pnputil /enum-devices /instanceid $instance /drivers | Out-String
if ($device.Count -ne 1 -or $driver.DriverProviderName -or
    $details -notmatch '(?im)^Status:\s+Stopped\s*$') {
    throw 'Unexpected controller binding or state'
}

$sysSignature = Get-AuthenticodeSignature $sys
$catSignature = Get-AuthenticodeSignature $cat
$cert = $sysSignature.SignerCertificate
if ($cert.Thumbprint -ne $expectedCert -or
    $catSignature.SignerCertificate.Thumbprint -ne $expectedCert -or
    $cert.Subject -ne $cert.Issuer -or
    $cert.NotAfter -lt (Get-Date)) {
    throw 'Unexpected or expired signing certificate'
}

$backup = Join-Path $root 'bcd-before-register-probe.bak'
if (Test-Path $backup) { throw "Existing BCD backup: $backup" }
bcdedit /export $backup | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'BCD export failed' }
$cer = Join-Path $root 'register-probe-test-signer.cer'
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
