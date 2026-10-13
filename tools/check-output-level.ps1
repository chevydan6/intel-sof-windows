# SPDX-License-Identifier: GPL-2.0-only
# Inspect the default speaker level; optionally reduce it by the given dB.
param([ValidateRange(0,30)][double]$AttenuateDb = 0)
$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$source = @'
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
public static class OutputLevel {
    static void Check(int hr) { Marshal.ThrowExceptionForHR(hr); }
    public static void Run(float reduction) {
        var e = (ILevelEnumerator)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")));
        ILevelDevice d = null; ILevelVolume volume = null; IntPtr pointer = IntPtr.Zero;
        try {
            Check(e.GetDefaultAudioEndpoint(0,1,out d));
            string id; Check(d.GetId(out id)); Console.WriteLine("Endpoint={0}",id);
            Guid iid = new Guid("5CDF2C82-841E-4546-9722-0CF74078229A");
            Check(d.Activate(ref iid,23,IntPtr.Zero,out pointer));
            volume = (ILevelVolume)Marshal.GetTypedObjectForIUnknown(pointer,typeof(ILevelVolume));
            float db, scalar, min, max, step; bool muted; uint hardware, channels;
            Check(volume.GetMasterVolumeLevel(out db));
            Check(volume.GetMasterVolumeLevelScalar(out scalar));
            Check(volume.GetMute(out muted));
            Check(volume.GetVolumeRange(out min,out max,out step));
            Check(volume.QueryHardwareSupport(out hardware));
            Check(volume.GetChannelCount(out channels));
            Console.WriteLine("BeforeDb={0} BeforeScalar={1} Muted={2} HardwareSupport={3} Channels={4} Range={5}..{6} Step={7}",db,scalar,muted,hardware,channels,min,max,step);
            if (reduction > 0) {
                float target = Math.Max(min, db-reduction);
                Guid context = Guid.Empty;
                Check(volume.SetMasterVolumeLevel(target, ref context));
                Console.WriteLine("RequestedDb={0}",target);
            }
            Check(volume.GetMasterVolumeLevel(out db));
            Check(volume.GetMasterVolumeLevelScalar(out scalar));
            Console.WriteLine("AfterDb={0} AfterScalar={1}",db,scalar);
        } finally {
            if(volume!=null) Marshal.ReleaseComObject(volume);
            if(pointer!=IntPtr.Zero) Marshal.Release(pointer);
            if(d!=null) Marshal.ReleaseComObject(d);
            Marshal.ReleaseComObject(e);
        }
    }
}
'@
Add-Type -TypeDefinition $source
[OutputLevel]::Run([float]$AttenuateDb)
