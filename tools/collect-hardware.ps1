# SPDX-License-Identifier: GPL-2.0-only
<#
.SYNOPSIS
Collects read-only Windows audio hardware and build-tool information.
.PARAMETER OutputPath
JSON file to write. Defaults to n100-hardware.json in the current directory.
#>
param([string]$OutputPath = 'n100-hardware.json')

$audioIds = @('PCI\VEN_8086&DEV_54C8', 'ACPI\10EC5682')
$devices = foreach ($prefix in $audioIds) {
    Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId.StartsWith($prefix) } |
        ForEach-Object {
            $device = $_
            $properties = Get-PnpDeviceProperty -InstanceId $device.InstanceId -ErrorAction SilentlyContinue
            [ordered]@{
                instance_id = $device.InstanceId
                friendly_name = $device.FriendlyName
                status = $device.Status
                problem_code = ($properties | Where-Object KeyName -eq 'DEVPKEY_Device_ProblemCode' |
                                Select-Object -First 1 -ExpandProperty Data)
                hardware_ids = ($properties | Where-Object KeyName -eq 'DEVPKEY_Device_HardwareIds' |
                                Select-Object -First 1 -ExpandProperty Data)
                location_paths = ($properties | Where-Object KeyName -eq 'DEVPKEY_Device_LocationPaths' |
                                  Select-Object -First 1 -ExpandProperty Data)
            }
        }
}

$result = [ordered]@{
    collected_at = (Get-Date).ToString('o')
    cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
    windows_version = [System.Environment]::OSVersion.Version.ToString()
    devices = @($devices)
    wdk_include_present = Test-Path 'C:\Program Files (x86)\Windows Kits\10\Include'
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "Wrote $OutputPath"
