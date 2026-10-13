# N100 ACX automatic firmware startup

Later listening results and the candidate echo correction are recorded in [the playback echo investigation](n100-acx-echo-test.md). The results below describe the earlier automatic-startup tests.

The previous ACX driver could transfer a Windows WAV only after a PowerShell test staged firmware and prepared the SOF graph while Windows audio services were stopped. The driver now performs those operations from `EvtDeviceD0Entry`, before servicing streams. The firmware image remains a separate locally supplied file at `C:\Windows\System32\drivers\sof-adl-n-ipc3.ri`; it is not bundled in the repository. The driver checks its exact length and SHA-256 (`8433390e4abfe40b251ff3dec41604e7b817501d1097d51aa2f51c11444d3bf0`) before staging any firmware DMA.

The device-start path reuses the bounded hardware routines used by the earlier probe. It builds the same fixed IPC3 host/buffer/SSP1 speaker graph, allocates a silent PCM ring, and configures PCM before declaring the device ready. D0 exit invalidates buffer access, stops playback and firmware, and restores the observed hardware baseline. The ACX build rejects the destructive diagnostic IOCTLs now that it owns its live hardware session; read-only snapshots and ACX status remain available. Standalone probe builds keep their diagnostic commands.

Packet copies and position reads use a device-owned readiness flag and spin lock so stream objects no longer retain raw DMA pointers across device shutdown. Passive startup/run/pause transitions use a separate wait lock. Failed D0 entry explicitly unwinds the firmware session: [WDF does not call D0Exit after a failed D0Entry](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry).

## Initial hardware results

WDK build `38451873748`, commit `ca96241`, passed compilation. It was installed with Windows audio services left running.

- Missing firmware: the device failed startup with stage 1 and `0xC0000034` (`STATUS_OBJECT_NAME_NOT_FOUND`). Windows stayed reachable. No firmware DMA was started.
- After copying the already validated firmware image and restarting the device: controller `OK`, startup stage 6, status zero, and an active speaker endpoint.
- `GetMixFormat`, `GetDevicePeriod`, and `Initialize` returned `S_OK`; the audio client received 4,800 frames.
- The first ordinary WAV invocation reached the driver (`RunCalls=1`, 310 packets submitted, 309 completion notifications). The initial test wrapper misreported a null child-process exit code; it was corrected to own a `System.Diagnostics.Process` directly. The status reader was also updated for the extended status structure.
- A subsequent WAV created another stream but failed in Run with `0xC000009C` (`STATUS_DEVICE_DATA_ERROR`), and no second hardware run occurred. This exposed the need to free/reapply SOF PCM parameters and reset the HDA ring before a new stream.

Commit `769c8eb`, WDK build `38452731227`, adds that PCM reset between streams and explicit timer-handle ownership. Status version 3 records the number of PCM resets and the DMA position immediately before RUN. `tools/test-acx-autostart.ps1` plays bounded three-second WAVs without staging firmware or stopping audio services, checks increasing packet/run counters, and requires each stream's starting hardware position to be zero. Hardware results for this build follow below.

## Scope

This is a development driver for the tested N100/PUJJOTEEN speaker route. Microphone/headphone routing, volume controls, interrupt-driven IPC, end-of-stream details, and comprehensive suspend/resume recovery are still unfinished. Packet counters establish application-to-driver transfer and DMA progress; audible confirmation remains separate.

## Repeated playback with PCM reset

Build `38452731227` installed cleanly as `oem23.inf`, with controller and endpoint `OK`. Both Windows audio services stayed running/automatic. Core Audio format, period, and initialization calls returned `S_OK`.

Three consecutive independent WAV player processes completed. The cumulative results were:

| Playback | Runs | Submitted packets | Completion notifications | PCM resets | Position before RUN |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 309 | 308 | 0 | 0 |
| 2 | 2 | 617 | 615 | 1 | 0 |
| 3 | 3 | 925 | 922 | 2 | 0 |

Startup count stayed at 1, startup stage at 6, and hardware-ready at 1. No callback failure was recorded. Hardware stream status was `0x20` (no error bits in `0x1c`). This verifies consecutive stream startup after the PCM reset correction; it does not certify end-of-stream sample accuracy. The user was asked to confirm the three tones audibly. A full Windows restart test is the next check.

## Full Windows reboot

With the driver and firmware left installed, Windows was restarted normally. Both audio services came up running/automatic. No staging command, graph-creation script, service restart, or user sign-in was required by the test. Core Audio again returned `S_OK` for format, period, and stream initialization.

After boot, startup count was 1, stage was 6, and hardware was ready. A stream had already run before the explicit test (`RunCalls=1`, 334 packets submitted). Three additional independent WAV playbacks passed; their final cumulative values were four runs, 1,260 packets submitted, 1,260 completion notifications, three PCM resets, position-before-RUN zero, and no recorded callback failure. The driver stayed on the same automatically initialized firmware session throughout.

Final machine state: the tested `38452731227` package remains installed as `oem23.inf`; the controller reports `OK`, and the pinned firmware file remains in the documented location. Test signing and the ACX16 test signer remain enabled for this development package. The superseded ACX15 signer was removed. A recovery script was staged at `C:\Users\dan\sofplay-acx16-test\rollback-acx16-test.ps1`; when run elevated, it removes the test package, test trust, test-signing boot setting, and this test's pinned firmware copy, after which Windows must be rebooted. It has not been invoked because the tested driver is being left installed for use and audible verification. Audible confirmation and sleep/resume testing remain pending.
