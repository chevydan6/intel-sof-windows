# Windows SOF host driver (Alder Lake-N first)

Open-source Windows host-driver development for Intel Sound Open Firmware (SOF).
The first target is an Intel N100 laptop with a PCI audio controller at
`8086:54c8` and an I2C Realtek RT5682 codec (`ACPI\10EC5682`). This repository
currently contains the verified hardware baseline and diagnostic tooling; it
does **not** yet contain an installable audio driver.

The read-only PCI probe builds on a Windows 2022 runner with the WDK, and its
generated INF passes a separate x64 `InfVerif /w` check. It loaded on the N100,
reported the controller's assigned resources, HDA capabilities, and powered-off
DSP status. All test packages were removed and the laptop was
returned to normal boot settings. See the [PCI resource](docs/n100-probe-test.md),
[HDA register](docs/n100-register-test.md), and
[DSP status](docs/n100-dsp-status-test.md) test records. The complete
[capability chain](docs/n100-capability-chain-test.md) was also captured.
Audio still does not
work: the probe does not load firmware or expose audio streams.

## Why a host driver is needed

SOF firmware runs on the audio DSP. A Windows host driver must claim the PCI
controller, initialize the DSP, load compatible firmware and topology, exchange
SOF IPC messages, manage DMA and power states, and expose audio streams through
Windows audio interfaces. The Realtek codec is a separate I2C device that also
needs a working path to the audio graph.

## First target

The initial target baseline reported Windows PnP problem code 28 for both
`PCI\VEN_8086&DEV_54C8` and `ACPI\10EC5682`. Linux maps `54c8` to Alder Lake-N
SOF (`HDA_ADL_N`) and defaults that platform to IPC3 with `sof-adl-n.ri`.
See [hardware/n100-windows.json](hardware/n100-windows.json) and
[docs/source-map.md](docs/source-map.md).

## Development order

1. Build a read-only KMDF PCI probe that binds only to `8086:54c8`, reports
   BARs, interrupt resources, and controller capabilities, then unloads cleanly.
2. Add DSP boot and firmware-ready handshake using the Alder Lake-N Linux SOF
   code as the hardware reference. Keep a known-good Windows restore path.
3. Add IPC3 request/reply handling, timeouts, and firmware trace collection.
4. Bring up one simple playback path, then capture and codec controls through
   Windows ACX audio circuits.
5. Add suspend/resume, error recovery, validation, and driver signing for wider
   use.

Run `tools/collect-hardware.ps1` on the Windows target to refresh the PnP
baseline. The Windows Driver Kit is not installed on the target, so initial
builds use Windows CI. Test-signed builds require a controlled test-signing
setup on the target for each hardware test.

## Licensing

This project is GPL-2.0-only so that appropriately attributed Linux SOF host
code can be adapted where useful. Third-party firmware and Windows Driver Kit
components keep their own licenses; neither is bundled here.
