$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$expectedSys = '9E2984EB7066C4EDC3E80931B8E31DAFB612DC497A85ED61D1173047D411B3CF'
$expectedInf = '361EB272FFF3F549D1214D9B1954531F390296B60C455B0023480BDEB0A694D1'
$expectedCat = '0CFAAC2A0995EEAA1ED2EC77BB5D3C73CAE49E2FBE7943A963C8F4A404268D6E'
$expectedCert = '0B102F40EA8F53AABA252CF9D09AD70797E84E98'
$root = Join-Path $env:USERPROFILE 'sofprobe-dsp-test'
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

$backup = Join-Path $root 'bcd-before-dsp-probe.bak'
if (Test-Path $backup) { throw "Existing BCD backup: $backup" }
bcdedit /export $backup | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'BCD export failed' }
$cer = Join-Path $root 'dsp-probe-test-signer.cer'
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
