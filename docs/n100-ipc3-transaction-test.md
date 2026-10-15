# N100 guarded IPC3 request/reply test

On 2026-10-13, the N100's temporarily installed, test-signed probe completed its first host-initiated IPC3 exchange. This was a bounded one-shot test: it booted the locally validated `sof-adl-n.ri` image, read the 108-byte `FW_READY` record, acknowledged that notification, sent a read-only memory-usage request, captured the reply, and reset/powered down the DSP before returning. The WDK build and independent INF verification passed in [CI run 38395066054](https://github.com/chevydan6/intel-sof-windows/actions/runs/38395066054).

The firmware's `FW_READY` base record reported zero for its mailbox offset fields. The validated IPC3 `XMan` window descriptor supplied the addresses instead: uplink at DSP BAR + `0x81000` (4 KiB) and downlink at DSP BAR + `0xa0000` (8 KiB). The probe used the downlink for an eight-byte `SOF_IPC_GLB_DEBUG | SOF_IPC_DEBUG_MEM_USAGE` request (`0xd0010000`), following Linux SOF's mailbox and CNL IPC doorbell ordering.

| Observation | Value |
| --- | --- |
| Firmware status | `0x00000005` (entered) |
| FW_READY acknowledgment | DSP busy cleared; host DONE asserted |
| Reply doorbell | `0x80000000` after 1 poll |
| Reply size / command / error | `292` bytes / `0xd0010000` / `0` |
| Reported memory zones | `13` |
| Probe result / cleanup flags | `0` / `0` |

An independent post-test register snapshot found the original idle output and capture streams, no stream errors, DSP power state `0x00000f0f`, processing-pipe control `0x40000000`, and clear host IPC registers. After the 21:12:24 reboot, rollback verification found the controller stopped and driverless, with no probe service, package, registry diagnostics, certificate trust, or test-signing option.

This confirms one firmware-originated notification and one host-originated request/reply. It does not yet implement a persistent DSP session, interrupt-driven IPC service, topology loading, or a Windows audio endpoint. The next driver step is a managed boot/session lifecycle with cleanup on PnP removal and power transitions, followed by repeatable IPC transactions and audio circuits.
