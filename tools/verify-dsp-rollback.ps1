$ErrorActionPreference = 'Stop'
trap { [Console]::Error.WriteLine($_.ToString()); exit 1 }
$instance = 'PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB'
$thumbprint = '0B102F40EA8F53AABA252CF9D09AD70797E84E98'
$device = Get-PnpDevice -InstanceId $instance
$boot = bcdedit /enum '{current}' | Out-String
$driver = Get-CimInstance Win32_PnPSignedDriver | Where-Object DeviceID -EQ $instance
$deviceDetails = pnputil /enum-devices /instanceid $instance /drivers | Out-String
$root = Test-Path "Cert:\LocalMachine\Root\$thumbprint"
$publisher = Test-Path "Cert:\LocalMachine\TrustedPublisher\$thumbprint"
$listed = (pnputil /enum-drivers /class System | Out-String) -match '(?i)sofprobe|Open SOF Windows Project'
$serviceListed = (sc.exe query sofprobe 2>&1 | Out-String) -notmatch '1060'
$key = "HKLM:\SYSTEM\CurrentControlSet\Enum\$instance\Device Parameters"
$valueNames = @((Get-ItemProperty -LiteralPath $key).PSObject.Properties.Name |
    Where-Object { $_ -like 'SofProbe*' })
[pscustomobject]@{
    BootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
    ControllerStatus = $device.Status
    ControllerProblem = $device.Problem
    DriverProvider = $driver.DriverProviderName
    PnpUtilStopped = ($deviceDetails -match '(?im)^Status:\s+Stopped\s*$')
    ProbeServiceListed = $serviceListed
    TestSigningEnabled = ($boot -match '(?im)^testsigning\s+Yes\s*$')
    TestRootTrusted = $root
    TestPublisherTrusted = $publisher
    ProbePackageListed = $listed
    ProbeRegisterValues = ($valueNames -join ', ')
} | Format-List
if ($deviceDetails -notmatch '(?im)^Status:\s+Stopped\s*$' -or
    $driver.DriverProviderName -or $serviceListed -or $root -or $publisher -or $listed -or
    $valueNames.Count -gt 0 -or
    $boot -match '(?im)^testsigning\s+Yes\s*$') {
    throw 'Rollback verification failed'
}
