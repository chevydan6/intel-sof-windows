# N100 read-only PCI probe test (2026-10-13)

The development package from [CI run 38368597210](https://github.com/chevydan6/intel-sof-windows/actions/runs/38368597210) was tested on the N100 Windows 11 target. The package had already built with the WDK and passed a separate x64 `InfVerif /w` check. Its files matched the staged hashes before the test:

- `sofprobe.sys`: `dcdc50755292ecee3d1b1ecb4fe342eba202cfe1bfb1e14a527c628ea79b4885`
- `sofprobe.inf`: `b13b38276ce8b34b40ad9383665d617e07962d076f6b6e62c25c955bb19d157e`
- `sofprobe.cat`: `f0aa05564de22c56a2f893548508cbd2473e6670e18865bf39e9bb1ad45ee4ff`

Before installation, the controller `PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB` had no driver and reported `CM_PROB_FAILED_INSTALL`. Secure Boot and BitLocker were off. We exported the BCD store, trusted only the package's self-signed WDK test certificate (`67B15485B1D3C680B09B6990CC689C35213D4939`) in the local machine Root and Trusted Publisher stores, and confirmed that both the `.sys` and `.cat` Authenticode signatures became `Valid`. We enabled test signing and rebooted.

`pnputil /add-driver sofprobe.inf /install` installed `oem23.inf` on the exact device. Windows then reported `CM_PROB_NONE`; `pnputil /enum-devices /resources` reported `Status: Started`, IRQ `0x17` (23), and two memory ranges: `0xBEEAC000–0xBEEAFFFF` and `0xBEF00000–0xBEFFFFFF`. This confirms that the KMDF probe can bind and start on the target. It does not confirm firmware boot, SOF IPC, codec operation, or audio playback. The probe's `KdPrintEx` messages were not captured, so the resource data above is from Windows PnP, not a kernel debug trace.

We then uninstalled and deleted `oem23.inf`, removed the test-signing BCD value and both certificate trust entries, and rebooted. The probe service and package are absent, test signing is off, and `pnputil` reports the controller `Stopped` with no driver provider. `Get-PnpDevice` reports `OK`/`CM_PROB_NONE` after rollback despite `pnputil` reporting `Stopped` and no driver; do not use that status alone to infer that audio is working.

The scripts in `tools/` document the preflight, preparation, installation, rollback, and final checks. They include hashes and the test-certificate thumbprint specific to this one CI artifact and should be updated for any new build.
