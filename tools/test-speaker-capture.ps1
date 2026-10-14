# SPDX-License-Identifier: GPL-2.0-only
# Fixed one-channel speaker diagnostic with microphone capture. Default is amp OFF.
# -Pulse explicitly requests one quiet short pulse; ordinary playback stays held.
# Right is the reference speaker; the test laptop's left hardware path is suspect.
param([string]$Output = (Join-Path $PSScriptRoot 'speaker-check.wav'),
      [ValidateSet('Left','Right')][string]$Channel='Right', [switch]$Pulse,
      [switch]$Extended, [switch]$Listen, [switch]$Louder)
$ErrorActionPreference = 'Stop'
if($Extended -and !$Pulse){throw 'Extended requires an explicit Pulse request'}
if($Extended -and $Listen){throw 'Choose Extended or Listen'}
if($Louder -and (!$Listen -or !$Pulse)){throw 'Louder requires Listen and Pulse'}
$parent = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
Add-Type -TypeDefinition @'

using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class SofSpeakerCaptureClient {
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
    public static byte[] Capture(string path,uint code) {
        using (SafeFileHandle handle = CreateFileW(path, 0x40000000U, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if(handle.IsInvalid) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes=new byte[1920064];uint returned;
            if(!DeviceIoControl(handle,code,IntPtr.Zero,0,bytes,(uint)bytes.Length,out returned,IntPtr.Zero))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if(returned<64 || returned>bytes.Length || (returned-64)%8!=0)
                throw new Exception("Malformed microphone result");
            Array.Resize(ref bytes,(int)returned);return bytes;
        }
    }
}

'@
. (Join-Path $PSScriptRoot 'read-live-snapshot.ps1') -AcxStatus | Out-Host
$paths=@([SofSpeakerCaptureClient]::Paths())
if($paths.Count -ne 1){throw 'Expected exactly one SOF interface'}
$failed=$null
try {
 $v=[SofProbeClient]::Exchange($paths[0],0x22605C,$null)
 if($v[0] -ne 8 -or $v[24] -ne 1 -or $v[25] -ne 1){throw 'Headphone-only amplifier hold required'}
 if(@(Get-Service Audiosrv,AudioEndpointBuilder | Where-Object Status -ne Stopped).Count){throw 'Stop audio services before the speaker diagnostic'}
 $before=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1')
 if($before.Ready-ne 1 -or $before.Active-ne 0 -or $before.Faulted-ne 0 -or $before.OutputRequest-ne 1){throw 'Guarded amplifier is not ready and off'}
 & (Join-Path $PSScriptRoot 'read-headphone-codec.ps1') | Out-Host
 $mode=if($Channel-eq 'Left'){1}else{2}
 if($Pulse){$mode+=2}
 $code=[uint32](0x22A068+4*($mode-1))
 if($Extended){$mode+=2;$code=[uint32](0x22A07C+4*($mode-5))}
 if($Listen){
  $mode=$(if($Channel-eq 'Left'){7}else{8})
  if(!$Pulse){$mode+=4}elseif($Louder){$mode+=2}
  $code=[uint32](0x22A084+4*($mode-7))
  if($before.Version-ne 2 -or $before.LeaseMs-ne 100 -or $before.MaximumMs-ne 2200){throw 'Listening amplifier guard is not installed'}
 }
 $bytes=[SofSpeakerCaptureClient]::Capture($paths[0],$code)
 $w=New-Object uint32[] 16
 for($i=0;$i -lt 16;$i++){$w[$i]=[BitConverter]::ToUInt32($bytes,$i*4)}
 $r=[ordered]@{Version=$w[0];Stage=$w[1];Status=('0x{0:X8}' -f $w[2]);CleanupStatus=('0x{0:X8}' -f $w[3]);Bytes=$w[4];Position=$w[5];HdaStatus=('0x{0:X2}' -f $w[6]);ReplyError=('0x{0:X8}' -f $w[7]);Command=('0x{0:X8}' -f $w[8]);Component=$w[9];MaxPollUs=$w[10];ElapsedMs=$w[11];Rate=$w[12];Channels=$w[13];Bits=$w[14];SpeakerFlags=('0x{0:X8}' -f $w[15]);Channel=$Channel;Pulse=[bool]$Pulse}
 $r['RequestedPulseMs']=$(if(!$Pulse){0}elseif($Listen){2000}elseif($Extended){120}else{40})
 $r['Listen']=[bool]$Listen; $r['Louder']=[bool]$Louder
 $r['SourcePeakDbfs']=$(if($Louder){-12.04}else{-18.06})
 $r | ConvertTo-Json | Tee-Object -FilePath ($Output+'.json') | Write-Host
 $expectedFlags=$mode -bor $(if($Pulse){0xF00}else{0xC00})
 if($Extended -or ($Listen -and $Pulse)){$expectedFlags=$expectedFlags -bor 0x1000}
 if($w[15] -ne $expectedFlags){throw 'The requested speaker diagnostic did not finish'}
 if($w[0] -ne 2 -or $w[2] -ne 0 -or $w[3] -ne 0 -or $w[4] -ne 1920000 -or $bytes.Length -ne 1920064 -or $w[12] -ne 48000 -or $w[13] -ne 2 -or $w[14] -ne 32){throw 'Microphone capture failed; see JSON diagnostics'}
 $f=[IO.File]::Create($Output);$bw=New-Object IO.BinaryWriter($f)
 try {
  $bw.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$bw.Write([uint32](36+$w[4]));$bw.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '));$bw.Write([uint32]16)
  $bw.Write([uint16]1);$bw.Write([uint16]2);$bw.Write([uint32]48000);$bw.Write([uint32]384000);$bw.Write([uint16]8);$bw.Write([uint16]32)
  $bw.Write([Text.Encoding]::ASCII.GetBytes('data'));$bw.Write([uint32]$w[4]);$bw.Write($bytes,64,[int]$w[4])
 } finally {$bw.Dispose();$f.Dispose()}
 Write-Host "Saved five-second speaker diagnostic recording: $Output"
 $s=[SofProbeClient]::Exchange($paths[0],0x226078,$null)
 if($s.Length -ne 54){throw 'Malformed speaker snapshot'}
 $snapshot=[ordered]@{Version=$s[0];Mode=$s[1];HostPosition=$s[2];CapturedBytes=$s[3]}
 $names=@('SSCR0','SSCR1','SSSR','SSPSP','SSTSA','SSCR2','SSPSP2','SSCR3')
 for($i=0;$i -lt 8;$i++){$snapshot[$names[$i]]=('0x{0:X8}' -f $s[4+$i])}
 $snapshot['AllRegistersReadAsOnes']=(@($s[4..11]|Where-Object {$_ -ne [uint32]::MaxValue}).Count -eq 0)
 $snapshot['PositionOffset']=$s[12];$snapshot['PositionReadMask']=$s[13]
 $snapshot['FirstCapturedBytes']=$s[14];$snapshot['SecondCapturedBytes']=$s[15]
 $snapshot['FirstPositionWords']=@($s[16..34]);$snapshot['SecondPositionWords']=@($s[35..53])
 $snapshot | ConvertTo-Json | Tee-Object -FilePath ($Output+'.ssp.json') | Write-Host
 if($s[0]-ne 2 -or $s[1]-ne $mode -or $s[3]-lt 384000){throw 'No valid pre-enable speaker snapshot'}
} catch {$failed=$_}
finally {
 $cleanupFailure=$null;$after=$null
 try{
  $after=& (Join-Path $PSScriptRoot 'read-speaker-amp.ps1') -Stop
  $after|ConvertTo-Json|Set-Content ($Output+'.amp.json')
 }catch{$cleanupFailure=$_}
 try{& (Join-Path $PSScriptRoot 'read-headphone-codec.ps1') -Stop | Out-Host}catch{$cleanupFailure=$_}
 & pnputil /restart-device $parent | Out-Host
 if($LASTEXITCODE -ne 0){throw 'Parent device cleanup failed; no reboot performed'}
 $p=@([SofProbeClient]::Paths())
 if($p.Count -ne 1){throw 'Maintenance interface absent after cleanup'}
 $v=[SofProbeClient]::Exchange($p[0],0x22605C,$null)
 $d=[SofProbeClient]::Exchange($p[0],0x226000,$null)
 if($v[11] -ne 7 -or $v[13] -ne 0 -or $d[4] -ne 0xF0F){throw 'DSP shutdown verification failed'}
 & (Join-Path $PSScriptRoot 'read-headphone-codec.ps1') | Out-Host
 if($cleanupFailure){throw $cleanupFailure}
 if($after.Active-ne 0 -or $after.OutputRequest-ne 1 -or $after.Faulted-ne 0 -or $after.CleanupStatus-ne '0x00000000'){throw 'Amplifier shutdown or fault check failed'}
 if($before -and $after.OnWrites-ne ($before.OnWrites+$(if($Pulse){1}else{0}))){throw 'Unexpected amplifier enable count'}
 Write-Host 'VERIFIED: amplifier off request accepted, DSP powered down, headphones muted; no Windows reboot'
}
if($failed){throw $failed}
