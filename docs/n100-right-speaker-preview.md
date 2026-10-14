# Bounded application-audio preview

The Lenovo's left-side imbalance reproduces in Fedora and Windows. The user
reports the right speaker clear and cool in both, so subsequent development
uses only the right physical speaker. This does not establish the cause of
the earlier heating or distinguish a damaged left speaker from its amplifier.

`sofplay.vcxproj /p:RightPreview=true` builds a separate experimental artifact.
The ordinary speaker package remains held, and the headphone build is unchanged.
The preview also enumerates with firmware, DMA and amplifier off. Only an
explicit arm request permits a stream to initialize the speaker pipeline.
Arm defaults to a dry run; a powered arm must be requested separately.

For this first application-stream test, the independent amplifier retains
its existing 100 ms lease and 2200 ms absolute ceiling. The parent stops at
2000 ms from stream start. It never automatically starts a second burst.
After pause, completion, fault or cancellation, another arm requires a clean
device restart. A passive non-periodic timer checks DMA advancement, left-slot
silence and sample limits every 20 ms. An 80 ms callback gap, stalled DMA,
invalid samples or failed amplifier call ends the preview. Deadline checks
precede enable/renew. The first enable occurs only after DMA has advanced.
Pause and power-down request amplifier off before stopping the DSP.

The converter mixes application stereo to mono at a nominal -12.04 dB ceiling,
removes DC with a first-order filter and bounds its output peak. It writes
zero to the physical left channel. The ring's existing consumed-data clearing
and end-of-stream handling remain in effect. This is a short diagnostic path,
not an unrestricted everyday speaker endpoint or a thermal protection system.

`tools/right-speaker-preview.ps1` queries by default. `-Arm` selects a dry
preview; `-Arm -Powered` permits a single powered preview. Phase and amplifier
enable count must be saved before restarting the audio device. Start with dry
application playback, inspect the ring and verify no amplifier enable. Only
then test powered playback, record it externally and verify explicit shutdown.
Preserve the known-good headphone package for rollback and restore it after
the experiment. No automatic Windows reboot is required by these tools.

Offline tests compile the production sample converter and timer callback with
injected clocks, DMA stalls, corrupted data and amplifier failures under
ASan/UBSan. They do not validate WDF scheduling, actual GPIO voltage, temperature
or sound quality. The separate WDK artifact and hardware checks are required
before treating this playback path as validated.

The timer design follows Microsoft's [passive timer requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-timers)
and [timer stop constraints](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdftimer/nf-wdftimer-wdftimerstop).
Timer draining occurs outside the callback's power lock to avoid deadlock.

## Initial hardware result and cleanup correction

Source `6aa5a40` passed WDK/INF verification in run `38566704222`. The preview
installed as oem55.inf, 12.49.9.42, with firmware/DMA/amp off and phase Unarmed.
The first dry application test crashed Windows before a ring snapshot was
saved. No powered arm was requested. Windows automatically restarted; this
was initially mistaken for a stale SSH connection until Event 1001 confirmed
bugcheck 0x1A/0x1230. The known-good headphone package oem54.inf 11.0.0.373 was
restored and audio services restarted. Amplifier status after recovery was off
with zero enables and no faults; that post-reboot count alone is not evidence
about pre-crash state. The dry request is the basis for saying no powered test
was requested.

Microsoft CDB traced the crash through `MiUnmapLockedPagesInUserSpace`,
`MmUnmapLockedPages`, `Acx::AfxRtStream::UnmapMdls`, and ACX's post-cleanup
callback. The shared driver cleanup callback was freeing packet MDLs before
this ACX post-cleanup step. Atomic ownership had prevented a second free, but
did not preserve the MDLs while ACX still had user mappings. Removed packet
freeing from the early cleanup callback: successfully published allocations
now belong to `EvtFreePackets`, while allocation failures still free their
unpublished buffers locally. A regression test fails on the old implementation
and passes with both cleanup/free callback orders after the correction.

The kernel dump and debugger output are private under ignored
`build/speaker-repair/right-preview-6aa5a40/`. The full dump's local and Windows
SHA-256 values match.

## Corrected build and dry transport results

Source `9d94d35` passed all three applicable GitHub workflows, including the
packet-lifetime regression, WDK builds and INF validation. Its separate preview
package installed as oem56.inf, 13.2.20.789. Two SSH-session dry playback runs
completed without another crash. The first submitted 151 packets and completed
150 with no late packets or driver-reported failure. Preview phase reached
Finished with success; the amplifier recorded zero enables, zero watchdog trips
and zero faults, and was off after cleanup.

These are transport/lifecycle results, not proof of audio playback: every saved
ring snapshot contained silence. A desktop-session retry could not start because
Windows had no signed-in user after the crash. The task never ran and was removed.
The later interactive tests below confirmed that nonzero samples do reach the
right slot when the player runs on the desktop.

Use `tools/invoke-right-speaker-preview.ps1 -WaveFile <wav> -OutputPrefix <new-prefix>`
from SSH after Windows sign-in. It checks for an interactive user, runs the worker
in that desktop, checks task completion and removes its temporary task. The worker
saves multiple ring snapshots and rejects silence, nonzero left samples, excessive
right peaks or a preview error. A zero task result requires those sample checks
as well as amplifier shutdown. `-Powered` remains a separate explicit option,
only appropriate after a successful dry run. Restart the parent device with audio
services stopped between attempts; the preview cannot rearm in place.

## First powered application preview

After desktop sign-in, the first interactive dry run captured nonzero right PCM
with all left samples zero, but finished with `0xC000009C` (device data error).
The amplifier stayed off. The current status does not distinguish a stalled
position from inactive DMA or an error detected while advancing the ring, so
the exact cause and timing remain unresolved. Do not describe this as a solved
fault merely because subsequent runs passed.

A second desktop dry run used a three-second source WAV and passed the two-second
preview cutoff. All seven nonempty ring snapshots had zero left samples and
right peaks below the ceiling; sampled phases progressed through Armed and
Active to Finished with success. Amplifier enables and watchdog trips stayed zero.

One powered run of that same melody then passed: Finished with success, exactly
one amplifier enable, zero watchdog trips/faults, and verified off after cleanup.
Across its saved rings, 6,000 right samples were nonzero, every left sample was
zero, and the maximum right magnitude was 436,125,245 (ceiling 536,870,911).
Ubuntu recorded the complete interval externally. Waveform correlation located
the source melody about 7.45 seconds into the capture (normalized correlation
0.398); no recorded samples clipped after the first startup second. This confirms
an acoustic signal corresponding to the melody, not an absence of mechanical
distortion or safe speaker temperature. Ambient sound and the recording path
limit acoustic conclusions. Private audio and machine evidence remain under
ignored `build/speaker-repair/right-preview-9d94d35/`.

A subsequent dry retry with the original 1.5-second WAV also passed, including
nonzero samples and clean shutdown. There have been no further crashes in these
corrected-build tests. The intermittent device-data error still needs diagnosis,
and normal sustained speaker playback is not yet enabled. Await the user's
listening/temperature check before extending the audible test; preserve the
existing two-second parent limit and independent 100 ms / 2200 ms amp limits.

The user subsequently confirmed that the powered melody sounded clear and both
speaker areas remained cool. This permits the next controlled test; it does not
resolve the intermittent monitoring error.

`right-speaker-preview.ps1 -Diagnostics` reads a separate 64-byte version-2 diagnostic
IOCTL without changing the original status ABI or any playback limits. It saves
the first stop reason, phase, elapsed time, callback gap, previous and observed
DMA positions, transport flags, advance status, raw hardware position/status and
boundary/completion read counts. Later cleanup retains that
snapshot; a new device initialization clears it. The test worker saves this
information even when the final preview status reports failure. Offline fault
injection verifies reason selection and preservation through cleanup.

After saving the results, restored the previously working headphone package
oem54.inf 11.0.0.373 and restarted both Windows audio services. The amplifier
was verified off with no faults. That older headphone package does not contain
the new packet-lifetime fix; the corrected headphone artifact passed CI but
still needs its own hardware validation before replacing the known baseline.

## Buffer-boundary correction

The instrumented `9f201fe` build reproduced the error on its second dry run:
AdvanceError in Active phase after 1138 ms, 36 checks, a 31 ms callback gap,
and both transport flags set. This rules out startup/no-progress and the late
callback check for that reproduction. The next diagnostic build, `14b655c`,
adds the raw HDA register values without changing the rejection policy.

Code review against Intel's [HDA 1.0a specification, sections 3.3.36–3.3.37](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf)
found two incorrect rejection conditions: LPIB may equal CBL at the wrap point,
and the BCIS status bit reports buffer completion rather than a transfer error.
The correction normalizes exactly CBL to ring offset zero and checks only the
FIFO/descriptor error bits. Positions greater than CBL still fail. It does not
change the amplifier lease, time limit, channel isolation or sample ceiling.

The regression fails on the previous code and passes in both speaker and
headphone builds with the correction. It checks CBL, repeated CBL/zero reads,
continuing from the boundary, BCIS, out-of-range positions and both actual error
bits. Offline success does not by itself prove the cause of the hardware error.

The raw-diagnostic build completed eight dry comparisons. It observed 61 reads
at exactly CBL across those runs, proving that the condition rejected by the
old code occurs on this hardware. None of those eight runs caught the preview
timer itself stopping at that boundary, so the earlier intermittent stop is not
conclusively attributed to this bug. A temporary SCP timeout was retried; the
Windows boot time stayed unchanged.

The corrected `4b0304c` build passed all applicable CI workflows and installed as
preview oem59.inf 13.28.12.991. Five consecutive interactive dry runs then passed,
including nonzero right PCM, silent left PCM, no late packets, no preview error,
normal stream-owned pause and amplifier off with zero enables/faults. These
runs exercised 44 boundary reads without rejecting them. The speaker preview
still has its original two-second limit.

## Separate eight-second preview build

`/p:RightPreviewLong=true` produces the separately named
`sofplay-right-preview-long-package` artifact. The normal preview remains
two seconds. The extended build queries a new amplifier capability before
arming, uses a distinct kernel-only START_PREVIEW command and stops at 8000 ms.
The amp supplies a fixed 8200 ms ceiling with the existing 100 ms lease; an old
amp driver cannot arm this preview. Neither duration nor output level is a
caller-supplied value. The DC filter, peak ceiling, silent left channel,
monitoring checks, explicit one-shot arm and shutdown behavior are unchanged.
Offline tests exercise both parent durations and the new amp authorization,
deadline, lost-heartbeat and owner-cleanup paths.

Source `3367a61` passed all four applicable CI workflows, including both preview
variants and the amplifier driver. The amplifier package installed as oem61.inf
13.40.35.108; the old oem53.inf package was exported before replacement.
Hardware preflight verified the extended capability while off and rejected
user-mode START, START_LISTEN, START_PREVIEW and RENEW with no amplifier enables.

The first extended dry run passed: Deadline stop at 8008 ms, 251 monitoring
checks, 53 boundary reads, no preview errors or late packets, and zero amplifier
enables/faults. Its captured rings contained 6,240 nonzero right samples, zero
nonzero left samples, and a right peak of 400,152,003 below the fixed ceiling.

One powered run then passed with Deadline stop at 8022 ms, 250 monitoring checks,
53 boundary reads, no preview errors or late packets, exactly one amplifier
enable, zero watchdog trips/faults, and verified off afterward. Its captured
rings contained 4,656 nonzero right samples, zero nonzero left samples, and a
right peak of 361,028,386. The cutoff is serviced by a passive software timer;
the measured 22 ms excess is reported rather than rounded to an exact 8000 ms.
The separate amplifier absolute ceiling remains 8200 ms from its enable.

The 21.696-second Ubuntu recording contains an eight-second waveform match at
about 7.488 seconds (normalized correlation 0.351). Neither microphone channel
clipped after its startup interval. This is evidence of the melody reaching the
speaker, not proof of distortion-free reproduction or safe temperature. User
feedback on clarity and temperature after the longer test is pending. All raw
recordings and machine-specific evidence remain private under ignored
`build/speaker-repair/preview-long-3367a61/`.

## Headphone validation status

The corrected headphone artifact from `4b0304c` installed as oem60.inf,
13.28.13.565. Its application transport started, but audible validation could
not proceed: while the stream was requested, the codec reported Playing=0,
JackPresent=0 and 16 jack polls, with success status and hardware mutes asserted.
The driver is waiting for jack detection; this is not evidence of a new playback
regression. The test now reports that condition explicitly before cleanup.
The user has been asked to connect headphones. Full two-cycle playback validation
remains pending and must not be reported as passed.

After the extended speaker test, the machine was returned to that corrected
headphone-only package with both Windows audio services running. The amplifier
remained off with no faults. The eight-second preview is available as a separate
test artifact, not left active for ordinary applications. No Windows reboot
occurred during this investigation; the recorded boot time remained 21:53:11.
