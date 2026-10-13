$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$root = Join-Path $env:USERPROFILE 'sofprobe-chain-test'
$expected = @{
    'sofprobe.sys' = '6C092AF766F189314F71C064840C773BD8F68125574CFA6167EBE53B4261CE86'
    'sofprobe.inf' = '915510E6423C9EED71071730F7E5F5B60E54C4DE64A48ACFE5459FAEF8DAE35F'
    'sofprobe.cat' = '93AB0BDE7DEFC2872EFAB33ECC422EA0AACAA6EF0449FF1C01D2537D22D72125'
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
