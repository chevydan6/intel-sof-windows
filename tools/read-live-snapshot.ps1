# SPDX-License-Identifier: GPL-2.0-only
param([switch]$PulseCore0, [switch]$TestDmaAllocation,
      [string]$StageFirmware, [switch]$ReadStream0,
      [switch]$TestStreamSetup, [switch]$ReadBootPreflight,
      [switch]$TestCaptureSetup, [switch]$ReadPciConfig,
      [switch]$TestCaptureDma)
$ErrorActionPreference = 'Stop'
$selected = 0
if ($PulseCore0) { $selected++ }
if ($TestDmaAllocation) { $selected++ }
if ($StageFirmware) { $selected++ }
if ($ReadStream0) { $selected++ }
if ($TestStreamSetup) { $selected++ }
if ($ReadBootPreflight) { $selected++ }
if ($TestCaptureSetup) { $selected++ }
if ($ReadPciConfig) { $selected++ }
if ($TestCaptureDma) { $selected++ }
if ($selected -gt 1) { throw 'Select only one diagnostic command' }
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
    [DllImport("kernel32.dll", EntryPoint="DeviceIoControl", SetLastError=true)]
    private static extern bool DeviceIoControlWithBuffer(SafeFileHandle handle, uint code, [In] byte[] input, uint inputSize, [Out] byte[] output, uint outputSize, out uint returned, IntPtr overlapped);
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
    public static uint[] Exchange(string path, uint code, byte[] input) {
        using (SafeFileHandle handle = CreateFileW(path, (code == 0x22A004U || code == 0x22A00CU || code == 0x22A014U || code == 0x22A01CU || code == 0x22A024U) ? 0x40000000 : 0x80000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes = new byte[code == 0x22A024U ? 80 : ((code == 0x22A014U || code == 0x226018U || code == 0x22A01CU) ? 64 : 32)];
            uint returned;
            bool ok = input == null
                ? DeviceIoControl(handle, code, IntPtr.Zero, 0, bytes, (uint)bytes.Length, out returned, IntPtr.Zero)
                : DeviceIoControlWithBuffer(handle, code, input, (uint)input.Length, bytes, (uint)bytes.Length, out returned, IntPtr.Zero);
            if (!ok)
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if (returned != bytes.Length) throw new Exception("Unexpected snapshot length: " + returned);
            uint[] words = new uint[bytes.Length / 4];
            for (int i=0; i<words.Length; i++) words[i] = BitConverter.ToUInt32(bytes, i*4);
            return words;
        }
    }
}
'@
Add-Type -TypeDefinition $source
$paths = @( [SofProbeClient]::Paths() )
if ($paths.Count -ne 1) { throw "Expected one active SOF probe interface; got $($paths.Count): $($paths -join ', ')" }
$code = if ($PulseCore0) { [uint32]0x22A004 } elseif ($TestDmaAllocation) { [uint32]0x226008 } elseif ($StageFirmware) { [uint32]0x22A00C } elseif ($ReadStream0) { [uint32]0x226010 } elseif ($TestStreamSetup) { [uint32]0x22A014 } elseif ($ReadBootPreflight) { [uint32]0x226018 } elseif ($TestCaptureSetup) { [uint32]0x22A01C } elseif ($ReadPciConfig) { [uint32]0x226020 } elseif ($TestCaptureDma) { [uint32]0x22A024 } else { [uint32]0x226000 }
$inputBytes = $null
if ($StageFirmware) {
    $expectedHash = '8433390E4ABFE40B251FF3DEC41604E7B817501D1097D51AA2F51C11444D3BF0'
    if ((Get-FileHash -LiteralPath $StageFirmware -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'The IPC3 image hash does not match the locally validated N100 firmware'
    }
    $inputBytes = [IO.File]::ReadAllBytes($StageFirmware)
}
$values = [SofProbeClient]::Exchange($paths[0], $code, $inputBytes)
if ($ReadStream0) {
    [pscustomobject]@{
        Control = ('0x{0:X6}' -f $values[0])
        Status = ('0x{0:X2}' -f $values[1])
        CyclicBufferLength = $values[2]
        LastValidIndex = $values[3]
        Format = ('0x{0:X4}' -f $values[4])
        BdlAddress = ('0x{0:X8}{1:X8}' -f $values[6], $values[5])
        ProcessingPipeControl = ('0x{0:X8}' -f $values[7])
    } | Format-List
    return
}
if ($values[0] -ne 1) { throw "Unsupported snapshot version: $($values[0])" }
if ($TestCaptureDma) {
    [pscustomobject]@{
        Version = $values[0]
        BeforeControl = ('0x{0:X8}' -f $values[1])
        ConfiguredControl = ('0x{0:X8}' -f $values[2])
        RunningControl = ('0x{0:X8}' -f $values[3])
        StoppedControl = ('0x{0:X8}' -f $values[4])
        Position = $values[5]
        CaptureStatus = ('0x{0:X2}' -f $values[6])
        ConfiguredPpctl = ('0x{0:X8}' -f $values[7])
        RestoredControl = ('0x{0:X8}' -f $values[8])
        RestoredPpctl = ('0x{0:X8}' -f $values[9])
        RestoredCbl = $values[10]
        RestoredLvi = $values[11]
        RestoredFormat = $values[12]
        RestoredBdl = ('0x{0:X8}{1:X8}' -f $values[14], $values[13])
        RestoredLtrp = ('0x{0:X2}' -f $values[15])
        Adspcs = ('0x{0:X8}' -f $values[16])
        PciCommand = ('0x{0:X4}' -f $values[17])
        StopPolls = $values[18]
        Result = $values[19]
    } | Format-List
    if ($values[19] -ne 0 -or ($values[3] -band 2) -eq 0 -or
        ($values[4] -band 2) -ne 0 -or $values[8] -ne $values[1] -or
        $values[9] -ne 0x40000000 -or $values[10] -ne 0 -or
        $values[11] -ne 0 -or $values[12] -ne 0 -or
        $values[13] -ne 0 -or $values[14] -ne 0 -or
        $values[15] -ne 0xA9 -or $values[16] -ne 0x00000F0F) {
        throw 'Capture DMA did not start, stop, and restore the observed baseline'
    }
    return
}
if ($ReadPciConfig) {
    [pscustomobject]@{
        VendorDevice = ('0x{0:X8}' -f $values[1])
        Command = ('0x{0:X4}' -f ($values[2] -band 0xffff))
        BusMasterEnabled = [bool]($values[2] -band 4)
        MemorySpaceEnabled = [bool]($values[2] -band 2)
        CommandStatus = ('0x{0:X8}' -f $values[2])
        RevisionClass = ('0x{0:X8}' -f $values[3])
        HeaderInfo = ('0x{0:X8}' -f $values[4])
    } | Format-List
    if ($values[1] -ne 0x54C88086) { throw 'Unexpected PCI audio device' }
    return
}
if ($ReadBootPreflight) {
    [pscustomobject]@{
        Gcap = ('0x{0:X4}' -f $values[1])
        CaptureControl = ('0x{0:X6}' -f ($values[2] -band 0x00ffffff))
        CaptureStatus = ('0x{0:X2}' -f ($values[2] -shr 24))
        CaptureCbl = $values[3]
        CaptureLvi = $values[4]
        CaptureFormat = ('0x{0:X4}' -f $values[5])
        CaptureBdl = ('0x{0:X8}{1:X8}' -f $values[7], $values[6])
        ProcessingPipeControl = ('0x{0:X8}' -f $values[8])
        Ltrp = ('0x{0:X2}' -f $values[9])
        Adspcs = ('0x{0:X8}' -f $values[10])
        Hipcidr = ('0x{0:X8}' -f $values[11])
        Hipcida = ('0x{0:X8}' -f $values[12])
        Hipcctl = ('0x{0:X8}' -f $values[13])
        Adspic = ('0x{0:X8}' -f $values[14])
        Adspis = ('0x{0:X8}' -f $values[15])
    } | Format-List
    return
}
if ($TestStreamSetup -or $TestCaptureSetup) {
    [pscustomobject]@{
        Version = $values[0]
        BeforeControl = ('0x{0:X6}' -f $values[1])
        ResetAcknowledged = [bool]$values[2]
        ConfiguredControl = ('0x{0:X6}' -f $values[3])
        ConfiguredCbl = $values[4]
        ConfiguredLvi = $values[5]
        ConfiguredFormat = ('0x{0:X4}' -f $values[6])
        ConfiguredBdl = ('0x{0:X8}{1:X8}' -f $values[8], $values[7])
        ConfiguredPpctl = ('0x{0:X8}' -f $values[9])
        RestoredControl = ('0x{0:X6}' -f $values[10])
        RestoredCbl = $values[11]
        RestoredLvi = $values[12]
        RestoredFormat = $values[13]
        RestoredBdlLow = $values[14]
        Result = $values[15]
    } | Format-List
    if ($values[15] -ne 0 -or $values[2] -ne 1 -or
        $values[10] -ne $values[1] -or $values[11] -ne 0 -or
        $values[12] -ne 0 -or $values[13] -ne 0 -or $values[14] -ne 0) {
        throw 'Stream setup did not complete and restore the observed baseline'
    }
    return
}
if ($TestDmaAllocation -or $StageFirmware) {
    [pscustomobject]@{
        Version = $values[0]
        BdlLogicalAddress = ('0x{0:X8}{1:X8}' -f $values[2], $values[1])
        PayloadLogicalAddress = ('0x{0:X8}{1:X8}' -f $values[4], $values[3])
        BdlBytes = $values[5]
        PayloadBytes = $values[6]
        BdlEntryBytes = $values[7]
    } | Format-List
    if (($values[1] -band 127) -ne 0 -or ($values[3] -band 127) -ne 0 -or
        $values[5] -ne 128 -or $values[6] -ne 524288 -or $values[7] -ne 16) {
        throw 'Unexpected DMA buffer alignment or descriptor size'
    }
    return
}
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
