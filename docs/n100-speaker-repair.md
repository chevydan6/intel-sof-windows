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
19.2 MHz MCLK. Only one channel receives a 1 kHz sine at -18.06 dBFS peak;
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

Source `5d202a2` passed [build 38535944208](https://github.com/chevydan6/windows-sof/actions/runs/38535944208)
and installed as `oem47.inf`, version `8.33.50.788`. Its initial left pulse
returned `0xF03`, full capture, zero status/cleanup errors and successful
amplifier-off/DSP-baseline checks. The recording now shows a left-test 1 kHz
component at 1.05–1.06 s, approximately -79.8/-88.1 dBFS at the two microphones,
10.8/5.7 dB above each recording's largest pre-test component. Thus the earlier
faint left recordings do not establish a dead channel. The physical heat/hearing
observation for this pulse was subsequently cool, with nothing audible.
Headphone audio services were restored afterward. The fresh amplifier query
reported ready=1, active=0, output-low accepted and no faults.

The same-level right test subsequently returned `0xF04`, full capture, zero
status/cleanup errors and verified shutdown. The user again reported cool
speakers and nothing audible. The right recording's 1 kHz component was
-72.7/-67.8 dBFS at 1.05–1.055 s, approximately 11.0/12.2 dB higher than
the previous right test, consistent with the 12.04 dB source increase.
Harmonic-bin levels in this short pulse do not establish electrical distortion
or the condition of the speakers. The channel difference could include
placement, microphone response or hardware; no failure diagnosis is established.

## Cached serial-interface observation

The next diagnostic keeps the fixed -30.10 dBFS level unchanged and adds
`IOCTL_SOFPROBE_SPEAKER_SNAPSHOT` (`0x226078`). During the existing pre-enable
check it caches SSP1 control/status registers and the corresponding host
position/capture-byte count. The read-only query returns that record without
starting hardware. The capture script saves it before the cleanup restart as
`.ssp.json`; it never reads the SSP audio FIFO or writes these registers.
The planned hardware check is a dry run with the amplifier held off.

SSP1's host BAR offset is already used by the existing firmware startup code.
Register offsets and format/status fields follow the upstream
[SOF SSP definitions](https://github.com/thesofproject/sof/blob/v2.2/src/include/sof/drivers/ssp.h).
Host tests exercise the production snapshot routine against distinguishable
register values and verify that the sampled words are not modified. This is
configuration/error readback, not an electrical measurement of the pins.

Source `a2f062c` passed build 38537054221 and installed as `oem48.inf`, version
`8.42.58.708`. The initial client buffer was incorrectly 32 bytes instead of
48; the driver rejected the undersized query, and the test still completed
amplifier-off/DSP shutdown. The reader was corrected and the dry test repeated.
It returned full capture, flags `0xC01` and zero status/cleanup errors, but all
eight SSP register values were `0xFFFFFFFF`. These values are unavailable
readback, not evidence that any decoded error bit is set. The status query
must be saved before the parent restart, which recreates its context.

## Firmware playback counters

The next snapshot format (version 2, 216 bytes) adds two bounded reads of the
firmware's stream-position record around one and four seconds of capture.
The hash-pinned image's XMan declares a 4096-byte stream window at BAR offset
`0xC1000`; the exact PCM_PARAMS reply supplies the relative record offset.
Bounds, alignment, record size, component ID and command are checked. Two
matching copies are required, with at most three attempts, because the DSP
can update the record concurrently.

Only the fixed diagnostic PCM changes: `no_stream_position=1` is retained and
`cont_update_posn=1` enables continuous shared-memory updates without unsolicited
IPC notifications. This follows the upstream
[host update implementation](https://github.com/thesofproject/sof/blob/v2.2/src/audio/host.c)
and [stream ABI](https://github.com/thesofproject/sof/blob/v2.2/src/include/ipc/stream.h).
The source code is an implementation reference; actual counter validity and
progress must be established on the installed firmware. Ordinary headphone
playback and the fixed amplifier lease are unchanged. Initial validation is
an amplifier-off run. The user has not switched out of Windows to compare
speaker behavior after the original heating.

Source `3ed76d0` passed build 38538135161 and installed as `oem49.inf`, version
`8.51.55.620`. Its dry-left run completed with full capture, flags `0xC01`,
zero status/cleanup errors and no amplifier enable. Two stable firmware records
were read at relative offset 76. Both report flags `0x100` (host position valid;
DAI position invalid). Host positions were 28800 and 5760 bytes. Their change
modulo the 49152-byte host ring is 26112, exactly matching the microphone capture
byte-count change (1542112-385504) modulo the same ring size. This establishes
firmware-side host progress, not valid SSP position or electrical output.
Zero DAI position must not be interpreted as a stalled DAI because its validity
flag is absent. Direct SSP reads remained unavailable.

For one further acoustic comparison the fixed tone is raised to -18.06 dBFS,
with unchanged channel isolation, 40 ms stop and independent amplifier lease.
This is still below the board's -9 dB maximum-volume attenuation, but that
comparison alone does not establish safety for damaged hardware or continuous
playback. If the stronger right pulse remains inaudible, do not continue
raising its level; obtain a different acoustic/electrical or known-driver
comparison instead. Continuous speaker playback remains disabled.
