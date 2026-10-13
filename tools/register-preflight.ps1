$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$root = Join-Path $env:USERPROFILE 'sofprobe-register-test'
$expected = @{
    'sofprobe.sys' = '1929C872C9F478096A4F0694FA16156E3237F3EA29631A492A6DEE57BFAE9331'
    'sofprobe.inf' = '51D2E14672FF1D509154BD5A351B80FEF0A4D4912E01C90A3ED3A58600858E04'
    'sofprobe.cat' = 'FBE0683E8973476A9371B281D64E63048D88B43CE7ED5020BF770D58B1370427'
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
