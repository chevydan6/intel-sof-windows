$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
Write-Host 'PnP details:'
pnputil /enum-devices /instanceid $instance /drivers /resources
Write-Host 'Selected device properties:'
Get-PnpDeviceProperty -InstanceId $instance |
    Where-Object KeyName -Match 'Driver|Service|Problem|Status|Class' |
    Select-Object KeyName, Data | Format-Table -AutoSize
Write-Host 'Probe service:'
sc.exe query sofprobe
Write-Host 'Installed driver details:'
Get-CimInstance Win32_PnPSignedDriver |
    Where-Object DeviceID -EQ $instance |
    Select-Object DeviceName, DeviceID, InfName, DriverProviderName, DriverVersion, IsSigned | Format-List
