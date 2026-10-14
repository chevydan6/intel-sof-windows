# Normal headphone playback on PUJJOTEEN

Normal Windows headphone playback is implemented and installed on the N100
Lenovo 14e Chromebook Gen 3. Applications can use the headphone render endpoint
without the old three-second audition command. Windows software volume and mute
work. The driver powers the RT5682 headphone output for an active stream, shuts
it down when the stream stops, and handles jack removal/reinsertion during
playback. The speaker amplifier remains held off.

## Implementation

The parent starts the codec's fixed stream operation before starting output DMA.
This lets jack detection and depop settle before the ACX packet timer starts,
avoiding consumption of an unserviced ring during analog setup. Stop mutes and
powers down the codec before stopping the DSP playback pipeline. The existing
PCM rearm path handles later streams without a device or OS restart.

The codec polls jack presence every 100 ms while a stream owns the output,
using a nonperiodic passive WDF timer and an explicit wait lock shared with
control requests and file cleanup. A removed jack shuts down the output and
leaves only jack detection active; insertion reinitializes the muted codec and
restores playback. Transport failure stops polling and attempts complete
shutdown. Closing the stream owner's codec handle also shuts the output down.
Other query handles do not own or stop playback. D0 exit cancels the timer before
closing I2C, without waiting for its callback while holding its lock.

Normal playback uses unity DAC gain (`0xaeae`, per the Linux RT5682 volume
control) and uses the transport's fixed 1/8 (-18.06 dB) output ceiling.
Windows volume is applied before that ceiling. The older isolated audition
still uses -39 dB DAC gain, but its test wrapper is superseded for the normal
playback package. No arbitrary gain/register controls were added.

The render endpoint stays selectable while unplugged and plays silently; jack
handling does not currently send Windows endpoint-connectivity notifications.
The hardware stream remains fixed at 48 kHz, stereo, 24 valid bits in a 32-bit
container. Windows shared-mode playback performs its normal format conversion.

References: [Linux RT5682 codec](https://github.com/torvalds/linux/blob/v6.8/sound/soc/codecs/rt5682.c),
[WDF passive timers](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-timers),
[WdfTimerStop locking restrictions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdftimer/nf-wdftimer-wdftimerstop).

## Validation, 2026-10-14

Source `e238066` passed WDK build and independent INF verification:

- [Headphone transport build 38526206348](https://github.com/chevydan6/windows-sof/actions/runs/38526206348), installed as `oem40.inf`, version `7.6.34.771`.
- [Codec build 38526206349](https://github.com/chevydan6/windows-sof/actions/runs/38526206349), installed as `oem39.inf`, version `7.6.43.663`.

Host tests exercise the production analog and stream routines: normal
start/stop/restart, initially absent jack, unplug/replug, failed poll, 172
injected stream-start transfer failures, 115 audition failures, 58 calibration
failures, and the existing format, ring, PCM-guard and DMIC cleanup checks.

Hardware tests used ordinary Windows SoundPlayer playback, not a hardware tone
IOCTL. Two consecutive 16-second streams completed with 3,213 packets submitted,
3,211 completed, no late packets, and one successful PCM rearm. Both stream
releases verified codec mute and digital power zero. A second two-stream test
exercised Windows endpoint volume/mute and also completed without late packets.
All restart tests reused the running DSP session between streams.

The user physically unplugged and reconnected the headphones during a silent
Windows stream. The monitor observed:

| Observation | Time from test start | Codec state |
| --- | --- | --- |
| Stream active, headphones connected | 2.84 s | Requested=1, Playing=1, Jack=1, HP mute=0, digital power=`0x8d01` |
| Plug removed | 11.74 s | Requested=1, Playing=0, Jack=0, HP mute=`0x8080`, digital power=0 |
| Plug reinserted | 22.76 s | Requested=1, Playing=1, Jack=1, HP mute=0, digital power=`0x8d01` |

These are observation times, not measured detection latencies. The transition
completed automatically within the same Windows stream.

## Acoustic checks

The headphones were beside the Windows laptop's two built-in microphones.
The fixed DMIC diagnostic recorded five seconds at 48 kHz, stereo S32_LE while
normal headphone playback continued. Raw recordings remain private in ignored
local `build/headphone-stream/`; they are not in GitHub or driver artifacts.

Distinct tones were supplied to left (997 Hz) and right (1499 Hz). Both tones
were captured. The different microphone/earcup positions produce unequal
recorded levels, so this does not establish calibrated channel balance.

Windows endpoint volume began at 0 dB. Changing it to -12 dB reduced both
recorded tones by approximately 12 dB; mute reduced them to the room noise
floor, and restoring volume restored the earlier tone levels. The test restored
the original Windows volume/mute settings in `finally`.

A final 16-second test increased each channel from amplitude 4,096/32,768 to
-1 dBFS (about +17.06 dB). The first microphone's fundamentals rose 17.16 and
17.32 dB, consistent with approximately linear output rather than limiting.
There were zero full-scale microphone samples; microphone peaks were -33.85
and -38.21 dBFS. Summed second-through-fifth harmonic amplitude was about
0.44–0.80% of the fundamentals across the two microphones and tones. Those are
**acoustic chain measurements**, including headphone transducers, room noise,
placement and microphones—not calibrated driver/DAC THD figures. They show no
obvious clipping under the tested conditions, and do not establish performance
at every frequency or resolve the disabled speakers' heating/distortion fault.

Analysis used the central four seconds, 48,000-sample Welch windows, and power
in +/-2 Hz bands around the fundamentals and harmonics. The per-tone percentage
is `100 * sqrt(sum(harmonic powers) / fundamental power)`.

## Test tools and limits

`tools/test-headphone-playback.ps1 -Wav <16-second PCM WAV> -Record <capture.wav>`
runs two normal playback cycles, checks counters and stream-owned power-down,
and captures the first cycle. `-ControlTest` additionally exercises Windows
volume/mute; `-Cycles 1` is useful for a single acoustic run. Cleanup stops
audio services and resets only the parent device. `-LeaveReady` restarts audio
services after successful checks. `tools/test-headphone-jack.ps1` uses a bounded
silent WAV while the user physically removes/reinserts the plug.

This remains an experimental, test-signed driver for the exact reviewed board.
Suspend/resume and surprise-removal recovery have not been hardware validated.
Headset microphone/button controls and a normal Windows built-in microphone
endpoint remain outside this headphone render implementation. The built-in
microphones currently work through the diagnostic capture tool only.

## Requested volume increase

After confirming YouTube playback sounded correct, the user requested a small
volume increase. Windows volume was already 0 dB and unmuted. The transport
ceiling was raised from 1/16 to 1/8 (+6.02 dB); the codec and speaker hold were
unchanged. Full-scale signed PCM conversion remains within 24-bit bounds with
18.06 dB of headroom. The acoustic results above precede this gain increase.
The status reader recognizes the earlier version-8 hardcoded speaker field
and reports the actual attenuation for new headphone builds.
