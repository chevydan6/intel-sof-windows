# SPDX-License-Identifier: GPL-2.0-only
# Read-only firmware table export for microphone topology identification.
param([ValidatePattern('^[A-Z0-9_]{4}$')][string]$Table='NHLT', [Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AcpiTableReader {
 [DllImport("kernel32.dll", SetLastError=true)]
 public static extern uint GetSystemFirmwareTable(uint provider,uint table,byte[] buffer,uint size);
}
'@
$provider=[uint32]0x41435049
$id=[BitConverter]::ToUInt32([Text.Encoding]::ASCII.GetBytes($Table),0)
$length=[AcpiTableReader]::GetSystemFirmwareTable($provider,$id,$null,0)
if ($length -lt 36 -or $length -gt 1048576) { throw "Table unavailable or unexpected size: $length, Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
$bytes=[byte[]]::new($length)
if ([AcpiTableReader]::GetSystemFirmwareTable($provider,$id,$bytes,$length) -ne $length) { throw 'Firmware table read failed' }
if ([Text.Encoding]::ASCII.GetString($bytes,0,4) -ne $Table -or [BitConverter]::ToUInt32($bytes,4) -ne $length) { throw 'Firmware table header mismatch' }
$sum=0;foreach($b in $bytes){$sum=($sum+$b)-band 255};if($sum -ne 0){throw 'ACPI checksum mismatch'}
[IO.File]::WriteAllBytes($Output,$bytes)
[pscustomobject]@{Table=$Table;Bytes=$length;Sha256=(Get-FileHash $Output -Algorithm SHA256).Hash;Output=$Output}|Format-List
