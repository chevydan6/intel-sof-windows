$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$key = "HKLM:\SYSTEM\CurrentControlSet\Enum\$instance"
pnputil /enum-devices /instanceid $instance /drivers /resources
sc.exe query sofprobe
Get-ItemProperty -LiteralPath $key | Format-List *
Get-PnpDeviceProperty -InstanceId $instance |
    Where-Object KeyName -Match 'Driver|Service|Problem' |
    Select-Object KeyName, Data | Format-Table -AutoSize
