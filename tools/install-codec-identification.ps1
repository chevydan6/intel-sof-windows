# Installs the pinned RT5682 muted-calibration package; never enables playback.
$ErrorActionPreference = 'Stop'
$parent = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$id = 'ACPI\10EC5682\0'
$hardware = 'ACPI\10EC5682'
$stem = 'sofrt5682'
$root = Join-Path $PSScriptRoot 'package'
function Assert-Hold {
 $reader = Join-Path $PSScriptRoot 'read-live-snapshot.ps1'
 . $reader -AcxStatus | Out-Host
 $paths = @([SofProbeClient]::Paths())
 if ($paths.Count -ne 1) { throw 'Expected one maintenance interface' }
 $v = [SofProbeClient]::Exchange($paths[0],0x22605C,$null)
 if ($v.Length -lt 25 -or $v[0] -notin @(7,8) -or $v[11] -ne 7 -or $v[13] -ne 0 -or $v[24] -ne 1 -or $v[2] -ne 0) { throw 'Playback hold verification failed' }
 if ($v[0] -eq 8 -and ($v.Length -lt 26 -or $v[25] -ne 1)) { throw 'Expected isolated headphone transport' }
 $s = [SofProbeClient]::Exchange($paths[0],0x226010,$null)
 if (($s[0] -band 2) -ne 0) { throw 'Unexpected HDA DMA RUN bit' }
 $d = [SofProbeClient]::Exchange($paths[0],0x226000,$null)
 if ($d[4] -ne 0xF0F) { throw ('Unexpected DSP power state 0x{0:X8}' -f $d[4]) }
 Write-Output 'VERIFIED: thermal hold, hardware not ready, no stream runs, HDA DMA stopped, DSP baseline 0x00000F0F'
}
try {
 Stop-Service Audiosrv -Force -ErrorAction Stop
 Stop-Service AudioEndpointBuilder -Force -ErrorAction Stop
 if ((bcdedit /enum '{current}' | Out-String) -notmatch '(?im)^testsigning\s+Yes\s*$') { throw 'Test signing not active' }
 Assert-Hold
 $amp = Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\MX98360A\0\Device Parameters'
 if ($amp.SofAmpStage -ne 3 -or $amp.SofAmpStatus -ne 0 -or $amp.SofAmpOffWrites -lt 1) { throw 'Amplifier shutdown request not verified' }
 $service = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Service -ErrorAction SilentlyContinue).Data
 if ($service -and $service -ne 'sofrt5682') { throw 'A different codec driver owns this device' }
 $device = @(Get-PnpDevice -PresentOnly | Where-Object InstanceId -eq $id)
 if ($device.Count -ne 1) { throw "Expected exact present device $id" }
 $ids = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_HardwareIds).Data
 if ($hardware -notin $ids) { throw 'Target hardware identity mismatch' }
 $expected = Get-Content (Join-Path $root 'expected.json') -Raw | ConvertFrom-Json
 foreach ($entry in $expected.PSObject.Properties) {
  if ((Get-FileHash (Join-Path $root $entry.Name) -Algorithm SHA256).Hash -ne $entry.Value) { throw "Hash mismatch: $($entry.Name)" }
 }
 $sys = Join-Path $root "$stem.sys"
 $cat = Join-Path $root "$stem.cat"
 $inf = Join-Path $root "$stem.inf"
 $cert = (Get-AuthenticodeSignature $sys).SignerCertificate
 if (!$cert -or $cert.Thumbprint -ne (Get-AuthenticodeSignature $cat).SignerCertificate.Thumbprint -or $cert.Subject -ne $cert.Issuer -or $cert.NotAfter -lt (Get-Date)) { throw 'Unexpected package signer' }
 $cer = Join-Path $root 'maintenance-signer.cer'
 [IO.File]::WriteAllBytes($cer,$cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
 Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
 Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
 if ((Get-AuthenticodeSignature $sys).Status -ne 'Valid' -or (Get-AuthenticodeSignature $cat).Status -ne 'Valid') { throw 'Package signature invalid' }
 Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MaintenanceDriverUpdate {
 [DllImport("newdev.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool UpdateDriverForPlugAndPlayDevicesW(IntPtr hwnd, string hardwareId, string inf, uint flags, [MarshalAs(UnmanagedType.Bool)] out bool reboot);
}
'@
 $oldInf = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath -ErrorAction SilentlyContinue).Data
 $reboot = $false
 # Force exact reviewed package; timestamp-stamped version may sort below an
 # older build from another time of day. No reboot is requested or performed.
 if (![MaintenanceDriverUpdate]::UpdateDriverForPlugAndPlayDevicesW([IntPtr]::Zero,$hardware,$inf,1,[ref]$reboot)) {
  throw ('Driver update failed: ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
 }
 if ($reboot) { throw 'Driver update requires reboot; leaving codec disabled, no automatic reboot' }
 $newInf = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath).Data
 if ((Get-FileHash (Join-Path $env:windir "INF\$newInf") -Algorithm SHA256).Hash -ne $expected."$stem.inf") { throw 'Installed INF does not match pinned package' }
 [pscustomobject]@{Package=$stem;PreviousInf=$oldInf;InstalledInf=$newInf;Signer=$cert.Thumbprint;RebootRequired=$reboot} | ConvertTo-Json | Set-Content (Join-Path $root 'install-result.json')
 if ((Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_ProblemCode).Data -eq 22) {
  Enable-PnpDevice -InstanceId $id -Confirm:$false -ErrorAction Stop
 }
 Assert-Hold
 $p = Get-ItemProperty -LiteralPath "Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\$id\Device Parameters"
 $p | Select-Object CodecStage,CodecStatus,CodecDeviceId,CodecVendorId,CodecVersionId,CodecReads,CodecInitialized,CodecHpMute,CodecDacControl,CodecDigitalPower | Format-List
 if ($p.CodecStage -ne 5 -or $p.CodecStatus -ne 0 -or $p.CodecDeviceId -ne 0x6530 -or $p.CodecReads -lt 9 -or $p.CodecInitialized -ne 1 -or ($p.CodecHpMute -band 0x8080) -ne 0x8080 -or ($p.CodecDacControl -band 0xe000) -ne 0x8000 -or $p.CodecDigitalPower -ne 0) { throw 'Muted codec calibration not verified' }
 Get-PnpDevice -InstanceId $id | Format-List Status,FriendlyName,InstanceId
 Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion | Format-Table KeyName,Data
 Get-Service Audiosrv,AudioEndpointBuilder | Format-Table Name,Status
} catch {
 [Console]::Error.WriteLine($_.ToString())
 Disable-PnpDevice -InstanceId $id -Confirm:$false -ErrorAction Continue
 exit 1
}
