# N100 HDA register probe (2026-10-13)

The read-only register probe from [CI run 38371045258](https://github.com/chevydan6/windows-sof/actions/runs/38371045258) built with the Windows Driver Kit and passed a separate x64 `InfVerif /w` check. The three package files were hash-matched on the N100 before testing. As in the [first PCI probe test](n100-probe-test.md), the package's self-signed WDK certificate was trusted temporarily, test signing was enabled, and Windows was rebooted. The new signing certificate had thumbprint `A5D29821CEA710677446ABFF98239CFCD81885B0`.

The driver mapped the target's 16 KiB HDA memory resource read-only and read a fixed set of registers. It did not write device registers, touch the 1 MiB DSP resource, or initialize firmware, DMA, interrupts, or the codec. WDF saved the readings under the device instance's `Device Parameters` registry subkey:

| Register | Offset | Value | Initial interpretation |
| --- | ---: | ---: | --- |
| GCAP | `0x00` | `0x9701` | 64-bit DMA address support; 7 input and 9 output streams |
| VMIN / VMAJ | `0x02` / `0x03` | `0` / `1` | HDA controller version 1.0 |
| GCTL | `0x08` | `0x00000001` | Controller out of reset |
| STATESTS | `0x0e` | `0x0000` | No HDA codec state-change bits set at read time |
| GCAP2 | `0x12` | `0x0001` | Recorded without interpretation |
| LLCH | `0x14` | `0x00000c00` | Extended capability list begins at offset `0x0c00` |

Offsets and bit meanings come from Linux's [HDA register definitions](https://github.com/torvalds/linux/blob/master/include/sound/hda_register.h). `pnputil` reported the device `Started` with the register probe installed, and `sc.exe` reported the `sofprobe` service running. A separate `Win32_PnPSignedDriver` query returned an empty provider field even while the device was running; `Get-PnpDeviceProperty` and `pnputil` showed the actual binding. The register values above came from the driver's WDF registry writes, not from the PnP resource list.

After capture, we removed the probe package, deleted its seven diagnostic registry values, removed the certificate from the machine Root and Trusted Publisher stores, removed the test-signing boot value, and rebooted. Verification found no probe service or driver package, no test-certificate trust, no diagnostic values, and test signing off. `pnputil` reports the controller `Stopped` with no driver bound. Audio remains unavailable.

The next hardware step is to read the extended capability header at `0x0c00` and the Intel DSP identification/status registers, then design a guarded DSP power-up and firmware-ready handshake. Firmware startup is a separate, more invasive milestone.
