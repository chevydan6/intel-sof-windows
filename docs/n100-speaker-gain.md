# PUJJOTEEN speaker output level

After the echo correction (`b484a8d`), Dan reported that the echo was gone but playback still sounded distorted, like overdriven speakers. A live YouTube stream had zero late packets and no recorded driver or hardware stream error. This narrows the investigation but does not establish analog clipping by itself.

`tools/check-output-level.ps1` read the default Windows endpoint at 0 dB / 100%, unmuted, with no hardware volume support. The supported software range was -96 to 0 dB in 1.5 dB steps. The same tool reduced output by 12 dB and verified -12 dB / scalar `0.4531023` (about 45% on the Windows slider). This is a diagnostic attenuation; a listening response is still required.

## Board-specific evidence

The public ChromeOS configuration for **pujjoteen**, `audio/cras-config.max98357/sof-rt5682.card_settings`, defines an explicit speaker curve beginning with `db_at_100 = -900`. CRAS documents these values as hundredths of a decibel, so that board configuration uses **-9 dB at maximum speaker volume**. The Windows prototype previously copied samples at unity gain (0 dB).

Sources:

- [Pinned PUJJOTEEN speaker curve](https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/93dea50b71007195324090989cb5b0d8759bc177/overlay-nissa/chromeos-base/chromeos-bsp-nissa/files/pujjoteen/audio/cras-config.max98357/sof-rt5682.card_settings)
- [CRAS volume-curve units](https://chromium.googlesource.com/chromiumos/third_party/adhd/+/main/cras/README.md)
- [Windows software endpoint volume behavior](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudioendpointvolume)

This is strong evidence of missing board attenuation in the prototype, not proof that all remaining distortion has the same cause. The configuration belongs to this board's MAX amplifier path; it is not a generic attenuation value for every N100 laptop.

## Correction

`driver/acx/sofplay-samples.h` supplies the speaker scaling used when copying Windows packets into the hardware ring. Signed S32 samples are multiplied by `11626 / 32768`, approximately -9.00039 dB, using a widened signed product. The maximum positive and negative inputs remain representable; silence remains exactly zero. Client buffers are not modified, and both portions of a wrapped packet use the same conversion. The direct diagnostic probe tone is unchanged.

This fixed board attenuation follows the existing Windows user/session volume controls. It reproduces the board's maximum level; it does not reproduce ChromeOS's entire 101-step volume curve or implement a dynamic limiter. Status version 5 reports `SpeakerAttenuationDb = 9` so the running binary can be identified after installation.

`python3 tools/test-acx-ring.py` passed with address/undefined-behavior sanitizers. It exercises the actual production conversion and ring code, including signed extrema, polarity, zero, unchanged source samples, wrapped copies, stale-data clearing, and invalid packet sizes. WDK compilation and listening validation are required before calling the correction verified on hardware.

## Build and activation

The first ACX build (`38464275766`) exposed a user-mode CRT header conflict from including `stdint.h` in kernel code. The helper now uses WDK `LONG` / `LONGLONG` types; the Linux test supplies fixed-width equivalents. Both workflows now select x64 MSBuild, and the ACX workflow independently verifies its generated INF with x64 InfVerif, matching the standalone probe's check.

Commit `56b94d8` passed ACX WDK build and INF verification in run [38464522460](https://github.com/chevydan6/windows-sof/actions/runs/38464522460). Standalone probe run `38464522549` also passed. The sanitized ring/sample test passed again after the kernel-type correction.

The package installed as `oem25.inf`, version `20.39.28.945`, signer `C2DF4A97C6DB64210192DF5009B54CB75B85EF2B`. Audio services were stopped before the update and restarted in `finally`. Installation returned zero, with no reboot required. The controller reported `OK`, automatic firmware initialization reached stage 6 with success, and the live status returned `SpeakerAttenuationDb = 9`, proving the new binary was active.

The endpoint's software volume remained -12 dB / `0.4531023` after installation. With the driver's fixed attenuation, the combined attenuation is approximately 21 dB before accounting for app/session volume. Playback quality still needs the user's listening report. Package and recovery scripts are in `C:\Users\dan\sofplay-acx18-test`; rollback covers test certificates through ACX18. Older development packages remain available for recovery.

Two successive WAVs in desktop session 1 (`05:41:19`–`05:41:33`, Windows local time) passed with the new driver. After the first, counters showed one run, 302 submitted packets, and 301 completion notifications. After the second, they showed two runs, 605 submitted packets, 603 completion notifications, and one PCM reset. Both reported zero late packets, zero starting position, no callback failures, hardware status `0x20`, and the 9 dB board attenuation. The playback script and scheduled task returned zero. This verifies repeated transport with the conversion enabled; it does not replace an audible distortion check.


## Thermal stop — 2026-10-14

Dan reports persistent fuzzy/buzzy playback at modest perceived loudness, suspects a damaged speaker, and reports that the speakers are really hot. Left/right isolation was requested, but no further playback test was performed after the heat report.

Stopped Audiosrv and AudioEndpointBuilder and verified both Stopped. Disabled the N100 SOF development PCI device and verified DEVPKEY_Device_ProblemCode = 22 (disabled). No reboot was issued. Do not automatically restart audio, re-enable this device, or install a package that re-enables it as part of routine continuation. Investigate the thermal fault before further powered speaker testing; damage is not established and driver output remains a possible cause.

A preceding three-second WASAPI software loopback recording captured stereo float32 at 48 kHz, with peaks approximately 0.524/0.529. That upstream capture does not establish safe downstream electrical output or physical speaker condition. The recording is local under ignored build/distortion and must not be committed. Diagnostic commit 07a1eda has not been installed on the target during this thermal stop.
