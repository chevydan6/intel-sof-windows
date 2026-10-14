# N100 RT5682 headphone bring-up

Target: Lenovo 14e Chromebook Gen 3 / Google PUJJOTEEN, Intel N100, Windows.
The user reports that the headphone jack works under Fedora and ChromeOS.
Testing here concerns the new Windows driver, not those existing implementations.

## Verified codec work, 2026-10-14

The separate KMDF codec driver matches `ACPI\10EC5682` and verifies the exact
75-byte ACPI resource descriptor before using I2C3 address `0x1a`. Selecting
I2C mode at register `0xffff` is necessary before the chip returns its ID.
Readback identified device `0x6530`, vendor `0x10ec`.

The bounded calibration sequence is adapted from Realtek's GPL-2.0 Linux v6.8
[RT5682 codec source](https://github.com/torvalds/linux/blob/v6.8/sound/soc/codecs/rt5682.c)
and [I2C probe](https://github.com/torvalds/linux/blob/v6.8/sound/soc/codecs/rt5682-i2c.c).
After calibration, hardware reads report headphone mute `0x8080`, DAC/output
control `0x8000`, and digital power `0`. The initial fixed-operation interface
supports query, muted digital preparation, and shutdown. The later bounded
audition operation is described below; arbitrary-register access is not exposed. Failure paths attempt all shutdown writes.

Codec package `79e64e0` passed WDK/InfVerif in CI run `38519142648`, then installed
as `oem31.inf`, version `5.54.26.153`, with no reboot. PnP reported OK and the
muted calibration checks passed. Codec STOP readback also confirmed digital
power zero and both mute controls asserted.

## Isolated headphone transport

`driver/headphone/sofheadphone.vcxproj` builds shared ACX code with
`SOFPLAY_HEADPHONE`. The endpoint is named N100 SOF Headphones. Firmware startup
is deferred until stream preparation so the shutdown-only amplifier child can
enumerate first. The headphone pipeline contains SSP0 only; no SSP1 speaker
component is created. Windows 24-bit left-aligned PCM is converted to SOF
S24_4LE with an additional 1/16 software gain. Codec DAC gain is fixed at -39 dB
for development, and the physical outputs remain muted.

The packaged `sof-adl-max98360a-rt5682.tplg` reference has SHA-256
`52fab042fb589cecb009be56c3deda85d6ec2291d49e4b2bd95854b54b88be80`.
Its SSP0 configuration is I2S, 48 kHz stereo, 19.2 MHz MCLK, 2.4 MHz BCLK,
two 25-bit slots carrying 24 valid bits. This is a packaged reference, not a
capture of the exact working Fedora configuration on this machine.

Initial muted tests failed before streaming. Additional diagnostics established
that firmware boot succeeded but component creation (`0x30010000`, component 4)
returned `-EINVAL`. The initial array reversed the `dai_index` and `type` fields:
SSP1 had hidden this because both values were 1. The fix uses index 0 and Intel
SSP type 1. `test-headphone-pipeline.py` checks component/configuration identity,
format, direction, clocks and slot masks using the
[SOF IPC3 topology ABI](https://github.com/thesofproject/sof/blob/v2.2/src/include/ipc/topology.h).

`tools/test-headphone-muted.ps1` checks the codec mutes and amplifier shutdown,
sends a three-second silent Windows stream, checks packet progress, and stops
Windows audio services afterward. It then resets the codec and restarts only
the PCI device, checking DMA stopped and DSP state `0x00000f0f`. It does not
reboot Windows. The failed tests passed these cleanup checks.

The status field `ThermalHold=1` means the speaker hold is active. With
`HeadphoneOnly=1`, the separate headphone transport may initialize while its
analog output remains muted. Packet counters alone do not establish audible
quality, electrical signal integrity, or temperature.

## Remaining work

The corrected muted pipeline is now verified (see below). Develop normal playback lifecycle and jack
insertion/removal handling, then validate stereo output and quality beyond the
bounded acoustic test. A short acoustic output test is recorded below; normal playback and full
audio-quality validation are still unfinished. Speaker heating and possible damage remain unresolved.

Microphone recordings stay in ignored local `build/` storage. They are not
uploaded to GitHub or included in driver packages.

## Muted streaming verified

After the field-order fix, the legacy host PCM guard rejected S24_4LE because
it was still pinned to the speaker's S32 layout. Commit `db2cd7b` shares the
build-selected format between parameter creation and validation; DMA address,
size, state, channel and rate checks remain enforced. Both guard variants are
exercised with accepted and malformed parameters in `test-pcm-guard.py`.

CI run `38521405338` passed. The transport installed as `oem36.inf`, version
`6.18.22.0`, without reboot. A three-second muted Windows stream produced:

| Readback | Value |
| --- | --- |
| Startup stage/status | 6 / success |
| Hardware ready | 1 |
| Stream runs | 1 |
| Packets submitted/completed | 306 / 305 |
| Late packets | 0 |
| HDA status | `0x20` (FIFO ready) |
| Codec prepared | 1 |
| HP mute / DAC control | `0x8080` / `0x8000` |
| Digital power during preparation | `0x8d01` |

Cleanup verified codec digital power zero, both output mutes asserted, audio
services stopped, DMA stopped and DSP `0x00000f0f`. The shutdown-only amplifier
still reported successful EN-low writes. Old failed IPC diagnostic registry
values can persist after successful startup; only inspect them for a failing
startup status.

## Bounded headphone audition

Commit `d778049` adds an administrator-only fixed audition operation. It
requires prior muted preparation, validates the fixed -39 dB DAC gain,
configures the Linux analog defaults and jack detection, refuses a missing jack
or jack-type timeout, and enables the headphone analog path for 30 polling
intervals of 100 ms. Every exit attempts mute and complete power-down. This is
a nominal three-second window plus I2C transaction time, not a hard realtime
deadline if a bus transaction stalls. No duration, gain, register address, or
speaker route can be supplied by the caller. Normal applications cannot unmute
this development endpoint.

The offline tests inject a fault at each of 113 audition transfers, and check
missing-jack, jack-type timeout and invalid-gain rejection. The driver-level
error path retries full shutdown. CI run `38521896561` passed WDK/InfVerif;
codec package installed as `oem37.inf`, version `6.23.27.42`. Installation
readbacks remained muted. The user confirmed wired headphones were off their
ears and positioned by the Ubuntu microphone for the acoustic test.

## First acoustic headphone output

The bounded test completed successfully with the headphones positioned at the
Ubuntu laptop microphone. The codec reported audition stage 7/status 0. Windows
completed an eight-second 1 kHz WAV with 808 submitted / 807 completed packets,
zero late packets, and HDA status `0x20`. Physical output was enabled only during
the codec's bounded audition window. No speaker route was enabled.

The local 48 kHz microphone recording contains a stable 1 kHz signal around
7.5–11 seconds, matching the three-second audition plus command overhead. On the
closer microphone channel, median peak spectral amplitude in quarter-second
windows was about -80.8 dBFS during the central test interval versus -101.7 dBFS
before and -102.0 dBFS after. No capture samples reached full scale. Harmonic
bands did not rise clearly above their background, but the low acoustic level
and uncalibrated microphone do not support a precise THD or output-voltage claim.
These observations support actual headphone sound output rather than only DMA
progress; they do not establish full stereo performance or general playback
quality.

After the test, readback confirmed `HP_CTRL_1=0x8080`, `HP_CTRL_2=0x8000`, digital
power zero, both Windows audio services stopped, DSP `0x00000f0f`, DMA stopped,
and successful amplifier shutdown writes. There was no OS reboot. The final
installed pair is transport `oem36.inf` (`db2cd7b`) and codec `oem37.inf`
(`d778049`). Normal applications still cannot unmute the endpoint; this is a
bounded bring-up prototype, not a completed general-purpose headphone driver.
