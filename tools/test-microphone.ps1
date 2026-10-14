# SPDX-License-Identifier: GPL-2.0-only
# Bounded physical microphone capture. Does not start playback/audio services.
param([string]$Output = (Join-Path $PSScriptRoot 'microphone.wav'),
      [switch]$DuringAudition, [string]$ReadyFile)
$ErrorActionPreference = 'Stop'
$parent = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
Add-Type -TypeDefinition @'

using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class SofMicClient {
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
    public static byte[] Capture(string path) {
        using (SafeFileHandle handle = CreateFileW(path, 0x40000000U, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if(handle.IsInvalid) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes=new byte[1920064];uint returned;
            if(!DeviceIoControl(handle,0x22A064U,IntPtr.Zero,0,bytes,(uint)bytes.Length,out returned,IntPtr.Zero))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if(returned<64 || returned>bytes.Length || (returned-64)%8!=0)
                throw new Exception("Malformed microphone result");
            Array.Resize(ref bytes,(int)returned);return bytes;
        }
    }
}

'@
. (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxStatus | Out-Host
$paths=@([SofMicClient]::Paths())
if($paths.Count -ne 1){throw 'Expected exactly one SOF interface'}
$failed=$null
try {
 $v=[SofProbeClient]::Exchange($paths[0],0x22605C,$null)
 if($v[0] -ne 8 -or $v[24] -ne 1 -or $v[25] -ne 1){throw 'Headphone-only amplifier hold required'}
 if($DuringAudition) {
  if(!$ReadyFile -or $v[13] -ne 1 -or $v[2] -lt 1){throw 'An active headphone test and readiness path are required'}
 } elseif(@(Get-Service Audiosrv,AudioEndpointBuilder | Where-Object Status -ne Stopped).Count){throw 'Stop audio services before ambient capture'}
 $amp=Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\MX98360A\0\Device Parameters'
 if($amp.SofAmpStage -ne 3 -or $amp.SofAmpStatus -ne 0 -or $amp.SofAmpOffWrites -lt 1){throw 'Amplifier hold not verified'}
 & (Join-Path $PSScriptRoot 'read-headphone-codec.ps1') -AllowPlaying:$DuringAudition | Out-Host
 if($DuringAudition){[IO.File]::WriteAllText($ReadyFile,'ready')}
 $bytes=[SofMicClient]::Capture($paths[0])
 $w=New-Object uint32[] 16
 for($i=0;$i -lt 16;$i++){$w[$i]=[BitConverter]::ToUInt32($bytes,$i*4)}
 $r=[ordered]@{Version=$w[0];Stage=$w[1];Status=('0x{0:X8}' -f $w[2]);CleanupStatus=('0x{0:X8}' -f $w[3]);Bytes=$w[4];Position=$w[5];HdaStatus=('0x{0:X2}' -f $w[6]);ReplyError=('0x{0:X8}' -f $w[7]);Command=('0x{0:X8}' -f $w[8]);Component=$w[9];MaxPollUs=$w[10];ElapsedMs=$w[11];Rate=$w[12];Channels=$w[13];Bits=$w[14]}
 $r | ConvertTo-Json | Tee-Object -FilePath ($Output+'.json') | Write-Host
 if($w[0] -ne 1 -or $w[2] -ne 0 -or $w[3] -ne 0 -or $w[4] -ne 1920000 -or $bytes.Length -ne 1920064 -or $w[12] -ne 48000 -or $w[13] -ne 2 -or $w[14] -ne 32){throw 'Microphone capture failed; see JSON diagnostics'}
 $f=[IO.File]::Create($Output);$bw=New-Object IO.BinaryWriter($f)
 try {
  $bw.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$bw.Write([uint32](36+$w[4]));$bw.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '));$bw.Write([uint32]16)
  $bw.Write([uint16]1);$bw.Write([uint16]2);$bw.Write([uint32]48000);$bw.Write([uint32]384000);$bw.Write([uint16]8);$bw.Write([uint16]32)
  $bw.Write([Text.Encoding]::ASCII.GetBytes('data'));$bw.Write([uint32]$w[4]);$bw.Write($bytes,64,[int]$w[4])
 } finally {$bw.Dispose();$f.Dispose()}
 Write-Host "Saved five-second built-in microphone recording: $Output"
} catch {$failed=$_}
finally {
 if(!$DuringAudition) {
 & (Join-Path $PSScriptRoot 'read-headphone-codec.ps1') -Stop | Out-Host
 & pnputil /restart-device $parent | Out-Host
 if($LASTEXITCODE -ne 0){throw 'Parent device cleanup failed; no reboot performed'}
 $p=@([SofProbeClient]::Paths())
 if($p.Count -ne 1){throw 'Maintenance interface absent after cleanup'}
 $v=[SofProbeClient]::Exchange($p[0],0x22605C,$null)
 $d=[SofProbeClient]::Exchange($p[0],0x226000,$null)
 if($v[11] -ne 7 -or $v[13] -ne 0 -or $d[4] -ne 0xF0F){throw 'DSP shutdown verification failed'}
 & (Join-Path $PSScriptRoot 'read-headphone-codec.ps1') | Out-Host
 Write-Host 'VERIFIED: DSP powered down, headphone codec muted; no Windows reboot'
 }
}
if($failed){throw $failed}
