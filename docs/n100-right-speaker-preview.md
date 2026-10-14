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
SHA-256 values match. The correction requires a new WDK build and another dry
hardware test; neither audible application playback nor sustained speaker
operation has been validated yet.
