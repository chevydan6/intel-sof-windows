# SPDX-License-Identifier: GPL-2.0-only
# Fixed readback and shutdown only. Does not enable output or write arbitrary registers.
param([switch]$Stop)
$ErrorActionPreference = 'Stop'
if (!('HeadphoneCodecClient' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class HeadphoneCodecClient {
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
        Guid guid = new Guid("128f66be-4874-44dd-9270-90a8f79e2834");
        uint length;
        uint result = CM_Get_Device_Interface_List_SizeW(out length, ref guid, null, 0);
        if (result != 0) throw new Exception("Interface list size: CONFIGRET " + result);
        char[] buffer = new char[length];
        result = CM_Get_Device_Interface_ListW(ref guid, null, buffer, length, 0);
        if (result != 0) throw new Exception("Interface list: CONFIGRET " + result);
        return new string(buffer).Split(new char[]{'\0'}, StringSplitOptions.RemoveEmptyEntries);
    }
    public static uint[] Exchange(string path, uint code) {
        using (SafeFileHandle handle = CreateFileW(path, code == 0x226400U ? 0x80000000U : 0x40000000U, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes = new byte[36]; uint returned;
            if (!DeviceIoControl(handle, code, IntPtr.Zero, 0, bytes, 36, out returned, IntPtr.Zero))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if (returned != 36) throw new Exception("Unexpected codec status length");
            uint[] words = new uint[9];
            for (int i=0;i<9;i++) words[i]=BitConverter.ToUInt32(bytes,i*4);
            return words;
        }
    }
}
'@
}
$paths = @([HeadphoneCodecClient]::Paths())
if ($paths.Count -ne 1) { throw 'Expected exactly one headphone codec interface' }
$code = if ($Stop) { [uint32]0x22A408 } else { [uint32]0x226400 }
$v = [HeadphoneCodecClient]::Exchange($paths[0],$code)
if ($v[0] -ne 1 -or $v[1] -ne 0x6530 -or $v[2] -ne 0x10ec -or $v[8] -ne 0) { throw 'Codec identity/status check failed' }
if (($v[5] -band 0x8080) -ne 0x8080 -or ($v[6] -band 0xe000) -ne 0x8000) { throw 'Headphone mutes not asserted' }
if ($Stop -and ($v[3] -ne 0 -or $v[4] -ne 0 -or $v[7] -ne 0)) { throw 'Codec shutdown not verified' }
[pscustomobject]@{Version=$v[0];DeviceId=('0x{0:X4}' -f $v[1]);VendorId=('0x{0:X4}' -f $v[2]);Initialized=$v[3];Prepared=$v[4];HpMute=('0x{0:X4}' -f $v[5]);DacControl=('0x{0:X4}' -f $v[6]);DigitalPower=('0x{0:X4}' -f $v[7]);LastStatus=('0x{0:X8}' -f $v[8])}
