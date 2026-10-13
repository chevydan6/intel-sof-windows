# N100 playback echo investigation

After the reboot tests below, Dan confirmed that the echo was gone. He reported remaining distortion resembling clipping; that follow-up is documented in [speaker output level](n100-speaker-gain.md).

On 2026-10-14, the user confirmed that ordinary YouTube audio reaches the speakers with the automatically initialized ACX driver, but described distortion and an echo, as though sounds were played twice. Audible output is therefore confirmed; usable audio quality is not.

The reported red error window was consistent with the interactive test harness failing its new-stream assertion. The actual captured failure was `Playback did not advance the expected driver/DMA counters`: while YouTube already owned a shared stream, the WAV completed and packet counters increased, but `RunCalls` stayed at 5. The harness now permits shared-stream reuse and labels the counter evidence accordingly; `-RequireNewStream` retains the stricter check for isolated tests. The laptop did not reboot, no new blue-screen dump appeared, and the user clarified that Windows itself had not blue-screened. This is not evidence of a new kernel crash.

## Candidate correction

Source `b484a8d`, WDK build [38460312959](https://github.com/chevydan6/windows-sof/actions/runs/38460312959), changes the render path:

- Clear DMA-consumed bytes under the same lock used for packet writes, so an underrun does not replay data from the preceding revolution of the 49,152-byte ring (128 ms at the hardware format).
- Reject writes behind the DMA cursor with `STATUS_DATA_LATE_ERROR`, and reject writes beyond available ring space.
- Use a high-resolution 1 ms WDF timer. A 1 ms period on the default timer did not itself select high-resolution timing. See [Microsoft's timer configuration documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdftimer/ns-wdftimer-_wdf_timer_config).
- Track the cursor from buffer preparation, including data consumed during the firmware START handshake; report presentation position with a contemporaneous timestamp.
- Increment the current-packet index before notifying ACX that the preceding packet completed.
- Add status version 4: per-preparation late packet count, maximum observed DMA advance between samples, and submitted packet size.

The exact audible cause is not yet proven. These changes address concrete stale-data and timing defects; only testing the loaded new driver can establish whether they resolve the reported echo. Position unwrapping still assumes samples occur within one ring revolution. Comprehensive interrupt-driven timing and end-of-stream handling remain unfinished.

## Validation and activation

Both ACX build `38460312959` and standalone probe build `38460313307` passed. `python3 tools/test-acx-ring.py` compiles the production advance/write routines unchanged against a mock DMA cursor, with AddressSanitizer and UndefinedBehaviorSanitizer. Tests passed for consumed-data clearing, underrun silence across a revolution, rejection of late/overrun writes, wrapped packet copy/clear, and invalid cursor/error-status handling. This harness does not test WDF concurrency, physical DMA, or audible quality.

During the live YouTube baseline, the old driver reported 39,776 submitted packets, 40,042 completion notifications, six runs, hardware status `0x20`, and no recorded callback failure. These counters establish ongoing transport, not quality or exact sample delivery.

The candidate package was staged as `oem24.inf`, version `20.8.20.781`, with signer thumbprint `62C10291592EC405F1D8ADE1A2790DD4584168F7`. Windows returned installation code 3010. SetupAPI recorded an outstanding-open veto while audio was playing. A subsequent attempt with audio services stopped could not restart the device because Windows marked the operation as pending reboot. Both audio services were restarted afterward.

Immediately after staging, the old version-3 binary was still running even though the PnP package properties named the new INF. Status version 4 fields were required to establish that the corrected binary had actually loaded; package properties alone were insufficient.

The new package and scripts are in `C:\Users\dan\sofplay-acx17-test`. The old package/signer remain available for recovery. `rollback-acx17-test.ps1` removes the development audio packages, trust entries through ACX17, the test-signing setting, and the pinned firmware copy, followed by a required reboot; it has not been run.

## After the user's reboot

The user restarted Windows; the reported boot time was `2026-10-14 05:26:21` (Windows local time). The connection returned after a short startup delay. Status version 4 fields confirmed the corrected binary was active, with startup count 1, stage 6, success status, and hardware ready. An initial stream had submitted 333 packets with zero late submissions.

Two additional three-second WAVs ran in the signed-in desktop (session 1) at `05:27:29`–`05:27:42`. The task and test exited successfully. Results were:

| Check | Runs | Submitted | Completion notifications | PCM resets | Late packets | Maximum sampled advance |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Before explicit tests | 1 | 333 | 332 | 0 | 0 | 4,608 bytes |
| After WAV 1 | 2 | 641 | 639 | 1 | 0 | 6,144 bytes |
| After WAV 2 | 3 | 944 | 941 | 2 | 0 | 6,528 bytes |

Packet size was 3,840 bytes. Each stream started at hardware position zero. No callback failure was recorded, and hardware status stayed `0x20`. Late-packet and maximum-advance counters reset when preparing each stream; the latter includes the initial firmware-start handshake before periodic polling. These results establish automatic startup and repeated packet transport with the corrected driver. They do not establish that music/speech sounds correct; the user was asked to retry YouTube and report whether the echo remains.
