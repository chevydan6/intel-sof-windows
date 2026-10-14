# Amplifier thermal investigation, 2026-10-14

Speaker playback remains stopped after Dan reported fuzzy/buzzy sound and very hot speakers. The initial read-only review identified missing amplifier enable control; it did not establish the thermal cause or whether a speaker is damaged. No playback, driver enable, installation, reboot, or GPIO write was performed during that initial review. A subsequent shutdown-only implementation and maintenance installation are recorded below.

Dan subsequently reports that the speakers seem to remain hot when not playing. AudioEndpointBuilder and Audiosrv were checked again and both are stopped. Asked for full shutdown, charger disconnection, and observation of whether the speaker area cools; that observation is pending. Residual heat versus ongoing heating has not been distinguished, and the actual heat source has not been measured. Do not interpret silence or disabled-device status as proof of amplifier electrical shutdown.

Dan then restarted the laptop. Windows reports LastBootUpTime `2026-10-14 10:15:23` (target local time). After reconnecting, the SOF device still reports problem code 22, confirming the disable persisted across restart. Windows audio services had automatically restarted. No playback or device enable was requested. Post-restart cooling remains unconfirmed.

Stopped both audio services again after this restart and verified both report Stopped. Their configured startup types were not changed.

Dan subsequently confirmed that the speaker area is cooling. This is an observation after the restart with the audio device disabled; it neither proves speaker integrity nor establishes the earlier heat source. Work moved to a shutdown-only GPIO client and an ACX build that holds all DSP startup/playback, described in [the amplifier driver notes](../driver/amp/README.md).

## Shutdown-only implementation and maintenance verification

Commit `7a6936d` adds an ACX thermal hold and a separate ACPI amplifier GPIO client. The client's first WDK build identified a missing `RESHUB_USE_HELPER_ROUTINES` definition; commit `c03754b` supplies it, matching Microsoft's sample. Local ASan/UBSan tests and the CI policy test passed. The held ACX WDK build and independent INF verification passed in [38495676945](https://github.com/chevydan6/windows-sof/actions/runs/38495676945). The amplifier WDK build and independent INF verification passed in [38495794914](https://github.com/chevydan6/windows-sof/actions/runs/38495794914).

Pinned package hashes and Authenticode signatures were verified before installation. The disabled parent was replaced with the held ACX package, then enabled to enumerate its amplifier child. Readback confirmed ThermalHold 1, startup stage 7, HardwareReady 0, zero stream runs/packets, HDA DMA RUN clear, and DSP ADSPCS `0x00000F0F`. This package is `oem26.inf`, version `1.26.8.27`. Parent status is OK while playback remains deliberately unavailable.

The shutdown-only amplifier package then installed as `oem27.inf`, version `1.27.25.292`. It validated the target's exact `_CRS`, acquired the translated GPIO connection, and reported SofAmpStage 3, SofAmpStatus 0, SofAmpOffWrites 1. Its PnP status is OK. This is evidence that the GPIO controller accepted the output-low request, not an independent pin-voltage or temperature reading. No request to raise EN exists in this driver.

Both audio services remain stopped. No reboot or sound test occurred. Packages, pinned hashes, installer, signer exports, and installation receipts are in `C:\Users\dan\sof-amp-maintenance` with a local copy under ignored `build/amp-maintenance`. Physical cooling after this new GPIO control, speaker integrity, and reboot/suspend behavior remain untested. Do not reinstall older autoplay packages or lift the source-level playback hold as part of routine continuation.

## Evidence from the target

Read the target's firmware tables through Windows GetSystemFirmwareTable while the SOF PCI device remained disabled. The ACPI audio scope identifies `MX98360A`, not a software-programmable smart amplifier. Its resource is an exclusive, output-only GPIO at `\\_SB.PCI0.GPIO`, pin `0x004B` (75). The `sdmode-gpio` property is active high; `sdmode-delay` is 5 ms. The scope is `\\_SB.PCI0.HDAS.MAXM`. The audio controller's properties identify speaker topology `max98360a`, headphone topology `rt5682`, and microphone topology `2ch-pdm0`.

Intel GPIO controller `ACPI\\INTC1057\\0` is present with status OK and service `iaLPSS2_GPIO2_ADL_N`. The SOF PCI device still reports problem code 22 (disabled). `ACPI\\MX98360A\\0` exists in the full PnP inventory but is not presently enumerated while the parent is disabled; that observation alone does not establish its earlier driver state. Audio services were verified stopped.

Original DSDT/SSDT and analysis files are local under ignored `build/amp-review/`. Full SSDT disassembly encountered duplicate `CNMT` names. To inspect the audio resources, the two complete HDAS Scope objects at original SSDT offsets 11791..12033 and 12033..12156 were copied unchanged into a separate, checksum-correct analysis table. That extract disassembled successfully. The original firmware tables were not edited and no table was installed on the laptop. GetSystemFirmwareTable exposes only the first instance of a repeated table signature; this is not a complete firmware dump.

## Missing control in the Windows prototype

The driver has no GPIO resource-hub connection or amplifier enable operation. Stream pause sends SOF STOP and stops HDA DMA; D0 exit additionally stops the firmware session and powers down the DSP. None of those operations explicitly drives the amplifier enable pin low. Windows reporting the audio device disabled therefore does not independently prove the physical enable pin is low or that amplifier temperature is safe.

The upstream Pujjo board configuration maps the amplifier control to GPP_A11 with the same 5 ms delay. Nissa baseboard GPIO initialization sets GPP_A11 (`EN_SPK_PA`) high. These upstream defaults corroborate the live ACPI mapping, but are not a readback of the current pin voltage.

Linux's MAX98357A/MAX98360A codec driver acquires the optional GPIO initially low, raises it after the configured delay on playback start, and lowers it on stop/suspend/pause. The Windows prototype is missing that explicit lifecycle control.

## What software can and cannot establish

The MAX98360 datasheet describes pin-selected gain and automatic digital-interface configuration, without an I2C register interface for temperature or fault telemetry. EN low places the output in high impedance. With EN high, no toggling on the digital input pins permits automatic standby. Thus an asserted enable pin alone does not establish the heat cause: continuing clocks, malformed audio, gain, physical damage, and other electrical faults still need to be distinguished. The device includes a DC-blocking filter, so a claim that a PCM DC offset necessarily reaches the speaker is not supported. Chip thermal protection is not a speaker-temperature measurement.

The next software correction is proper ownership of the ACPI-provided amplifier GPIO through Windows GPIO resources, with default-off behavior, controlled start timing, and shutdown on stop/failure/power transition. It must not use guessed raw pad-register writes. A build of that control path alone would not prove the reported thermal problem repaired. Keep powered speaker tests suspended until the electrical-output and thermal questions can be resolved.

## Primary references

- [MAX98360 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98360A-MAX98360D.pdf), especially enable/standby, input configuration, gain, and DC filter sections.
- [Linux codec lifecycle](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/max98357a.c).
- [Coreboot Pujjo amplifier configuration](https://github.com/coreboot/coreboot/blob/master/src/mainboard/google/brya/variants/pujjo/overridetree.cb).
- [Coreboot Nissa GPIO initialization](https://github.com/coreboot/coreboot/blob/master/src/mainboard/google/brya/variants/baseboard/nissa/gpio.c).
