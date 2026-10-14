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

`right-speaker-preview.ps1 -Diagnostics` reads a separate 48-byte diagnostic
IOCTL without changing the original status ABI or any playback limits. It saves
the first stop reason, phase, elapsed time, callback gap, previous and observed
DMA positions, transport flags and advance status. Later cleanup retains that
snapshot; a new device initialization clears it. The test worker saves this
information even when the final preview status reports failure. Offline fault
injection verifies reason selection and preservation through cleanup.

After saving the results, restored the previously working headphone package
oem54.inf 11.0.0.373 and restarted both Windows audio services. The amplifier
was verified off with no faults. That older headphone package does not contain
the new packet-lifetime fix; the corrected headphone artifact passed CI but
still needs its own hardware validation before replacing the known baseline.
