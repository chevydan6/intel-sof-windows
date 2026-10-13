$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$thumbprint = 'A5D29821CEA710677446ABFF98239CFCD81885B0'
$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$provider = (Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_DriverProvider' -ErrorAction SilentlyContinue).Data
$infName = (Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_DriverInfPath' -ErrorAction SilentlyContinue).Data
if ($provider -eq 'Open SOF Windows Project' -and $infName -match '^oem\d+\.inf$') {
    pnputil /delete-driver $infName /uninstall | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Could not remove $infName" }
}
$key = "HKLM:\SYSTEM\CurrentControlSet\Enum\$instance\Device Parameters"
foreach ($name in @('SofProbeGcap', 'SofProbeVmin', 'SofProbeVmaj',
                     'SofProbeGctl', 'SofProbeStatests', 'SofProbeGcap2',
                     'SofProbeLlch')) {
    Remove-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue
}

bcdedit /deletevalue '{current}' testsigning | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Could not restore test-signing boot setting' }
Remove-Item "Cert:\LocalMachine\TrustedPublisher\$thumbprint" -ErrorAction SilentlyContinue
Remove-Item "Cert:\LocalMachine\Root\$thumbprint" -ErrorAction SilentlyContinue
Write-Host 'Probe package and test trust removed; reboot to leave test mode.'
