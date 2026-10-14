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

The headphone driver's explicit diagnostic IOCTLs select six modes:
left/right with the amp kept off, with a 40 ms pulse, or with an explicit
120 ms quiet pulse.
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
The explicit 120 ms modes renew the 100 ms lease at 40 ms intervals until
the fixed stop target; they cannot extend the amplifier's 250 ms absolute
per-start ceiling. The amp's separate watchdog backs up the caller's shutdown. Transfer/scheduling delays mean neither time is an electrical guarantee.
Cleanup requests amp off before IPC/DMA waits, closes the ownership handle,
stops capture/output and retains buffers on ambiguous DMA cleanup.

The response uses the existing bounded microphone result storage with version 2.
The last header word contains mode 1..6 plus flags: `0x800` host ring contents
verified immediately before enable, `0x400` output DMA advanced,
`0x100` enable accepted, `0x200` explicit stop accepted, and `0x1000` lease
renewed for an extended pulse. The general ACX status
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

Source `7e3c4ec` passed build 38538475724 and installed as `oem50.inf`, version
`8.54.47.948`. One right pulse completed with flags `0xF04`, full capture,
zero status/cleanup errors and verified amplifier-off/DSP cleanup. Both firmware
position records were valid host records (DAI-valid remained clear). The
right tone now measured -60.4/-54.7 dBFS around 1.05 s, 32/39 dB above the
largest pre-test 1 kHz component, and about 12.3/13.1 dB above the previous
right test. The recording did not reach digital full scale.

A 20 ms, 1 kHz fit on microphone 2 shows a broadly sinusoidal waveform without
obvious flat-topped clipping; fitted second-through-fifth harmonics total about
1.63% and the remaining broadband residual is about 18.2% of fundamental RMS.
These are short acoustic-chain observations, including microphone, placement,
room and noise. They are not calibrated amplifier THD, a musical playback test,
or thermal clearance. Waveform/recording artifacts remain private in build.
The user subsequently reported that the speakers remained cool and no pulse
was audible. Do not increase the source amplitude further.
Headphone services have been restored, with the amplifier reporting off and
no faults. If it is again inaudible, a separately placed Ubuntu microphone
(previously authorized by the user) could distinguish an acoustic tone from
coupling into the Lenovo's built-in microphone; do not raise the source level.

After the user placed the laptops together, an independent Ubuntu internal-mic
recording captured one further right pulse at the same -18.06 dBFS level.
Ubuntu's initial microphone volume was 1.00 and baseline captures clipped;
volume 0.025 produced a baseline without clipping and was used for the test.
The original microphone volume was restored afterward. No speaker gain or
driver changes were made.

The 13.16-second external recording contains a distinct 1 kHz burst at 7.415 s,
measuring -81.35/-89.31 dBFS, 22.16/14.63 dB above the largest corresponding
component in its first two seconds. Neither external channel clipped. This
supports actual acoustic output rather than pickup confined to the Lenovo's
internal microphone circuitry. It does not establish useful playback loudness,
speaker condition, calibrated sound pressure, or safety for continuous output.
The simultaneous Lenovo capture completed with flags `0xF04`, full data and
zero status/cleanup errors. Its tone peaked around 1.07 s.

The saved pre-restart amplifier status shows exactly one enable, inactive state,
an accepted off request, zero faults and zero watchdog trips. Cleanup verified
DSP shutdown; headphone audio services were then restored. User temperature
feedback for this repeat pulse is pending. All recordings and analysis remain
private in the ignored build directory; no further level increase is planned.


## Independent control recording and small level steps

The user confirmed the right pulse left the speakers cool. An amplifier-off
right-channel control then completed with flags `0xC02`, zero enable writes,
full capture, and zero status/cleanup errors. The external microphone's largest
1 kHz components, excluding recording boundaries, were -108.26/-109.22 dBFS;
the preceding pulse was 26.91/19.92 dB higher. This supports an amplifier-dependent
sound, without proving calibrated acoustic gain or the condition of the coils.
Headphone services and the Ubuntu input volume were restored after the control.

After this independent check, the user explicitly requested continued tests
with gradual sound-level increases and distortion monitoring. The next fixed
build uses a 45/256 full-scale peak (-15.10 dBFS), a 2.96 dB step from 32/256.
It preserves the nominal 40 ms pulse, amplifier lease, channel isolation,
shutdown verification, and headphone-only normal playback. Further powered
steps depend on the recorded result and physical temperature feedback; this
is not approval for unattended continuous speaker playback. No thermal safety
or clean musical playback is established by a short sine recording.

The same-level left pulse (`7e3c4ec`, -18.06 dBFS) completed with flags `0xF03`,
full capture, exactly one amplifier enable and verified shutdown without faults.
Its Lenovo 1 kHz components near the expected pulse time were -71.09/-75.54
dBFS, comparable to that recording's background. The Ubuntu recording did not
provide a reliable left-tone measurement. The signal-to-noise ratio is too low
for a useful distortion estimate; an unconstrained search selected unrelated
sounds outside the pulse window, so those fits were rejected. This result does
not establish left-speaker failure. Temperature feedback after this pulse is
still pending.

An Ubuntu recording check identified a capture-start transient during the first
second. After it settled, volume 0.10 with 32-bit recording gave nonzero useful
samples without clipping. Subsequent recording starts allow three seconds to
settle before requesting the diagnostic, and analysis excludes startup. The
previous low-volume 16-bit left capture had extensive quantization to zero.
External dBFS values at these different gain settings are not directly comparable.

Source `9943b70` passed build 38543198966 and installed as `oem51.inf`, version
`9.33.36.555`. Its amplifier-off right test completed with flags `0xC02`, full
capture, zero status/cleanup errors, zero enable writes, and verified shutdown.
The independent Ubuntu capture had no clipping after its first second, with
settled peaks below 0.0022 full scale. Normal headphone services and the original
Ubuntu microphone volume were restored. The new -15.10 dBFS powered pulse has
not been issued; it awaits temperature feedback after the preceding left pulse.

After the user confirmed cool speakers, one -15.10 dBFS right pulse completed
with flags `0xF04`, full capture and zero status/cleanup errors. Saved amplifier
status shows exactly one enable, inactive state, an accepted off request and
no fault/watchdog trip. Headphone services and Ubuntu microphone volume were
restored. The Ubuntu microphone's known first-second startup transient clipped,
but no samples clipped afterward or in either microphone's pulse window.

The Lenovo pulse measured -55.24/-48.81 dBFS near 1.055-1.060 s. The independent
Ubuntu pulse measured -45.79/-46.08 dBFS near 8.175 s, approximately 39 dB above
its pre-test 1 kHz background. Twenty-millisecond fits give second-through-fifth
harmonics of 2.90/4.61 percent on Ubuntu and 13.06/6.10 percent on Lenovo; the
second harmonic dominates. Ubuntu residual broadband noise is about 6 percent
of the fitted fundamental RMS. Visual inspection shows no obvious flat-topped
clipping. These are recordings of the complete acoustic/microphone path, not
calibrated amplifier THD or proof of undamaged speakers.

Hold escalation at this point. Prepare a comparison at the previous -18.06 dBFS
level, preserving the current microphone gain, 32-bit recording, settling period
and laptop placement. Earlier lower-level recordings used different external
recording settings and cannot establish a controlled distortion-versus-level
comparison. Temperature/hearing feedback after the -15.10 dBFS pulse is pending;
no further powered pulse has been issued.

The verified lower-level package was restored as `oem50.inf`, version
`8.54.47.948`, with held DSP/no-DMA checks passing. A fresh amplifier query is
ready, inactive, off-request accepted and fault-free. Headphone services were
started again. The matched lower-level recording is prepared but has not run.

## Matched lower-level comparison

The user confirmed the -15.10 dBFS pulse was audible and the speakers remained
cool, but its duration was too short to judge sound quality by ear. One matched
-18.06 dBFS right pulse then completed with flags `0xF04`, full capture, zero
status/cleanup errors, exactly one enable and verified amplifier-off cleanup.
Both microphone paths used the preceding recording settings; Ubuntu was at
volume 0.10, 32-bit recording, with a three-second settling period. No clipping
occurred after the Ubuntu startup transient or in the pulse windows. Headphone
services and Ubuntu microphone volume were restored afterward.

Ubuntu's fitted lower-level fundamentals were -48.99/-49.57 dBFS, versus
-45.74/-46.04 dBFS in the louder capture. The 3.24/3.54 dB measured increases
are close to the 2.96 dB source step. Second-through-fifth harmonic ratios were
2.22/3.37 percent at the lower level and 2.90/4.61 percent at the higher level.
Lenovo's corresponding ratios fell from 13.06/6.10 to 9.42/4.20 percent. These
short acoustic-chain fits support a level-dependent nonlinear contribution,
without identifying whether it arises in the amplifier, speaker, microphone,
or a combination. They do not establish electrical amplifier THD, a damaged
speaker, or safe sustained playback. No obvious hard clipping is established.
Hold further amplitude escalation while investigating this distortion; thermal
feedback after the latest lower-level pulse remains pending.

## Playback-path review after the matched comparison

The user confirmed the matched lower-level pulse also left the speakers cool.
A source review, with no additional speaker enable, compared the held ordinary
speaker graph against the diagnostic graph. Both use signed S32, SSP1, I2S,
48 kHz, two 32-bit slots and a 3.072 MHz bit clock. The headphone-specific
S24 conversion is excluded from the speaker build. Previous sample arithmetic
checks did not reproduce clipping; the original application's format and DSP
output still cannot be reconstructed from these facts alone.

The ordinary speaker graph still requested a 24 MHz MCLK, unlike the packaged
board topology and the successful diagnostic's 19.2 MHz request. It now uses
19.2 MHz as well. The cross-path regression check failed on the old value and
passes with the correction, also checking S32 format, playback direction and
SSP identity. The ACX workflow runs this check and tracks its speaker-source
inputs. The amplifier itself needs no MCLK, per the
[manufacturer's device description](https://www.analog.com/en/products/max98360a.html),
so this consistency fix does not establish the cause of the heat or harmonics.

The original normal speaker stream has no amplifier ownership/start/stop client.
The current production-facing speaker path remains held, while explicit
short diagnostics use the guarded amplifier client. Do not remove the hold to
restore music playback: any future normal speaker route requires verified
amplifier lifecycle integration as well as thermal and audible validation.
No new speaker pulse or driver installation occurred during this review.


## Fixed longer quiet diagnostic

The clock consistency correction `d103f2f` passed Windows builds and INF checks
for ACX (38545547340), headphones (38545547515), the guarded amplifier
(38545547370), and the standalone probe (38545547353). It did not lift the
ordinary speaker playback hold or change the installed package during review.

To make a bounded test easier to hear after repeated cool-speaker observations,
new explicit modes 5/6 request a single nominal 120 ms left/right pulse. Both
short and extended diagnostics return to the lower -18.06 dBFS level. The
extended IOCTLs are `0x22A07C` and `0x22A080`; the script requires both `-Pulse`
and `-Extended`, records the requested duration and verifies the renewal flag.
No caller-supplied level, waveform, duration, or continuous output is accepted.

The parent checks the fixed sample ring before every renewal, renews no more
frequently than 40 ms, and checks the 120 ms stop condition before considering
a renewal. Failed renewal, corrupted samples, capture cancellation, DMA error
or an excessive polling gap take the existing amplifier-first cleanup path.
The independent amplifier retains its 100 ms lease and 250 ms maximum per-start
interval. These remain software deadlines, not measured electrical cutoff.

Host tests exercise both durations and channels, renewal failure, corrupted
samples during an extended pulse, late callbacks stopping without renewal,
and shutdown ordering. Existing amplifier lease/absolute-limit/ownership tests
and microphone cleanup tests pass. Hardware validation starts with an
amplifier-off run; an extended powered result is not yet available.

Source `0481451` passed headphone build/INF verification 38545927470, ACX
38545927530 and standalone probe 38545927599. The headphone package installed
as `oem52.inf`, version `9.56.0.486`; its signature/hash, dormant DSP, stopped
DMA and amplifier-off checks passed. The updated PowerShell capture script
parsed successfully on the target without starting hardware.

The new-build dry-right run returned flags `0xC02`, full capture, zero errors
and zero amplifier enables. One subsequent extended right pulse at -18.06
dBFS returned flags `0x1F06`, confirming enable, lease renewal and explicit
stop. Capture and cleanup succeeded, and the saved amplifier status shows one
enable, inactive state, off-request accepted, zero faults and zero watchdog
trips. Headphone services and the original Ubuntu microphone volume were
restored. No normal speaker endpoint was enabled.

The external recording contains approximately 107 ms of tone above its
minus-6-dB envelope threshold; internal microphones give 104-107 ms. This is
an acoustic-envelope measurement, not the electrical GPIO-on interval. The
nominal 120 ms includes setup and the acoustic onset/decay do not define its
software deadline. No samples clipped after the Ubuntu capture-start transient.
A roughly 57 ms central-window fit on Ubuntu gives 1000.18/1000.11 Hz,
second-through-fifth harmonic ratios 2.51/2.74 percent and broadband residual
13.6/16.1 percent relative to fundamental RMS. Results vary with microphone and
window, so they remain acoustic-chain estimates, not amplifier THD or proof of
speaker integrity. User hearing/temperature feedback after this longer test is
pending; do not infer sustained-playback safety from its completed shutdown.

The user missed the first extended pulse, then confirmed the speakers were
still cool. One announced repeat of the same 120 ms right pulse completed
with flags `0x1F06`, full capture, zero errors, one enable and verified shutdown.
The saved amplifier status has no fault or watchdog trip. Both microphone
recordings show about 106-107 ms above the minus-6-dB envelope threshold and
no clipping after Ubuntu's startup transient. Central-window Ubuntu harmonic
ratios are 2.66/3.75 percent, again acoustic-chain estimates rather than
calibrated amplifier distortion. Headphone services and input volume were
restored. The user reports that this beep sounds fine. Temperature feedback
after the repeat remains pending; the next prepared check is the left channel
at the same level and duration, with no further amplitude increase.
