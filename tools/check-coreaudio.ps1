# SPDX-License-Identifier: GPL-2.0-only
# Read-only Core Audio endpoint and activation diagnostic.
$ErrorActionPreference = 'Stop'
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
public static class CoreAudioCheck {
    public static void Run() {
        var type = Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"));
        IMMDeviceEnumerator e = (IMMDeviceEnumerator)Activator.CreateInstance(type);
        IMMDeviceCollection list;
        int h = e.EnumAudioEndpoints(0, 1, out list);
        Console.WriteLine("Enum HR=0x{0:X8}", h);
        if (h == 0) {
            uint count; h = list.GetCount(out count);
            Console.WriteLine("Active render count={0} HR=0x{1:X8}", count, h);
            for (uint i=0; i<count; ++i) {
                IMMDevice dev; h = list.Item(i, out dev);
                if (h == 0) { string id; uint state; dev.GetId(out id); dev.GetState(out state);
                    Console.WriteLine("Device {0}: {1}, state={2}", i, id, state); }
            }
        }
        IMMDevice d;
        h = e.GetDefaultAudioEndpoint(0, 1, out d);
        Console.WriteLine("Default render HR=0x{0:X8}", h);
        if (h == 0) {
            string id; uint state; d.GetId(out id); d.GetState(out state);
            Console.WriteLine("Default ID={0}, state={1}", id, state);
            Guid iid = new Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2");
            IntPtr client; h = d.Activate(ref iid, 23, IntPtr.Zero, out client);
            Console.WriteLine("IAudioClient Activate HR=0x{0:X8}", h);
            if (h == 0) {
                IAudioClient audio = (IAudioClient)Marshal.GetTypedObjectForIUnknown(client, typeof(IAudioClient));
                IntPtr mix; h = audio.GetMixFormat(out mix);
                Console.WriteLine("GetMixFormat HR=0x{0:X8}", h);
                long defaultPeriod, minimumPeriod;
                int periodHr = audio.GetDevicePeriod(out defaultPeriod, out minimumPeriod);
                Console.WriteLine("GetDevicePeriod HR=0x{0:X8}, default={1}, minimum={2}",
                    periodHr, defaultPeriod, minimumPeriod);
                if (h == 0) {
                    Console.WriteLine("Mix format tag={0}, channels={1}, rate={2}, block={3}, bits={4}",
                        Marshal.ReadInt16(mix,0), Marshal.ReadInt16(mix,2), Marshal.ReadInt32(mix,4),
                        Marshal.ReadInt16(mix,12), Marshal.ReadInt16(mix,14));
                    Guid session = Guid.Empty;
                    h = audio.Initialize(0, 0, 1000000, 0, mix, ref session);
                    Console.WriteLine("Initialize HR=0x{0:X8}", h);
                    if (h == 0) { uint frames; audio.GetBufferSize(out frames); Console.WriteLine("Frames={0}", frames); }
                    Marshal.FreeCoTaskMem(mix);
                }
                if (h != 0) {
                    byte[] fmt = new byte[40];
                    BitConverter.GetBytes((ushort)0xFFFE).CopyTo(fmt, 0);
                    BitConverter.GetBytes((ushort)2).CopyTo(fmt, 2);
                    BitConverter.GetBytes((uint)48000).CopyTo(fmt, 4);
                    BitConverter.GetBytes((uint)384000).CopyTo(fmt, 8);
                    BitConverter.GetBytes((ushort)8).CopyTo(fmt, 12);
                    BitConverter.GetBytes((ushort)32).CopyTo(fmt, 14);
                    BitConverter.GetBytes((ushort)22).CopyTo(fmt, 16);
                    BitConverter.GetBytes((ushort)32).CopyTo(fmt, 18);
                    BitConverter.GetBytes((uint)3).CopyTo(fmt, 20);
                    new Guid("00000001-0000-0010-8000-00AA00389B71").ToByteArray().CopyTo(fmt, 24);
                    IntPtr native = Marshal.AllocHGlobal(40);
                    Marshal.Copy(fmt, 0, native, 40);
                    IntPtr closest;
                    int supportedHr = audio.IsFormatSupported(0, native, out closest);
                    Console.WriteLine("IsFormatSupported explicit S32 HR=0x{0:X8}", supportedHr);
                    if (closest != IntPtr.Zero) Marshal.FreeCoTaskMem(closest);
                    Guid session = Guid.Empty;
                    int hr = audio.Initialize(0, 0, 1000000, 0, native, ref session);
                    Console.WriteLine("Initialize explicit S32 HR=0x{0:X8}", hr);
                    Marshal.FreeHGlobal(native);
                }
                Marshal.Release(client);
            }
        }
    }
}
'@
Add-Type -TypeDefinition $source
[CoreAudioCheck]::Run()
