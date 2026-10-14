# PUJJOTEEN amplifier shutdown-only prototype

This development package is for investigating the reported speaker overheating.
It is **not a playback driver or a verified thermal repair**. Do not install the
older automatic-playback ACX packages to make its ACPI child enumerate.

`sofamp` binds to `ACPI\MX98360A`. Before opening GPIO it requires exactly one
translated GPIO I/O connection and compares the evaluated `_CRS` against the
42-byte resource captured from this target: exclusive output-only pin 75 on
`\_SB.PCI0.GPIO`. A different resource, malformed ACPI reply, or failed query
rejects startup. The connection ID comes from PnP; no physical GPIO registers or
guessed resource-hub IDs are used.

The driver opens that connection for output and requests **zero only** on D0
entry and exit. It retains the connection until resource release. It exposes no
user command interface and contains no operation to raise EN. GPIO requests have
a two-second timeout. Start failure is reported as failure, not successful
shutdown. Surprise removal releases handles without issuing another GPIO write.

`SofAmpStage`, `SofAmpStatus`, and `SofAmpOffWrites` are recorded in the device's
hardware registry key when available, and in kernel debug output. Stages are
1 = resource discovery begun, 2 = validation finished, 3 = entry/open/off result,
4 = exit/off result. Counts reset on PrepareHardware. A successful GPIO completion
records that the controller accepted a low-output request; its echoed buffer is
**not independent electrical readback**. GPIO state after target close, controller
power transition, or firmware restart is not established by this package.

The accompanying ACX source now holds playback: D0 entry reports startup stage 7,
`ThermalHold = 1`, and `HardwareReady = 0`, without booting firmware or configuring
SSP. It returns PnP success to permit amplifier-child enumeration. There is no
registry override or IOCTL to lift the hold. The old startup helper remains in
source for review but has no callers. Stream preparation fails while held.

Build with the WDK using `msbuild driver/amp/sofamp.vcxproj /m
/p:Configuration=Debug /p:Platform=x64`; CI independently runs InfVerif. Run
`python3 tools/test-amp-hold.py` for sanitizer-backed resource rejection and the
production held D0 callback. These checks do not exercise GpioClx on the target.

The first controlled maintenance installation succeeded on 2026-10-14. The held
ACX package is `oem26.inf`, version `1.26.8.27`; the shutdown-only amplifier
package is `oem27.inf`, version `1.27.25.292`. The parent is enabled with playback
held, allowing the amplifier child to own its GPIO. Readback showed stage 7,
ThermalHold 1, HardwareReady 0, no stream runs, HDA DMA stopped, and DSP ADSPCS
`0x00000F0F`. The amplifier reported stage 3, status 0, and one accepted off write.
Both devices reported OK, and both Windows audio services remained stopped.
There was no reboot or playback test. This verifies GPIO resource ownership and
an accepted shutdown request, not physical output voltage, temperature, speaker
integrity, or boot/suspend behavior. Do not replace the held ACX package with an
older package that automatically starts firmware/playback.

Before playback can return, implement and verify the full amplifier lifecycle
and investigate the remaining distorted signal/thermal cause. Lowering EN is not
by itself evidence that the prior heating cause is corrected.

References: [Windows GPIO resource connection](https://learn.microsoft.com/en-us/windows-hardware/drivers/gpio/connecting-a-kmdf-driver-to-gpio-i-o-pins),
[GPIO write contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/gpio/ni-gpio-ioctl_gpio_write_pins),
and [target investigation](../../docs/n100-amplifier-thermal-review.md).
