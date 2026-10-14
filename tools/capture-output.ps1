# SPDX-License-Identifier: GPL-2.0-only
# Bounded software-output capture for local audio diagnostics (not microphone).
param([ValidateRange(1,10)][int]$Seconds=3,
      [Parameter(Mandatory=$true)][string]$OutputPrefix)
$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$source = @'
using System;
using System.Runtime.InteropServices;
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDeviceEnumerator {
    [PreserveSig] int EnumAudioEndpoints(int flow, uint stateMask, out IMMDeviceCollection devices);
    [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice device);
    [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
    [PreserveSig] int RegisterEndpointNotificationCallback(IntPtr callback);
    [PreserveSig] int UnregisterEndpointNotificationCallback(IntPtr callback);
}
[ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDeviceCollection {
    [PreserveSig] int GetCount(out uint count);
    [PreserveSig] int Item(uint index, out IMMDevice device);
}
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDevice {
    [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr activationParams, out IntPtr obj);
    [PreserveSig] int OpenPropertyStore(uint access, out IntPtr properties);
    [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
    [PreserveSig] int GetState(out uint state);
}
[ComImport, Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IAudioClient {
    [PreserveSig] int Initialize(int shareMode, uint streamFlags, long bufferDuration, long periodicity, IntPtr format, ref Guid session);
    [PreserveSig] int GetBufferSize(out uint frames);
    [PreserveSig] int GetStreamLatency(out long latency);
    [PreserveSig] int GetCurrentPadding(out uint padding);
    [PreserveSig] int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closest);
    [PreserveSig] int GetMixFormat(out IntPtr format);
    [PreserveSig] int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);
    [PreserveSig] int Start();
    [PreserveSig] int Stop();
    [PreserveSig] int Reset();
    [PreserveSig] int SetEventHandle(IntPtr handle);
    [PreserveSig] int GetService(ref Guid iid, out IntPtr obj);
}

[ComImport, Guid("C8ADBD64-E71E-48A0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IAudioCaptureClient {
 [PreserveSig] int GetBuffer(out IntPtr data, out uint frames, out uint flags, out ulong device, out ulong qpc);
 [PreserveSig] int ReleaseBuffer(uint frames);
 [PreserveSig] int GetNextPacketSize(out uint frames);
}
public sealed class LoopbackResult {
 public string FormatBase64;
 public long Frames;
 public int Discontinuities;
 public int TimestampErrors;
 public int SilentPackets;
 public int Packets;
}
public static class OutputCapture {
 static void Check(int hr) { Marshal.ThrowExceptionForHR(hr); }
 public static LoopbackResult Run(string path, int seconds) {
  IMMDeviceEnumerator e=null; IMMDevice d=null; IAudioClient client=null;
  IAudioCaptureClient capture=null; IntPtr mix=IntPtr.Zero, p=IntPtr.Zero, cp=IntPtr.Zero;
  bool started=false;
  try {
   e=(IMMDeviceEnumerator)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")));
   Check(e.GetDefaultAudioEndpoint(0,1,out d));
   Guid iid=new Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2");
   Check(d.Activate(ref iid,23,IntPtr.Zero,out p));
   client=(IAudioClient)Marshal.GetTypedObjectForIUnknown(p,typeof(IAudioClient));
   Check(client.GetMixFormat(out mix));
   int formatLength=18+(ushort)Marshal.ReadInt16(mix,16);
   if(formatLength>256) throw new Exception("Unexpected format size");
   byte[] fmt=new byte[formatLength]; Marshal.Copy(mix,fmt,0,fmt.Length);
   int block=(ushort)Marshal.ReadInt16(mix,12);
   if(block<1 || block>64) throw new Exception("Unexpected block alignment");
   var result=new LoopbackResult {FormatBase64=Convert.ToBase64String(fmt)};
   Guid session=Guid.Empty;
   Check(client.Initialize(0,0x20000,1000000,0,mix,ref session));
   iid=new Guid("C8ADBD64-E71E-48A0-A4DE-185C395CD317");
   Check(client.GetService(ref iid,out cp));
   capture=(IAudioCaptureClient)Marshal.GetTypedObjectForIUnknown(cp,typeof(IAudioCaptureClient));
   using(var file=System.IO.File.Create(path)) {
    Check(client.Start()); started=true;
    var clock=System.Diagnostics.Stopwatch.StartNew();
    while(clock.Elapsed.TotalSeconds<seconds) {
     uint next; Check(capture.GetNextPacketSize(out next));
     if(next==0) {System.Threading.Thread.Sleep(5); continue;}
     IntPtr data; uint frames,flags; ulong device,qpc;
     Check(capture.GetBuffer(out data,out frames,out flags,out device,out qpc));
     try {
      if(frames>48000) throw new Exception("Unexpected capture packet size");
      var bytes=new byte[checked((int)frames*block)];
      if((flags&2)==0) Marshal.Copy(data,bytes,0,bytes.Length); else ++result.SilentPackets;
      file.Write(bytes,0,bytes.Length);
      result.Frames+=frames; ++result.Packets;
      if((flags&1)!=0) ++result.Discontinuities;
      if((flags&4)!=0) ++result.TimestampErrors;
     } finally {Check(capture.ReleaseBuffer(frames));}
    }
   }
   return result;
  } finally {
   if(started) client.Stop();
   if(capture!=null) Marshal.ReleaseComObject(capture);
   if(cp!=IntPtr.Zero) Marshal.Release(cp);
   if(mix!=IntPtr.Zero) Marshal.FreeCoTaskMem(mix);
   if(client!=null) Marshal.ReleaseComObject(client);
   if(p!=IntPtr.Zero) Marshal.Release(p);
   if(d!=null) Marshal.ReleaseComObject(d);
   if(e!=null) Marshal.ReleaseComObject(e);
  }
 }
}
'@
Add-Type -TypeDefinition $source
$result = [OutputCapture]::Run($OutputPrefix + '.pcm', $Seconds)
$result | ConvertTo-Json | Set-Content ($OutputPrefix + '.json')
$result | Format-List
