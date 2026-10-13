# N100 active-session device-restart test

On 2026-10-13, the temporarily installed, test-signed probe started the N100 DSP firmware and left an IPC3 session active. Windows `pnputil /restart-device` then restarted only the SOF PCI controller. The restart completed successfully and the probe rebound with `CM_PROB_NONE`. An independent post-restart register snapshot found the original idle baseline: ADSPCS `0x00000f0f`, PPCTL `0x40000000`, idle capture and output streams, and clear host IPC doorbells.

A new staged firmware buffer then booted in the restarted device. `StartSession` returned firmware status `0x00000005` and a valid 292-byte IPC3 memory reply; a separate `SessionMemory` call received another valid reply after one poll. Explicit `StopSession` restored ADSPCS `0x00000f0f` with zero cleanup flags. This confirms the probe can recover from a PnP restart while a session was active and boot again without a system reboot.

The driver calls its session stop helper from KMDF `EvtDeviceD0Exit`. [Microsoft's KMDF callback sequence](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/wdm-irps-and-kmdf-event-callback-functions) places `EvtDeviceD0Exit` before `EvtDeviceReleaseHardware` on a device stop. The test did not capture the callback's own debug print, so the exact callback return fields were not observed; the successful PnP restart and idle readback are the external evidence. Surprise removal and system suspend remain untested.

After the 21:57:30 normal reboot, rollback verification found the controller driverless and stopped, with no probe service, driver package, registry diagnostics, signer trust, or test-signing option.
