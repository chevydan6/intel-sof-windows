$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$root = Join-Path $env:USERPROFILE 'sofprobe-dsp-test'
$expected = @{
    'sofprobe.sys' = '9E2984EB7066C4EDC3E80931B8E31DAFB612DC497A85ED61D1173047D411B3CF'
    'sofprobe.inf' = '361EB272FFF3F549D1214D9B1954531F390296B60C455B0023480BDEB0A694D1'
    'sofprobe.cat' = '0CFAAC2A0995EEAA1ED2EC77BB5D3C73CAE49E2FBE7943A963C8F4A404268D6E'
}
foreach ($name in $expected.Keys) {
    $path = Join-Path $root $name
    $actual = (Get-FileHash $path -Algorithm SHA256).Hash
    if ($actual -ne $expected[$name]) { throw "Hash mismatch: $path" }
    Write-Host "$name SHA256 $actual"
}
$sys = Get-AuthenticodeSignature (Join-Path $root 'sofprobe.sys')
$cat = Get-AuthenticodeSignature (Join-Path $root 'sofprobe.cat')
[pscustomobject]@{
    SysSigner = $sys.SignerCertificate.Subject
    SysThumbprint = $sys.SignerCertificate.Thumbprint
    SysSignature = $sys.Status
    CatThumbprint = $cat.SignerCertificate.Thumbprint
    CatSignature = $cat.Status
    SecureBoot = (Confirm-SecureBootUEFI -ErrorAction SilentlyContinue)
    BitLocker = (Get-BitLockerVolume -MountPoint 'C:').ProtectionStatus
    BootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
} | Format-List
