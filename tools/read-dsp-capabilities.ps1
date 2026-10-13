$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$provider = (Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_DriverProvider').Data
if ($provider -ne 'Open SOF Windows Project') { throw 'The controller is not bound to our probe' }
$key = "HKLM:\SYSTEM\CurrentControlSet\Enum\$instance\Device Parameters"
$values = Get-ItemProperty -LiteralPath $key
$names = @('SofProbeGcap', 'SofProbeVmin', 'SofProbeVmaj',
           'SofProbeGctl', 'SofProbeStatests', 'SofProbeGcap2',
           'SofProbeLlch', 'SofProbeCapOffset', 'SofProbeCapHeader',
           'SofProbeAdspcs', 'SofProbeAdspis', 'SofProbeHipct',
           'SofProbeHipci')
foreach ($name in $names) {
    if ($null -eq $values.$name) { throw "Missing register value: $name" }
    '{0} = 0x{1:X8} ({1})' -f $name, [uint32]$values.$name
}
