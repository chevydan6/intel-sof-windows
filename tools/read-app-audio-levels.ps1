# SPDX-License-Identifier: GPL-2.0-only
# Read-only endpoint/session inspection, using the Windows Core Audio interfaces.
# Run in the signed-in desktop session to inspect its applications. An SSH
# session may enumerate only system audio and cannot rule out app attenuation.
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
[ComImport,Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface QEnumerator {
 [PreserveSig] int EnumAudioEndpoints(int flow,uint mask,out IntPtr list);
 [PreserveSig] int GetDefaultAudioEndpoint(int flow,int role,out QDevice device);
}
[ComImport,Guid("D666063F-1587-4E43-81F1-B948E807363F"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface QDevice {
 [PreserveSig] int Activate(ref Guid id,uint context,IntPtr args,out IntPtr obj);
}
[ComImport,Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface QManager {
 [PreserveSig] int GetAudioSessionControl(IntPtr id,uint flags,out IntPtr control);
 [PreserveSig] int GetSimpleAudioVolume(IntPtr id,uint flags,out IntPtr volume);
 [PreserveSig] int GetSessionEnumerator(out QSessions sessions);
}
[ComImport,Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface QSessions {
 [PreserveSig] int GetCount(out int count);
 [PreserveSig] int GetSession(int index,out IntPtr session);
}
[ComImport,Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface QControl {
 [PreserveSig] int GetState(out int state);
 [PreserveSig] int GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string name);
 [PreserveSig] int SetDisplayName(IntPtr name,IntPtr context);
 [PreserveSig] int GetIconPath(out IntPtr path);
 [PreserveSig] int SetIconPath(IntPtr path,IntPtr context);
 [PreserveSig] int GetGroupingParam(out Guid group);
 [PreserveSig] int SetGroupingParam(ref Guid group,IntPtr context);
 [PreserveSig] int RegisterAudioSessionNotification(IntPtr events);
 [PreserveSig] int UnregisterAudioSessionNotification(IntPtr events);
 [PreserveSig] int GetSessionIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
 [PreserveSig] int GetSessionInstanceIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
 [PreserveSig] int GetProcessId(out uint pid);
}
[ComImport,Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface QVolume {
 [PreserveSig] int SetMasterVolume(float level,ref Guid context);
 [PreserveSig] int GetMasterVolume(out float level);
 [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute,ref Guid context);
 [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
}
public sealed class AppAudioLevel {
 public uint ProcessId; public string ProcessName,Name; public int State; public float Volume; public bool Muted;
}
public static class AppAudioLevels {
 static void Check(int hr){Marshal.ThrowExceptionForHR(hr);}
 public static AppAudioLevel[] Read(){
  object enumerator=null,manager=null;QDevice device=null;QSessions sessions=null;IntPtr ptr=IntPtr.Zero;
  var results=new List<AppAudioLevel>();
  try {
   enumerator=Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")));
   Check(((QEnumerator)enumerator).GetDefaultAudioEndpoint(0,1,out device));
   Guid id=new Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F");
   Check(device.Activate(ref id,23,IntPtr.Zero,out ptr));
   manager=Marshal.GetObjectForIUnknown(ptr);Marshal.Release(ptr);ptr=IntPtr.Zero;
   Check(((QManager)manager).GetSessionEnumerator(out sessions));
   int count;Check(sessions.GetCount(out count));
   for(int i=0;i<count;i++){
    object obj=null;
    try {
     Check(sessions.GetSession(i,out ptr));obj=Marshal.GetObjectForIUnknown(ptr);Marshal.Release(ptr);ptr=IntPtr.Zero;
     QControl control=(QControl)obj;QVolume volume=(QVolume)obj;
     var row=new AppAudioLevel();Check(control.GetProcessId(out row.ProcessId));Check(control.GetState(out row.State));
     Check(control.GetDisplayName(out row.Name));Check(volume.GetMasterVolume(out row.Volume));Check(volume.GetMute(out row.Muted));
     try{row.ProcessName=System.Diagnostics.Process.GetProcessById((int)row.ProcessId).ProcessName;}catch{row.ProcessName="system/ended";}
     results.Add(row);
    }finally{if(obj!=null)Marshal.ReleaseComObject(obj);if(ptr!=IntPtr.Zero){Marshal.Release(ptr);ptr=IntPtr.Zero;}}
   }
   return results.ToArray();
  }finally{
   if(ptr!=IntPtr.Zero)Marshal.Release(ptr);
   if(sessions!=null)Marshal.ReleaseComObject(sessions);
   if(manager!=null)Marshal.ReleaseComObject(manager);
   if(device!=null)Marshal.ReleaseComObject(device);
   if(enumerator!=null)Marshal.ReleaseComObject(enumerator);
  }
 }
}
'@
[AppAudioLevels]::Read() | ConvertTo-Json -Depth 3
