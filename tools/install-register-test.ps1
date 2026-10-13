$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$root = Join-Path $env:USERPROFILE 'sofprobe-register-test'
$inf = Join-Path $root 'sofprobe.inf'
$sys = Join-Path $root 'sofprobe.sys'
$cat = Join-Path $root 'sofprobe.cat'
$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$boot = bcdedit /enum '{current}' | Out-String
if ($LASTEXITCODE -ne 0 -or $boot -notmatch '(?im)^testsigning\s+Yes\s*$') {
    throw 'Test signing is not enabled on the current boot entry'
}
foreach ($file in @($sys, $cat)) {
    $signature = Get-AuthenticodeSignature $file
    if ($signature.Status -ne 'Valid') { throw "Invalid signature on $file`: $($signature.Status)" }
}
$before = Get-PnpDevice -InstanceId $instance
$beforeProvider = (Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_DriverProvider' -ErrorAction SilentlyContinue).Data
if ($beforeProvider) { throw "Controller already has a driver: $beforeProvider" }

Write-Host "Boot time: $((Get-CimInstance Win32_OperatingSystem).LastBootUpTime)"
Write-Host "Before: status=$($before.Status) problem=$($before.Problem)"
pnputil /add-driver $inf /install | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Driver installation failed with code $LASTEXITCODE" }
Start-Sleep -Seconds 4
$after = Get-PnpDevice -InstanceId $instance
$provider = (Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_DriverProvider').Data
Write-Host "After: status=$($after.Status) problem=$($after.Problem)"
Write-Host "Driver provider: $provider"
pnputil /enum-devices /instanceid $instance /resources | Out-Host
if ($after.Status -ne 'OK') { throw 'Probe did not start successfully' }
if ($provider -ne 'Open SOF Windows Project') {
    throw 'Controller is not bound to the register probe'
}
