$ErrorActionPreference = 'Stop'
$root = Join-Path $env:USERPROFILE 'sofprobe-test'
$sys = Join-Path $root 'sofprobe.sys'
$inf = Join-Path $root 'sofprobe.inf'
$cat = Join-Path $root 'sofprobe.cat'
$device = Get-PnpDevice | Where-Object InstanceId -Like 'PCI\VEN_8086&DEV_54C8*'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$admin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

foreach ($path in @($sys, $inf, $cat)) {
    if (-not (Test-Path $path)) { throw "Missing package file: $path" }
}

$sysSignature = Get-AuthenticodeSignature $sys
$catSignature = Get-AuthenticodeSignature $cat
[pscustomobject]@{
    IsAdministrator = $admin
    DeviceInstanceId = $device.InstanceId
    DeviceStatus = $device.Status
    DeviceProblem = $device.Problem
    DriverProvider = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_DriverProvider' -ErrorAction SilentlyContinue).Data
    SysSha256 = (Get-FileHash $sys -Algorithm SHA256).Hash
    InfSha256 = (Get-FileHash $inf -Algorithm SHA256).Hash
    CatSha256 = (Get-FileHash $cat -Algorithm SHA256).Hash
    SysSignatureStatus = $sysSignature.Status.ToString()
    SysSigner = $sysSignature.SignerCertificate.Subject
    SysSignerThumbprint = $sysSignature.SignerCertificate.Thumbprint
    SysSignerIssuer = $sysSignature.SignerCertificate.Issuer
    SysSignerValidFrom = $sysSignature.SignerCertificate.NotBefore
    SysSignerValidUntil = $sysSignature.SignerCertificate.NotAfter
    CatSignatureStatus = $catSignature.Status.ToString()
    CatSigner = $catSignature.SignerCertificate.Subject
    CatSignerThumbprint = $catSignature.SignerCertificate.Thumbprint
    SecureBoot = (Confirm-SecureBootUEFI -ErrorAction SilentlyContinue)
    BitLocker = (Get-BitLockerVolume -MountPoint 'C:' -ErrorAction SilentlyContinue).ProtectionStatus
} | Format-List

bcdedit /enum '{current}'
pnputil /enum-drivers /class System
