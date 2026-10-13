# Read Core Audio's actual endpoint format properties through IPropertyStore.
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface EPDeviceEnumerator {
    [PreserveSig] int EnumAudioEndpoints(int flow, uint mask, out IntPtr list);
    [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out EPDevice device);
    [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out EPDevice device);
    [PreserveSig] int RegisterEndpointNotificationCallback(IntPtr callback);
    [PreserveSig] int UnregisterEndpointNotificationCallback(IntPtr callback);
}
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface EPDevice {
    [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr parameters, out IntPtr obj);
    [PreserveSig] int OpenPropertyStore(uint access, out EPPropertyStore store);
    [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
    [PreserveSig] int GetState(out uint state);
}
[StructLayout(LayoutKind.Sequential)]
public struct EPPropertyKey { public Guid FormatId; public uint PropertyId; }
[StructLayout(LayoutKind.Explicit, Size=24)]
public struct EPPropertyValue {
    [FieldOffset(0)] public ushort VariantType;
    [FieldOffset(8)] public uint BlobSize;
    [FieldOffset(16)] public IntPtr BlobData;
}
[ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface EPPropertyStore {
    [PreserveSig] int GetCount(out uint count);
    [PreserveSig] int GetAt(uint index, out EPPropertyKey key);
    [PreserveSig] int GetValue(ref EPPropertyKey key, out EPPropertyValue value);
    [PreserveSig] int SetValue(ref EPPropertyKey key, ref EPPropertyValue value);
    [PreserveSig] int Commit();
}
public static class EPPropertyCheck {
    [DllImport("ole32.dll")] public static extern int PropVariantClear(ref EPPropertyValue value);
    public static void Run() {
        var type = Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"));
        var e = (EPDeviceEnumerator)Activator.CreateInstance(type);
        EPDevice device;
        int hr = e.GetDefaultAudioEndpoint(0, 1, out device);
        Console.WriteLine("Default endpoint HR=0x{0:X8}", hr);
        if (hr != 0) return;
        EPPropertyStore store;
        hr = device.OpenPropertyStore(0, out store);
        Console.WriteLine("OpenPropertyStore HR=0x{0:X8}", hr);
        if (hr != 0) return;
        Read(store, "OEMFormat", "e4870e26-3cc5-4cd2-ba46-ca0a9a70ed04", 3);
        Read(store, "DeviceFormat", "f19f064d-082c-4e27-bc73-6882a1bb8e4c", 0);
    }
    static void Read(EPPropertyStore store, string name, string guid, uint id) {
        EPPropertyKey key = new EPPropertyKey { FormatId = new Guid(guid), PropertyId = id };
        EPPropertyValue value;
        int hr = store.GetValue(ref key, out value);
        Console.WriteLine("{0} HR=0x{1:X8} VT={2} BlobSize={3}", name, hr, value.VariantType, value.BlobSize);
        if (hr == 0 && value.VariantType == 65 && value.BlobSize <= 256 && value.BlobData != IntPtr.Zero) {
            var data = new byte[value.BlobSize];
            Marshal.Copy(value.BlobData, data, 0, data.Length);
            Console.WriteLine("{0} data={1}", name, BitConverter.ToString(data));
        }
        if (hr == 0) PropVariantClear(ref value);
    }
}
'@
[EPPropertyCheck]::Run()
