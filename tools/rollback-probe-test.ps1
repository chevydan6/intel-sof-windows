$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$thumbprint = '67B15485B1D3C680B09B6990CC689C35213D4939'
$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$driver = Get-CimInstance Win32_PnPSignedDriver |
    Where-Object { $_.DeviceID -eq $instance -and $_.InfName -match '^oem\d+\.inf$' }
if ($driver -and $driver.DriverProviderName -eq 'Open SOF Windows Project') {
    pnputil /delete-driver $driver.InfName /uninstall | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Could not remove $($driver.InfName)" }
}

bcdedit /deletevalue '{current}' testsigning | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Could not restore test-signing boot setting' }
Remove-Item "Cert:\LocalMachine\TrustedPublisher\$thumbprint" -ErrorAction SilentlyContinue
Remove-Item "Cert:\LocalMachine\Root\$thumbprint" -ErrorAction SilentlyContinue
Write-Host 'Probe package and test trust removed; reboot to leave test mode.'
