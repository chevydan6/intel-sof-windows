# SPDX-License-Identifier: GPL-2.0-only
# Windows software endpoint volume/mute control for bounded validation.
if (!('HeadphoneLevel' -as [type])) {
Add-Type -TypeDefinition @'

using System;
using System.Runtime.InteropServices;
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ILevelEnumerator {
    [PreserveSig] int EnumAudioEndpoints(int flow, uint stateMask, out IntPtr devices);
    [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out ILevelDevice device);
}
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ILevelDevice {
    [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr parameters, out IntPtr obj);
    [PreserveSig] int OpenPropertyStore(uint access, out IntPtr properties);
    [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
    [PreserveSig] int GetState(out uint state);
}
[ComImport, Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ILevelVolume {
    [PreserveSig] int RegisterControlChangeNotify(IntPtr notify);
    [PreserveSig] int UnregisterControlChangeNotify(IntPtr notify);
    [PreserveSig] int GetChannelCount(out uint count);
    [PreserveSig] int SetMasterVolumeLevel(float level, ref Guid context);
    [PreserveSig] int SetMasterVolumeLevelScalar(float level, ref Guid context);
    [PreserveSig] int GetMasterVolumeLevel(out float level);
    [PreserveSig] int GetMasterVolumeLevelScalar(out float level);
    [PreserveSig] int SetChannelVolumeLevel(uint channel, float level, ref Guid context);
    [PreserveSig] int SetChannelVolumeLevelScalar(uint channel, float level, ref Guid context);
    [PreserveSig] int GetChannelVolumeLevel(uint channel, out float level);
    [PreserveSig] int GetChannelVolumeLevelScalar(uint channel, out float level);
    [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid context);
    [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
    [PreserveSig] int GetVolumeStepInfo(out uint step, out uint count);
    [PreserveSig] int VolumeStepUp(ref Guid context);
    [PreserveSig] int VolumeStepDown(ref Guid context);
    [PreserveSig] int QueryHardwareSupport(out uint mask);
    [PreserveSig] int GetVolumeRange(out float min, out float max, out float step);
}
public sealed class HeadphoneLevel : IDisposable {
 ILevelEnumerator e; ILevelDevice d; ILevelVolume v; IntPtr pointer;
 static void Check(int hr){Marshal.ThrowExceptionForHR(hr);}
 public string Id {get;private set;}
 public HeadphoneLevel() {
  try {
   e=(ILevelEnumerator)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")));
   Check(e.GetDefaultAudioEndpoint(0,1,out d));string id;Check(d.GetId(out id));Id=id;
   Guid iid=new Guid("5CDF2C82-841E-4546-9722-0CF74078229A");Check(d.Activate(ref iid,23,IntPtr.Zero,out pointer));
   v=(ILevelVolume)Marshal.GetTypedObjectForIUnknown(pointer,typeof(ILevelVolume));
  } catch {Dispose();throw;}
 }
 public float Db {get{float db;Check(v.GetMasterVolumeLevel(out db));return db;}}
 public bool Muted {get{bool b;Check(v.GetMute(out b));return b;}}
 public void SetDb(float db){Guid g=Guid.Empty;Check(v.SetMasterVolumeLevel(db,ref g));}
 public void SetMuted(bool b){Guid g=Guid.Empty;Check(v.SetMute(b,ref g));}
 public void Dispose(){if(v!=null){Marshal.ReleaseComObject(v);v=null;}if(pointer!=IntPtr.Zero){Marshal.Release(pointer);pointer=IntPtr.Zero;}if(d!=null){Marshal.ReleaseComObject(d);d=null;}if(e!=null){Marshal.ReleaseComObject(e);e=null;}}
}
'@
}
