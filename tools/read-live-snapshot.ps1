# SPDX-License-Identifier: GPL-2.0-only
param([switch]$PulseCore0)
$ErrorActionPreference = 'Stop'
$source = @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class SofProbeClient {
    [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode)]
    private static extern uint CM_Get_Device_Interface_List_SizeW(out uint length, ref Guid classGuid, string deviceId, uint flags);
    [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode)]
    private static extern uint CM_Get_Device_Interface_ListW(ref Guid classGuid, string deviceId, [Out] char[] buffer, uint length, uint flags);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool DeviceIoControl(SafeFileHandle handle, uint code, IntPtr input, uint inputSize, [Out] byte[] output, uint outputSize, out uint returned, IntPtr overlapped);
    public static string[] Paths() {
        Guid guid = new Guid("F6B7AB70-381C-4A67-A85C-4A006A7B01DD");
        uint length;
        uint result = CM_Get_Device_Interface_List_SizeW(out length, ref guid, null, 0);
        if (result != 0) throw new Exception("Interface list size: CONFIGRET " + result);
        char[] buffer = new char[length];
        result = CM_Get_Device_Interface_ListW(ref guid, null, buffer, length, 0);
        if (result != 0) throw new Exception("Interface list: CONFIGRET " + result);
        return new string(buffer).Split(new char[]{'\0'}, StringSplitOptions.RemoveEmptyEntries);
    }
    public static uint[] Exchange(string path, bool pulse) {
        using (SafeFileHandle handle = CreateFileW(path, pulse ? 0x40000000 : 0x80000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes = new byte[32];
            uint returned;
            if (!DeviceIoControl(handle, pulse ? 0x22A004U : 0x226000U, IntPtr.Zero, 0, bytes, (uint)bytes.Length, out returned, IntPtr.Zero))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if (returned != bytes.Length) throw new Exception("Unexpected snapshot length: " + returned);
            uint[] words = new uint[8];
            for (int i=0; i<words.Length; i++) words[i] = BitConverter.ToUInt32(bytes, i*4);
            return words;
        }
    }
}
'@
Add-Type -TypeDefinition $source
$paths = @( [SofProbeClient]::Paths() )
if ($paths.Count -ne 1) { throw "Expected one active SOF probe interface; got $($paths.Count): $($paths -join ', ')" }
$values = [SofProbeClient]::Exchange($paths[0], $PulseCore0.IsPresent)
if ($values[0] -ne 1) { throw "Unsupported snapshot version: $($values[0])" }
if ($PulseCore0) {
    [pscustomobject]@{
        Version = $values[0]
        Before = ('0x{0:X8}' -f $values[1])
        Requested = ('0x{0:X8}' -f $values[2])
        Powered = ('0x{0:X8}' -f $values[3])
        Restored = ('0x{0:X8}' -f $values[4])
        UpPolls = $values[5]
        DownPolls = $values[6]
        Result = $values[7]
    } | Format-List
    if ($values[7] -ne 0 -or $values[4] -ne $values[1]) {
        throw 'The DSP power pulse did not restore the observed baseline'
    }
    return
}
[pscustomobject]@{
    Interface = $paths[0]
    Version = $values[0]
    Gctl = ('0x{0:X8}' -f $values[1])
    Ppctl = ('0x{0:X8}' -f $values[2])
    Ppsts = ('0x{0:X8}' -f $values[3])
    Adspcs = ('0x{0:X8}' -f $values[4])
    Adspis = ('0x{0:X8}' -f $values[5])
    Hipct = ('0x{0:X8}' -f $values[6])
    Hipci = ('0x{0:X8}' -f $values[7])
} | Format-List
