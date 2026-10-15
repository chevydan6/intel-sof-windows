# N100 managed IPC3 session test

On 2026-10-13, the test-signed SOF development probe kept the N100 audio DSP running across separate Windows client processes. The WDK build and independent INF verification passed in [CI run 38396721481](https://github.com/chevydan6/intel-sof-windows/actions/runs/38396721481).

The probe staged the locally validated IPC3 firmware and checked the idle controller baseline before boot. `StartSession` loaded firmware, acknowledged `FW_READY`, and received a read-only memory-usage reply. It then stopped the boot-only HDA code-loader DMA and restored the output stream while leaving the DSP active. A separate `SessionMemory` call received another memory-usage reply. `StopSession` reset and powered down the DSP; an independent preflight read found the original idle state.

| Step | Observed result |
| --- | --- |
| Firmware entered | `0x00000005` |
| Initial IPC3 reply | Command `0xd0010000`, 292 bytes, error 0 |
| Persistent session | Active after `StartSession` returned |
| Second IPC3 reply | Doorbell `0x80000000` after 1 poll; command `0xd0010000`, 292 bytes, error 0 |
| Explicit stop | ADSPCS `0x00000f0f` restored; cleanup flags 0 |
| Independent idle readback | Output and capture streams idle; PPCTL `0x40000000`; IPC doorbells clear; ADSPCS `0x00000f0f` |

The first run exposed a PowerShell client comparison bug: a high-bit IPC command literal compared as a signed value, so the client falsely reported failure despite successful hardware results. The client now compares unsigned values; the full start/query/stop sequence passed on a second test boot.

The driver also rejects unrelated diagnostics while a session is active and calls the stop routine from KMDF `EvtDeviceD0Exit`. An [active-session PnP restart](n100-ipc3-device-restart-test.md) subsequently returned the controller to its idle baseline and allowed a fresh session; surprise removal and system suspend remain untested. This is still a controlled development probe: one firmware boot attempt per device start, polling IPC, no firmware trace service, topology, codec setup, or Windows audio endpoint. After the 21:27:38 normal reboot, rollback verification found the controller driverless and stopped, with no probe service, driver package, registry diagnostics, signer trust, or test-signing option.
