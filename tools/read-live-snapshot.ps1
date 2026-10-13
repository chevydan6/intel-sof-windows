# SPDX-License-Identifier: GPL-2.0-only
param([switch]$PulseCore0, [switch]$TestDmaAllocation,
      [string]$StageFirmware, [switch]$ReadStream0,
      [switch]$TestStreamSetup, [switch]$ReadBootPreflight,
      [switch]$TestCaptureSetup, [switch]$ReadPciConfig,
      [switch]$TestCaptureDma, [switch]$TestRomEntry,
      [switch]$TestFirmwareDma, [switch]$TestIpc3Ready,
      [switch]$ReadIpc3Info, [switch]$AckIpc3Ready,
      [switch]$QueryIpc3Memory, [switch]$StartSession,
      [switch]$SessionMemory, [switch]$StopSession)
$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
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
if ($TestRomEntry) { $selected++ }
if ($TestFirmwareDma) { $selected++ }
if ($TestIpc3Ready) { $selected++ }
if ($ReadIpc3Info) { $selected++ }
if ($AckIpc3Ready) { $selected++ }
if ($QueryIpc3Memory) { $selected++ }
if ($StartSession) { $selected++ }
if ($SessionMemory) { $selected++ }
if ($StopSession) { $selected++ }
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
        using (SafeFileHandle handle = CreateFileW(path, (code == 0x22A004U || code == 0x22A00CU || code == 0x22A014U || code == 0x22A01CU || code == 0x22A024U || code == 0x22A028U || code == 0x22A02CU || code == 0x22A030U || code == 0x22A034U || code == 0x22A038U || code == 0x22A03CU || code == 0x22A040U || code == 0x22A044U || code == 0x22A048U) ? 0x40000000 : 0x80000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes = new byte[code == 0x22A040U ? 720 : (code == 0x22A044U ? 412 : (code == 0x22A048U ? 24 : (code == 0x22A03CU ? 716 : (code == 0x22A038U ? 312 : (code == 0x22A034U ? 300 : (code == 0x22A030U ? 192 : (code == 0x22A02CU ? 152 : (code == 0x22A028U ? 96 : (code == 0x22A024U ? 80 : ((code == 0x22A014U || code == 0x226018U || code == 0x22A01CU) ? 64 : 32))))))))))];
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
$code = if ($PulseCore0) { [uint32]0x22A004 } elseif ($TestDmaAllocation) { [uint32]0x226008 } elseif ($StageFirmware) { [uint32]0x22A00C } elseif ($ReadStream0) { [uint32]0x226010 } elseif ($TestStreamSetup) { [uint32]0x22A014 } elseif ($ReadBootPreflight) { [uint32]0x226018 } elseif ($TestCaptureSetup) { [uint32]0x22A01C } elseif ($ReadPciConfig) { [uint32]0x226020 } elseif ($TestCaptureDma) { [uint32]0x22A024 } elseif ($TestRomEntry) { [uint32]0x22A028 } elseif ($TestFirmwareDma) { [uint32]0x22A02C } elseif ($TestIpc3Ready) { [uint32]0x22A030 } elseif ($ReadIpc3Info) { [uint32]0x22A034 } elseif ($AckIpc3Ready) { [uint32]0x22A038 } elseif ($QueryIpc3Memory) { [uint32]0x22A03C } elseif ($StartSession) { [uint32]0x22A040 } elseif ($SessionMemory) { [uint32]0x22A044 } elseif ($StopSession) { [uint32]0x22A048 } else { [uint32]0x226000 }
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
if ($StartSession) {
    [pscustomobject]@{
        FirmwareStatus = ('0x{0:X8}' -f $values[28])
        FirstReplyCommand = ('0x{0:X8}' -f $values[80])
        FirstReplyError = [BitConverter]::ToInt32([BitConverter]::GetBytes($values[81]), 0)
        FirstReplySize = $values[79]
        SessionActive = $values[179]
        BootResult = $values[22]
    } | Format-List
    if ($values[22] -ne 0 -or $values[179] -ne 1 -or
        $values[80] -ne 0xd0010000 -or $values[81] -ne 0) {
        throw 'Managed IPC3 session did not start'
    }
    return
}
if ($SessionMemory) {
    [pscustomobject]@{
        ReplyDoorbell = ('0x{0:X8}' -f $values[1])
        ReplySize = $values[2]
        ReplyCommand = ('0x{0:X8}' -f $values[3])
        ReplyError = [BitConverter]::ToInt32([BitConverter]::GetBytes($values[4]), 0)
        ReplyPolls = $values[5]
        Result = $values[6]
    } | Format-List
    if ($values[6] -ne 0 -or $values[3] -ne 0xd0010000 -or
        $values[4] -ne 0 -or $values[2] -lt 12 -or
        ($values[1] -band 0x80000000) -eq 0) {
        throw 'Managed IPC3 request/reply failed'
    }
    return
}
if ($StopSession) {
    [pscustomobject]@{
        BeforeAdspcs = ('0x{0:X8}' -f $values[1])
        RestoredAdspcs = ('0x{0:X8}' -f $values[2])
        SspUnavailableMask = ('0x{0:X8}' -f $values[3])
        CleanupFlags = ('0x{0:X8}' -f $values[4])
        Result = $values[5]
    } | Format-List
    if ($values[5] -ne 0 -or $values[4] -ne 0 -or
        $values[2] -ne $values[1]) {
        throw 'Managed IPC3 session did not stop cleanly'
    }
    return
}
if ($QueryIpc3Memory) {
    $replySize = $values[79]
    $replyWordCount = [Math]::Min([int]($replySize / 4), 96)
    $replyWords = if ($replyWordCount -gt 0) { $values[83..(82 + $replyWordCount)] } else { @() }
    [pscustomobject]@{
        FirmwareStatus = ('0x{0:X8}' -f $values[28])
        ReadyAckDoorbell = ('0x{0:X8}' -f $values[75])
        ReplyDoorbell = ('0x{0:X8}' -f $values[78])
        ReplySize = $replySize
        ReplyCommand = ('0x{0:X8}' -f $values[80])
        ReplyError = [BitConverter]::ToInt32([BitConverter]::GetBytes($values[81]), 0)
        ReplyPolls = $values[82]
        ReplyWords = (($replyWords | ForEach-Object { '0x{0:X8}' -f $_ }) -join ' ')
        CleanupFlags = ('0x{0:X8}' -f $values[46])
        Result = $values[22]
    } | Format-List
    if ($values[22] -ne 0 -or $values[46] -ne 0 -or
        ($values[28] -band 0x00ffffff) -ne 5 -or
        ($values[78] -band 0x80000000) -eq 0 -or
        $replySize -lt 12 -or $replySize -gt 384) {
        throw 'IPC3 memory query or restoration failed'
    }
    return
}
if ($AckIpc3Ready) {
    [pscustomobject]@{
        FirmwareStatus = ('0x{0:X8}' -f $values[28])
        MailboxSize = $values[41]
        MailboxCommand = ('0x{0:X8}' -f $values[42])
        HipctdrBeforeAck = ('0x{0:X8}' -f $values[38])
        HipctdrAfterAck = ('0x{0:X8}' -f $values[75])
        HipctdaAfterAck = ('0x{0:X8}' -f $values[76])
        AckPolls = $values[77]
        CleanupFlags = ('0x{0:X8}' -f $values[46])
        Result = $values[22]
    } | Format-List
    if ($values[22] -ne 0 -or $values[46] -ne 0 -or
        ($values[28] -band 0x00ffffff) -ne 5 -or
        ($values[38] -band 0x80000000) -eq 0 -or
        ($values[75] -band 0x80000000) -ne 0 -or
        $values[41] -ne 108 -or $values[48] -ne $values[41] -or
        $values[49] -ne $values[42]) {
        throw 'IPC3 firmware-ready acknowledgment or restoration failed'
    }
    return
}
if ($ReadIpc3Info) {
    [pscustomobject]@{
        FirmwareStatus = ('0x{0:X8}' -f $values[28])
        MailboxSize = $values[41]
        MailboxCommand = ('0x{0:X8}' -f $values[42])
        DspBoxOffset = ('0x{0:X8}' -f $values[50])
        HostBoxOffset = ('0x{0:X8}' -f $values[51])
        DspBoxSize = $values[52]
        HostBoxSize = $values[53]
        FirmwareReadyWords = (($values[48..74] | ForEach-Object { '0x{0:X8}' -f $_ }) -join ' ')
        CleanupFlags = ('0x{0:X8}' -f $values[46])
        SspUnavailableMask = ('0x{0:X8}' -f $values[47])
        Result = $values[22]
    } | Format-List
    if ($values[22] -ne 0 -or $values[46] -ne 0 -or
        ($values[28] -band 0x00ffffff) -ne 5 -or
        ($values[38] -band 0x80000000) -eq 0 -or
        $values[41] -ne 108 -or $values[48] -ne $values[41] -or
        $values[49] -ne $values[42] -or
        ($values[49] -band 0xf0000000) -ne 0x70000000) {
        throw 'IPC3 firmware-ready payload or restoration did not complete'
    }
    return
}
if ($TestIpc3Ready) {
    [pscustomobject]@{
        Version = $values[0]
        RomStatus = ('0x{0:X8}' -f $values[7])
        FirmwareStatus = ('0x{0:X8}' -f $values[28])
        FirmwareError = ('0x{0:X8}' -f $values[29])
        Hipctdr = ('0x{0:X8}' -f $values[38])
        Hipctdd = ('0x{0:X8}' -f $values[39])
        Hipctda = ('0x{0:X8}' -f $values[40])
        MailboxSize = $values[41]
        MailboxCommand = ('0x{0:X8}' -f $values[42])
        ReadyPolls = $values[43]
        HipctdrAfterCleanup = ('0x{0:X8}' -f $values[44])
        HipctdaAfterCleanup = ('0x{0:X8}' -f $values[45])
        CleanupFlags = ('0x{0:X8}' -f $values[46])
        SspUnavailableMask = ('0x{0:X8}' -f $values[47])
        OutputStatus = ('0x{0:X2}' -f $values[27])
        SpibControlRestored = ('0x{0:X8}' -f $values[36])
        SpibValueRestored = $values[37]
        RestoredAdspcs = ('0x{0:X8}' -f $values[11])
        RestoredOutputControl = ('0x{0:X8}' -f $values[12])
        RestoredPpctl = ('0x{0:X8}' -f $values[13])
        Ssp0Before = ('0x{0:X8}' -f $values[16])
        Ssp0After = ('0x{0:X8}' -f $values[17])
        Ssp1Before = ('0x{0:X8}' -f $values[18])
        Ssp1After = ('0x{0:X8}' -f $values[19])
        Ssp2Before = ('0x{0:X8}' -f $values[20])
        Ssp2After = ('0x{0:X8}' -f $values[21])
        Result = $values[22]
    } | Format-List
    if ($values[22] -ne 0 -or ($values[28] -band 0x00ffffff) -ne 5 -or
        ($values[38] -band 0x80000000) -eq 0 -or
        ($values[42] -band 0xf0000000) -ne 0x70000000 -or
        $values[11] -ne $values[1] -or $values[12] -ne 0x00040000 -or
        $values[13] -ne 0x40000000 -or $values[36] -ne $values[32] -or
        $values[37] -ne $values[33]) {
        throw 'IPC3 firmware-ready message or restoration did not complete'
    }
    return
}
if ($TestFirmwareDma) {
    [pscustomobject]@{
        Version = $values[0]
        RomStatus = ('0x{0:X8}' -f $values[7])
        RomError = ('0x{0:X8}' -f $values[8])
        IpcAck = ('0x{0:X8}' -f $values[6])
        RunningOutputControl = ('0x{0:X8}' -f $values[24])
        StoppedOutputControl = ('0x{0:X8}' -f $values[25])
        OutputPosition = $values[26]
        OutputStatus = ('0x{0:X2}' -f $values[27])
        FirmwareStatus = ('0x{0:X8}' -f $values[28])
        FirmwareError = ('0x{0:X8}' -f $values[29])
        FirmwarePolls = $values[30]
        StopPolls = $values[31]
        SpibControlBefore = ('0x{0:X8}' -f $values[32])
        SpibValueBefore = $values[33]
        SpibControlConfigured = ('0x{0:X8}' -f $values[34])
        SpibValueConfigured = $values[35]
        SpibControlRestored = ('0x{0:X8}' -f $values[36])
        SpibValueRestored = $values[37]
        RestoredAdspcs = ('0x{0:X8}' -f $values[11])
        RestoredOutputControl = ('0x{0:X8}' -f $values[12])
        RestoredPpctl = ('0x{0:X8}' -f $values[13])
        RestoredHipcidr = ('0x{0:X8}' -f $values[14])
        RestoredHipcida = ('0x{0:X8}' -f $values[15])
        Result = $values[22]
    } | Format-List
    if ($values[22] -ne 0 -or ($values[28] -band 0x00ffffff) -ne 5 -or
        $values[11] -ne $values[1] -or $values[12] -ne 0x00040000 -or
        $values[13] -ne 0x40000000 -or $values[36] -ne $values[32] -or
        $values[37] -ne $values[33]) {
        throw 'Firmware DMA, firmware entry, or register restoration did not complete'
    }
    return
}
if ($TestRomEntry) {
    [pscustomobject]@{
        Version = $values[0]
        BeforeAdspcs = ('0x{0:X8}' -f $values[1])
        PoweredAdspcs = ('0x{0:X8}' -f $values[2])
        ResetReleasedAdspcs = ('0x{0:X8}' -f $values[3])
        RunningAdspcs = ('0x{0:X8}' -f $values[4])
        RomControl = ('0x{0:X8}' -f $values[5])
        IpcAck = ('0x{0:X8}' -f $values[6])
        RomStatus = ('0x{0:X8}' -f $values[7])
        RomError = ('0x{0:X8}' -f $values[8])
        AckPolls = $values[9]
        RomPolls = $values[10]
        RestoredAdspcs = ('0x{0:X8}' -f $values[11])
        RestoredOutputControl = ('0x{0:X8}' -f $values[12])
        RestoredPpctl = ('0x{0:X8}' -f $values[13])
        RestoredHipcidr = ('0x{0:X8}' -f $values[14])
        RestoredHipcida = ('0x{0:X8}' -f $values[15])
        Ssp0Before = ('0x{0:X8}' -f $values[16])
        Ssp0After = ('0x{0:X8}' -f $values[17])
        Ssp1Before = ('0x{0:X8}' -f $values[18])
        Ssp1After = ('0x{0:X8}' -f $values[19])
        Ssp2Before = ('0x{0:X8}' -f $values[20])
        Ssp2After = ('0x{0:X8}' -f $values[21])
        Result = $values[22]
        OutputConfiguredControl = ('0x{0:X6}' -f $values[23])
    } | Format-List
    if ($values[22] -ne 0 -or ($values[7] -band 0x00ffffff) -ne 1 -or
        $values[11] -ne $values[1] -or $values[12] -ne 0x00040000 -or
        $values[13] -ne 0x40000000) {
        throw 'DSP ROM entry or register restoration did not complete'
    }
    return
}
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
