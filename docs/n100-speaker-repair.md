# Guarded speaker investigation

This work follows the reported speaker heat and fuzzy/buzzy sound. The user
confirms the speaker area stays cool with the amplifier held off, and has
positioned the built-in microphones closer using the lid. A damaged or shorted
speaker, incorrect digital signal, or needless amplifier drive remain possible;
none has been established by a physical electrical measurement.

## Installed amplifier control

Source `61f3fbf` passed [WDK/INF and state-machine checks
38532820265](https://github.com/chevydan6/windows-sof/actions/runs/38532820265).
It is installed as `oem44.inf`, version `8.6.50.535`, with the headphone parent
held dormant during installation. The hardware query and application rejection
checks passed: output-low accepted, active=0, accepted high requests=0. No
speaker was powered for this maintenance validation.

The [amplifier control notes](../driver/amp/README.md) explain ownership, fault
latching and the 100 ms lease / 250 ms maximum diagnostic interval. These are
software deadlines, not physical voltage, temperature or independent cutoff.

## Fixed speaker diagnostic

The headphone driver's explicit diagnostic IOCTLs select only four modes:
left/right with the amp kept off, or left/right with one short quiet pulse.
Normal app playback remains headphone-only. Neither boot nor microphone-only
capture starts a speaker test. The diagnostic requires a fresh idle DSP session
with no claimed Windows stream, no previous playback run and no previous mic
or speaker test. A verified parent restart is required afterward.

It mutes the headphone codec, releases its idle PCM, and builds a separate SSP1
S32 graph using IDs 20/22/24/25, distinct from the DMIC graph. The fixed format
matches the topology: 48 kHz, I2S, 3.072 MHz BCLK, stereo 32-bit slots and
19.2 MHz MCLK. Only one channel receives a 1 kHz sine at -30.10 dBFS peak;
the other channel is exactly zero. The ring holds an exact integer number of
cycles. No arbitrary frequency, sample data, amplitude or enable duration is
accepted from applications.

The built-in microphones record five seconds. After one second of capture,
the test checks output DMA progress and errors. Dry modes keep EN low throughout.
Pulse modes request a kernel-owned amp lease, then explicitly request off after
40 ms measured from before the enable call (including its 5 ms setup delay).
There is no renewal. The amp's separate 100 ms watchdog backs up the caller's
shutdown. Transfer/scheduling delays mean neither time is an electrical guarantee.
Cleanup requests amp off before IPC/DMA waits, closes the ownership handle,
stops capture/output and retains buffers on ambiguous DMA cleanup.

The response uses the existing bounded microphone result storage with version 2.
The last header word contains mode 1..4 plus flags: `0x800` host ring contents
verified immediately before enable, `0x400` output DMA advanced,
`0x100` enable accepted, `0x200` explicit stop accepted. The general ACX status
clears HeadphoneOnly/ThermalHold after a speaker test is attempted, so diagnostics
do not falsely report an unchanged speaker hold while this explicit path runs.
A verified DSP reset clears the diagnostic state.

`tools/test-speaker-capture.ps1 -Channel Left` is dry by default; `-Pulse`
explicitly selects the powered test. Its cleanup verifies amp off/fault state,
expected enable count, DSP baseline and muted headphones. Recordings are private
ignored build artifacts. Host tests execute the real fixed sample generator,
pulse timing and shutdown ordering, and check the IPC graph against the ABI.
Hardware dry-run and supervised pulse results must be recorded before claiming
speaker playback repaired. No continuous speaker endpoint is enabled by this work.

## Initial hardware results

Diagnostic source `b093791` passed WDK/INF and host checks in
[build 38533829631](https://github.com/chevydan6/windows-sof/actions/runs/38533829631)
and was installed as `oem45.inf`, version `8.15.46.10`.

The left dry run returned version 2, stage 23, flags `0x401`, all 1,920,000
capture bytes, and zero status/cleanup errors. Amplifier accepted-high count
remained zero. The first left pulse returned flags `0x703`, full capture and
zero errors. Accepted-high count increased exactly once; afterward active=0,
output-low accepted, fault=0 and watchdog trips=0. Both tests verified the
DSP baseline and muted headphone codec after the parent-device restart.
No Windows reboot was performed.

The recording does not establish audible speaker output or distortion quality:
the maximum 1 kHz component in 10 ms windows around the expected pulse was
approximately -89/-89 dBFS at the two microphones, comparable to background
noise. Neither recording clipped. A low microphone signal is not evidence
that the speaker waveform is clean. Recordings and analysis remain private
under the ignored build directory.

Further powered tests await the user's physical temperature observation.
The driver has no temperature or speaker-current measurement, and accepted
GPIO requests alone cannot establish actual voltage or thermal safety.
Continuous speaker playback remains disabled; the ordinary headphone audio
services were restored after this diagnostic.
The fresh amplifier query after restoration reported ready=1, active=0,
output-low accepted and no faults. Its counters had reset to on=0/off=1;
these counters are scoped to a hardware preparation, not a persistent test
history. The saved pre-restart pulse result preserves the one-enable evidence.

The user subsequently confirmed the left speaker area stayed cool. The same
original -48.16 dBFS right-channel test returned flags `0x704`, full capture,
zero status/cleanup errors and no amplifier faults/watchdog trips. The user
also confirmed it stayed cool. Its recorded 1 kHz component around the expected
pulse was approximately -88/-83 dBFS, still insufficient to assess distortion.

The second diagnostic raised only the fixed sample amplitude by 6.02 dB to
-42.14 dBFS, retaining the 40 ms stop and unchanged amplifier watchdog. It
checks the full host ring against the channel-specific test signal after PCM
preparation and again before requesting amplifier enable. Corrupt or cleared
samples abort with the amplifier off; host tests inject both conditions.
This check does not establish the physical SSP waveform or speaker condition.

Source `b32155c` passed [build 38535084193](https://github.com/chevydan6/windows-sof/actions/runs/38535084193)
and installed as `oem46.inf`, version `8.26.30.273`. The dry-left, pulse-left
and pulse-right tests returned `0xC01`, `0xF03`, `0xF04` respectively, full
captures and zero status/cleanup errors. The host sample ring was intact at
the pre-enable check. Shutdown checks passed with no amplifier faults or
watchdog trips. The user reported both powered tests remained cool and heard
nothing. Audio services were restored afterward with amplifier-off confirmed.

Using a 30 ms Hann window and 5 ms steps, the maximum 1 kHz component in
0.95–1.15 s on the right test was -83.7/-79.9 dBFS at the two microphones,
peaking at 1.055 s. These are 4.7/6.1 dB above the corresponding original
right-test levels, consistent with the requested 6 dB increase, and 9/13.9 dB
above each recording's maximum pre-test component (0.15–0.85 s). The left
test remained close to background. This is a useful channel difference,
not proof of speaker damage or a clean acoustic waveform; placement,
electrical coupling and hardware faults remain possible.

The next fixed diagnostic uses -30.10 dBFS (12.04 dB above the second test),
with the same sample verification, channel isolation, 40 ms stop and watchdog.
It does not enable a general speaker playback path.
