# N100 built-in microphone capture

The headphone prototype now has a fixed, five-second physical microphone
capture diagnostic. It is **not yet an ACX microphone endpoint** that Windows
applications can select. `tools/capture-output.ps1` remains software output
loopback and is unrelated to this physical capture path.

Lenovo specifies a dual-microphone array for the 14e Chromebook Gen 3.
Coreboot's Nissa baseboard assigns GPP_R4/R5 to the user-facing camera DMIC
clock/data on PDM controller 0; R6/R7 are the optional world-facing camera
pair. The PUJJO variant does not override those pads. This probe enables
PDM0 A+B only. Counting bezel holes is not a reliable channel-count check.

References:

- [Lenovo specifications](https://psref.lenovo.com/syspool/Sys/PDF/Lenovo/Lenovo_14e_Chromebook_Gen_3/Lenovo_14e_Chromebook_Gen_3_Spec.pdf)
- [Coreboot Nissa GPIO configuration](https://github.com/coreboot/coreboot/blob/main/src/mainboard/google/brya/variants/baseboard/nissa/gpio.c)
- [PUJJO GPIO overrides](https://github.com/coreboot/coreboot/blob/main/src/mainboard/google/brya/variants/pujjo/gpio.c)
- [SOF v2.2 DMIC IPC ABI](https://github.com/thesofproject/sof/blob/v2.2/src/include/ipc/dai-intel.h)
- [SOF v2.2 microphone driver](https://github.com/thesofproject/sof/blob/v2.2/src/drivers/intel/dmic/dmic.c)

`GetSystemFirmwareTable` returned Win32 1168 (not found) for NHLT on this
machine. The configuration therefore uses the existing SOF RT5682 topology's
48 kHz, 32-bit, 2.4–4.8 MHz PDM clock limits and 200 ms unmute ramp, with
only the board's PDM0 microphone pair enabled.

The fixed graph is DMIC0 -> buffer -> capture host, with its own component
IDs and DAI scheduling component. Input HDA stream 0 uses tag 1; SOF host DMA
subtracts one from the tag, independently for each direction. Output stream
0 can therefore keep its own tag 1. The host ring is 49,152 bytes. Poll gaps
of 50 ms or longer fail the capture rather than silently accepting an
ambiguous ring wrap. The PCM transfer is 48 kHz stereo S32_LE.

The maintenance IOCTL accepts no user parameters and returns a diagnostic
header plus at most 1,920,000 sample bytes. It is only present in the isolated
headphone build. Firmware IPC is serialized with the audio power lock. The
microphone test cannot write codec gains, select arbitrary PDM controllers,
or enable the speaker amplifier. DMA is stopped before transfer registers
are cleared. Buffers are retained on cleanup failure until verified DMA and
DSP shutdown. A device restart resets the one-shot graph; no OS reboot is
performed by the test scripts.

## Hardware result, 2026-10-14

Build [38524223364](https://github.com/chevydan6/windows-sof/actions/runs/38524223364)
from `5c52b0b` passed WDK compilation, INF verification, the existing playback
checks and the new microphone ABI/DMA cleanup tests. Installed as `oem38.inf`,
version `6.47.4.412`.

An ambient recording returned 240,000 frames per channel in 5,001 ms:

| Check | Result |
| --- | --- |
| Capture and cleanup status | Both zero (success) |
| Input DMA status | `0x20`, no FIFO/descriptor error bits |
| Longest polling interval | 16,228 microseconds |
| Channel 1 RMS / peak | -65.12 / -43.15 dBFS |
| Channel 2 RMS / peak | -63.52 / -41.36 dBFS |
| Samples at digital full scale | None |

Both channels contained distinct nonzero samples. This establishes physical
capture data transfer, unlike the earlier ICCMAX DMA probe. It does not yet
prove calibrated frequency response, microphone sensitivity or channel
placement. The DSP returned to `0x00000f0f`, codec outputs remained muted and
power was zero after cleanup. The speaker shutdown driver remained installed.

A simultaneous headphone audition also returned a full five-second recording
with capture and cleanup status zero. The first wrapper incorrectly treated
an unavailable child-process exit code as failure after the WAV was saved;
opening the process handle before waiting fixes that bookkeeping issue.
The acoustic tone was weak relative to other recorded sound, so the recording
must not be used to claim a clean output waveform or a distortion percentage.
A second simultaneous test passed the corrected wrapper end to end: 804
playback packets submitted, 803 completed, zero late packets, and 240,000
captured frames per channel in 5,000 ms. Maximum microphone poll interval
was 16,779 microseconds. The 1 kHz band rose by about 21 dB in channel 1
and 14–16 dB in channel 2 during the bounded output window, then fell back.
Peak captured levels were -60.88 and -61.75 dBFS, with no full-scale samples.
This confirms an acoustic headphone tone reached both physical microphones;
it does not clear the earlier speaker distortion/heating fault. Final checks
verified stopped audio services, dormant DSP, stopped playback DMA, muted
and powered-down codec, and the speaker shutdown driver's successful status.

All recordings stay in ignored local `build/` storage; they are not published.

## Use

With the pinned development package installed and the speaker shutdown hold
verified, `tools/test-microphone.ps1` records ambient sound and resets the
parent device afterward. Audio services must already be stopped.

For the bounded headphone test, put an earcup beside the Windows laptop's
microphone holes and run `tools/test-headphone-muted.ps1 -Audition
-MicrophoneOutput C:\Users\dan\sof-headphone\microphone-headphones.wav`.
That wrapper owns playback, microphone process completion and shutdown.
`-DuringAudition` is an internal child mode, not an independent recording
command: its parent must perform the final cleanup.
