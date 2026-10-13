# Read-only diagnostics for the live ACX KS filter. No stream is opened.
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class KsProbeNative {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern SafeFileHandle CreateFile(string name, uint access, uint share,
    IntPtr security, uint creation, uint flags, IntPtr template);
  [DllImport("kernel32.dll", SetLastError=true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool DeviceIoControl(SafeFileHandle device, uint code,
    byte[] input, int inputLength, byte[] output, int outputLength,
    out int returned, IntPtr overlapped);
}
'@

$interface = '\\?\PCI#VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00#3&11583659&0&FB#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\Speaker0'
$handle = [KsProbeNative]::CreateFile($interface, 0, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
if ($handle.IsInvalid) { throw "CreateFile failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
try {
  $set = [Guid]::Parse('8c134960-51ad-11cf-878a-94f801c10000').ToByteArray()
  foreach ($pinId in 0, 1, 2) {
    $ranges = $null
    foreach ($prop in @(@('CTYPES', 1), @('DATAFLOW', 2), @('DATARANGES', 3), @('COMMUNICATION', 7), @('CATEGORY', 11), @('NAME', 12), @('PROPOSEDATAFORMAT', 14), @('PROPOSEDATAFORMAT2', 15), @('MODEDATAFORMATS', 16))) {
      $input = New-Object byte[] 32
      [Array]::Copy($set, $input, 16)
      [BitConverter]::GetBytes([uint32]$prop[1]).CopyTo($input, 16)
      [BitConverter]::GetBytes([uint32]1).CopyTo($input, 20)
      [BitConverter]::GetBytes([uint32]$pinId).CopyTo($input, 24)
      $out = New-Object byte[] 4096
      $returned = 0
      $ok = [KsProbeNative]::DeviceIoControl($handle, 0x2f0003, $input, $input.Length,
        $out, $out.Length, [ref]$returned, [IntPtr]::Zero)
      $errorCode = if ($ok) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
      $hex = if ($returned -gt 0) { [BitConverter]::ToString($out, 0, [Math]::Min($returned, 128)) } else { '' }
      Write-Output "pin=$pinId prop=$($prop[0]) ok=$ok error=$errorCode bytes=$returned data=$hex"
      if ($pinId -eq 0 -and $prop[0] -eq 'DATARANGES' -and $ok) {
        $ranges = New-Object byte[] $returned
        [Array]::Copy($out, $ranges, $returned)
      }
    }
    if ($pinId -eq 0 -and $null -ne $ranges) {
      foreach ($mode in @(@('RAW', '9e90ea20-b493-4fd1-a1a8-7e1361a956cf'), @('DEFAULT', 'c18e2f7e-933d-4965-b7d1-1eef228d2af3'))) {
        $input = New-Object byte[] 48
        [Array]::Copy($set, $input, 16)
        [BitConverter]::GetBytes([uint32]16).CopyTo($input, 16)
        [BitConverter]::GetBytes([uint32]1).CopyTo($input, 20)
        [Guid]::Parse($mode[1]).ToByteArray().CopyTo($input, 32)
        $out = New-Object byte[] 4096
        $returned = 0
        $ok = [KsProbeNative]::DeviceIoControl($handle, 0x2f0003, $input, $input.Length,
          $out, $out.Length, [ref]$returned, [IntPtr]::Zero)
        $errorCode = if ($ok) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
        $hex = if ($returned -gt 0) { [BitConverter]::ToString($out, 0, [Math]::Min($returned, 128)) } else { '' }
        Write-Output "pin=$pinId prop=MODEDATAFORMATS mode=$($mode[0]) ok=$ok error=$errorCode bytes=$returned data=$hex"
        $input = New-Object byte[] 80
        [Array]::Copy($set, $input, 16)
        [BitConverter]::GetBytes([uint32]15).CopyTo($input, 16)
        [BitConverter]::GetBytes([uint32]1).CopyTo($input, 20)
        [BitConverter]::GetBytes([uint32]48).CopyTo($input, 32)
        [BitConverter]::GetBytes([uint32]1).CopyTo($input, 36)
        [BitConverter]::GetBytes([uint32]40).CopyTo($input, 40)
        [Guid]::Parse('e1f89eb5-5f46-419b-967b-ff6770b98401').ToByteArray().CopyTo($input, 48)
        [Guid]::Parse($mode[1]).ToByteArray().CopyTo($input, 64)
        $out = New-Object byte[] 256
        $returned = 0
        $ok = [KsProbeNative]::DeviceIoControl($handle, 0x2f0003, $input, $input.Length,
          $out, $out.Length, [ref]$returned, [IntPtr]::Zero)
        $errorCode = if ($ok) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
        $hex = if ($returned -gt 0) { [BitConverter]::ToString($out, 0, [Math]::Min($returned, 128)) } else { '' }
        Write-Output "pin=$pinId prop=PROPOSEDATAFORMAT2 mode=$($mode[0]) ok=$ok error=$errorCode bytes=$returned data=$hex"
      }
      $input = New-Object byte[] (32 + $ranges.Length)
      [Array]::Copy($set, $input, 16)
      [BitConverter]::GetBytes([uint32]4).CopyTo($input, 16)
      [BitConverter]::GetBytes([uint32]1).CopyTo($input, 20)
      [BitConverter]::GetBytes([uint32]$pinId).CopyTo($input, 24)
      [Array]::Copy($ranges, 0, $input, 32, $ranges.Length)
      $out = New-Object byte[] 256
      $returned = 0
      $ok = [KsProbeNative]::DeviceIoControl($handle, 0x2f0003, $input, $input.Length,
        $out, $out.Length, [ref]$returned, [IntPtr]::Zero)
      $errorCode = if ($ok) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
      $hex = if ($returned -gt 0) { [BitConverter]::ToString($out, 0, [Math]::Min($returned, 128)) } else { '' }
      Write-Output "pin=$pinId prop=DATAINTERSECTION ok=$ok error=$errorCode bytes=$returned data=$hex"
      foreach ($propertyId in 4, 14, 15, 16) {
        $basic = New-Object byte[] 32
        [Array]::Copy($set, $basic, 16)
        [BitConverter]::GetBytes([uint32]$propertyId).CopyTo($basic, 16)
        [BitConverter]::GetBytes([uint32]0x200).CopyTo($basic, 20)
        $out = New-Object byte[] 256
        $returned = 0
        $ok = [KsProbeNative]::DeviceIoControl($handle, 0x2f0003, $basic, $basic.Length,
          $out, $out.Length, [ref]$returned, [IntPtr]::Zero)
        $errorCode = if ($ok) { 0 } else { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
        $hex = if ($returned -gt 0) { [BitConverter]::ToString($out, 0, [Math]::Min($returned, 32)) } else { '' }
        Write-Output "pin=$pinId prop=$propertyId BASICSUPPORT ok=$ok error=$errorCode bytes=$returned data=$hex"
      }
    }
  }
} finally { $handle.Dispose() }
