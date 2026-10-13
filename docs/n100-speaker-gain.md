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
