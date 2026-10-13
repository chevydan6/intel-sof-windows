# N100 guarded IPC3 firmware-ready test

On 2026-10-13, a temporary test-signed probe booted the locally validated `sof-adl-n.ri` firmware on the N100's `8086:54c8` controller and captured its first IPC3 message. The probe uses the same bounded ROM entry, HDA code-loader DMA, and SPIB setup described in the [firmware-transfer test](n100-firmware-dma-test.md). It does not leave firmware running after the IOCTL returns.

The first firmware-ready probe ([CI run 38389796118](https://github.com/chevydan6/windows-sof/actions/runs/38389796118)) received a 108-byte uplink mailbox message with command `0x70000000`, but reported cleanup result 8. A follow-up ([CI run 38390699128](https://github.com/chevydan6/windows-sof/actions/runs/38390699128)) isolated the mismatch to three SSP registers: each read `0xffffffff` after firmware startup, while output, capture, DSP, SPIB, and IPC state restored to the prior values. The SSP blocks had become inaccessible after firmware boot. The probe now records that condition and skips writes to inaccessible registers; accessible SSP values still require exact restoration.

The final probe ([CI run 38391560316](https://github.com/chevydan6/windows-sof/actions/runs/38391560316)) returned:

| Observation | Value |
| --- | --- |
| Firmware status / error | `0x00000005` / `0` |
| IPC transmit doorbell / extension | `0xf0000000` / `0x00000080` |
| Uplink mailbox size / command | `108` bytes / `0x70000000` |
| FW_READY polls | `1` |
| IPC doorbell / acknowledgment after cleanup | `0` / `0` |
| SSP unavailable mask | `0x00000007` (all three blocks) |
| Cleanup flags / driver result | `0` / `0` |

Independent read-only snapshots after the test found the original idle output and capture stream controls, zero stream errors, DSP power state `0x00000f0f`, processing-pipe control `0x40000000`, and clear host IPC registers. The test package and its signing certificate were then removed. After the 20:44:43 reboot, the controller was stopped and driverless, with no probe service, package, registry values, certificate trust, or test-signing setting.

This establishes firmware entry and receipt of the IPC3 `FW_READY` notification during a bounded test. The next driver step is to keep the DSP running across initialization, acknowledge and service IPC3 messages, add request/reply timeouts and recovery, and only then expose audio circuits. No Windows audio endpoint is provided yet.
