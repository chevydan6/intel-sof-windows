# SPDX-License-Identifier: GPL-2.0-only
# Query/off diagnostics only. The driver rejects application enable requests.
param([switch]$Stop,[switch]$CheckUserEnableRejected)
$ErrorActionPreference='Stop'
if (!('SofAmpClient' -as [type])) {
Add-Type -TypeDefinition @'

using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class SofAmpClient {
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
        Guid guid = new Guid("1d4a273b-20e6-49eb-b7ca-1936ea22c754");
        uint length;
        uint result = CM_Get_Device_Interface_List_SizeW(out length, ref guid, null, 0);
        if (result != 0) throw new Exception("Interface list size: CONFIGRET " + result);
        char[] buffer = new char[length];
        result = CM_Get_Device_Interface_ListW(ref guid, null, buffer, length, 0);
        if (result != 0) throw new Exception("Interface list: CONFIGRET " + result);
        return new string(buffer).Split(new char[]{'\0'}, StringSplitOptions.RemoveEmptyEntries);
    }
    public static uint[] Exchange(string path, uint code) {
        using (SafeFileHandle handle = CreateFileW(path, code == 0x226500U ? 0x80000000U : 0x40000000U, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes = new byte[48]; uint returned;
            if (!DeviceIoControl(handle, code, IntPtr.Zero, 0, bytes, 48, out returned, IntPtr.Zero))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if (returned != 48) throw new Exception("Unexpected amplifier status length");
            uint[] words = new uint[12];
            for (int i=0;i<12;i++) words[i]=BitConverter.ToUInt32(bytes,i*4);
            return words;
        }
    }
}

'@
}
$paths=@([SofAmpClient]::Paths())
if($paths.Count -ne 1){throw 'Expected exactly one guarded amplifier interface'}
$code=if($Stop){[uint32]0x22A504}else{[uint32]0x226500}
$v=[SofAmpClient]::Exchange($paths[0],$code)
if($v[0]-ne 1){throw 'Unexpected amplifier ABI'}
if($CheckUserEnableRejected){
 if($v[2]-ne 0 -or $v[4]-ne 1 -or $v[5]-ne 0){throw 'Enable rejection check requires untouched off state'}
 foreach($forbidden in @([uint32]0x22A508,[uint32]0x22A50C)){
  $rejected=$false
  try{[SofAmpClient]::Exchange($paths[0],$forbidden)|Out-Null}
  catch{
   $e=$_.Exception
   while($e.InnerException){$e=$e.InnerException}
   if($e -is [ComponentModel.Win32Exception] -and $e.NativeErrorCode -eq 5){$rejected=$true}else{throw}
  }
  if(!$rejected){throw 'Driver unexpectedly accepted a user-mode enable/renew request'}
 }
 $v=[SofAmpClient]::Exchange($paths[0],[uint32]0x226500)
 if($v[2]-ne 0 -or $v[4]-ne 1 -or $v[5]-ne 0){throw 'Amplifier state changed during authorization test'}
 Write-Host 'VERIFIED: application START/RENEW rejected; no accepted high request'
}
[pscustomobject]@{Version=$v[0];Ready=$v[1];Active=$v[2];Faulted=$v[3];OutputRequest=$v[4];OnWrites=$v[5];OffWrites=$v[6];WatchdogTrips=$v[7];FaultStatus=('0x{0:X8}' -f $v[8]);CleanupStatus=('0x{0:X8}' -f $v[9]);LeaseMs=$v[10];MaximumMs=$v[11]}
