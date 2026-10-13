# SPDX-License-Identifier: GPL-2.0-only
$ErrorActionPreference = 'Stop'
$interesting = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match '^ACPI\\(10EC|ESSX|MX|NAU|INTC|CSC|CS|RT|DLGS|AMDI)' -or
    $_.Class -eq 'MEDIA' -or
    $_.InstanceId -match '^PCI\\VEN_8086&DEV_54C8'
}
$interesting | Select-Object Status, Class, FriendlyName, InstanceId |
    Sort-Object InstanceId | Format-Table -Wrap -AutoSize
