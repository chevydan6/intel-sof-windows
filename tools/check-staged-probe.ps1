$ErrorActionPreference = 'Stop'
$package = Join-Path $env:USERPROFILE 'sofprobe-development.zip'
$probe = Join-Path $env:USERPROFILE 'sofprobe-test\sofprobe.sys'

if (-not (Test-Path $package)) { throw "Missing staged package: $package" }
if (-not (Test-Path $probe)) { throw "Missing extracted driver: $probe" }

$device = Get-PnpDevice | Where-Object InstanceId -Like 'PCI\VEN_8086&DEV_54C8*'
[pscustomobject]@{
    PackageSha256 = (Get-FileHash $package -Algorithm SHA256).Hash
    ProbeSha256 = (Get-FileHash $probe -Algorithm SHA256).Hash
    SignatureStatus = (Get-AuthenticodeSignature $probe).Status.ToString()
    ControllerInstanceId = $device.InstanceId
    ControllerStatus = $device.Status
    TestSigningEnabled = ((bcdedit /enum '{current}' | Out-String) -match '(?im)^testsigning\s+Yes\s*$')
} | Format-List
