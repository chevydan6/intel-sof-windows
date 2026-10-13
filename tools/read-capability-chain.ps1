$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$provider = (Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_DriverProvider').Data
if ($provider -ne 'Open SOF Windows Project') { throw 'The controller is not bound to our probe' }
$key = "HKLM:\SYSTEM\CurrentControlSet\Enum\$instance\Device Parameters"
$values = Get-ItemProperty -LiteralPath $key
$count = [int]$values.SofProbeCapCount
if ($count -lt 1 -or $count -gt 10) { throw "Invalid capability count: $count" }
Write-Host "Capability count: $count"
for ($i = 0; $i -lt $count; $i++) {
    $offset = [uint32]$values."SofProbeCap${i}Offset"
    $header = [uint32]$values."SofProbeCap${i}Header"
    $id = ($header -shr 16) -band 0xfff
    $version = $header -shr 28
    $next = $header -band 0xffff
    'cap {0}: offset=0x{1:X4} header=0x{2:X8} id={3} version={4} next=0x{5:X4}' -f $i, $offset, $header, $id, $version, $next
}
foreach ($name in @('SofProbePpFound', 'SofProbePpCtl', 'SofProbePpSts',
                     'SofProbeAdspcs', 'SofProbeAdspis',
                     'SofProbeHipct', 'SofProbeHipci')) {
    if ($null -eq $values.$name) { throw "Missing value: $name" }
    '{0} = 0x{1:X8}' -f $name, [uint32]$values.$name
}
