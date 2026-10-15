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
control). The current source converts left-aligned S32 PCM to S24_4LE by
dividing by 768: format conversion plus 9.54 dB attenuation. Windows endpoint
volume can further attenuate this fixed ceiling. The older isolated audition
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

- [Headphone transport build 38526206348](https://github.com/chevydan6/intel-sof-windows/actions/runs/38526206348), installed as `oem40.inf`, version `7.6.34.771`.
- [Codec build 38526206349](https://github.com/chevydan6/intel-sof-windows/actions/runs/38526206349), installed as `oem39.inf`, version `7.6.43.663`.

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


## Full-range calibration preparation

The user subsequently requested the highest practical clean headphone level.
The source now removes the extra fixed PCM attenuation while retaining the
format conversion. Signed full-scale input maps to -8,388,608 through
8,388,607 without overflow. Codec gain and speaker hold are unchanged.
This temporary revision was built and subsequently used for calibration.
Before installation, persist a -24 dB Windows endpoint level and verify it
afterward; increase only in measured steps with the headphones off ears.
The acoustic results above apply to the original -24.08 dB transport.

Full-range source `45db6da` passed [WDK build, INF validation and host tests
38528570160](https://github.com/chevydan6/intel-sof-windows/actions/runs/38528570160).
It was installed as `oem41.inf`, version `7.29.0.329`, for the gain sweep.
Endpoint identity changed during installation; volume persisted at -24.0824 dB.
The identity check muted the new endpoint until it was positively identified
and its low level verified.
`tools/analyze-headphone-capture.py` reproduces the earlier measurements and
optionally compares fundamental levels with a reference recording. It requires
NumPy/SciPy and rejects incomplete or unexpected microphone WAV formats.


## Gain sweep and chosen ceiling, 2026-10-14

With YouTube paused and headphones beside the microphones, the temporary
full-range transport played -1 dBFS L997/R1499 Hz tones at Windows levels
-24.0824, -18, -12, -6 and 0 dB. Each 16-second run recorded five seconds,
verified no late packets, and checked codec power-down and speaker hold.
The microphone fundamentals tracked the increments closely (each final
6 dB step measured about 5.94–6.05 dB). At 0 dB, recorded peaks were -12.62
and -23.38 dBFS, with no full-scale captured samples. The acoustic harmonic
ratios across both microphones/tones were 0.10–1.14%.

Bass validation used -1 dBFS L97/R151 Hz tones. The nearby right earcup's
151 Hz signal on microphone 1 rose 5.82 dB from Windows -12 to -6 dB, while
its harmonic ratio increased from 1.75% to 3.22%. This is evidence of
increasing acoustic nonlinearity; it does not isolate the DAC, headphones or
microphone as the cause. The distant left earcup's 97 Hz fundamental was
weak and its harmonic ratios cannot establish a useful electrical limit.
No further bass increase was attempted.

The selected production ceiling is therefore 1/4 (-12.04 dB), approximately
the lower bass-tested step and 12.04 dB louder than the original 1/16 ceiling.
This is a conservative practical setting for this headphone/placement, not
a certified distortion-free maximum for every load or frequency. Codec analog
gain and the speaker shutdown driver are unchanged. The full-range build was
for calibration; it is not the intended final everyday maximum.

Private gain recordings and logs remain under ignored `build/headphone-gain/`.
Analysis supports `--tones 97 151` for the bass recordings.


### Final installed gain validation

Source `ea9c9d9` passed [WDK build, independent INF verification and host tests
38529731127](https://github.com/chevydan6/intel-sof-windows/actions/runs/38529731127).
It is installed as `oem42.inf`, version `7.39.33.883`. The loaded service's
driver binary SHA-256 matches the pinned build artifact:
`D2717E1EBDFFC40907BC57C42162222B1C6DFE26B5FF63AAB7F24ED5E6EC8C8D`.
Codec and amplifier drivers were not replaced.

At Windows 0 dB, two bass playback cycles submitted 3,346 packets, completed
3,344, recorded zero late packets and exercised one PCM rearm. Both ordinary
stream releases muted and powered down the codec; cleanup verified DSP
baseline and speaker shutdown request. A brief interruption/transient in the
first final acoustic recording prompted a repeat rather than treating the
whole recording as steady state.

The repeat's 151 Hz fundamental differed from the earlier -12 dB calibration
reference by only -0.009 and -0.031 dB on the two microphones. On the nearby
microphone, its acoustic harmonic ratio was 1.98%, versus 3.22% at the louder
rejected step. The distant earcup/bass limitations above still apply. The
repeat completed successfully with a full five-second capture, no late packets
and verified shutdown. This does not claim zero distortion or a universal
maximum for arbitrary headphones/music.

Final readback reports 12.04 dB PCM attenuation, headphone-only mode and
speaker thermal hold. All test tasks were removed. Audio services are running
with automatic startup. Windows endpoint volume was left at -6 dB, unmuted,
for a first listen: about +6.04 dB versus the original full-volume output,
with another 6 dB available up to the new ceiling. No OS reboot was performed.


### Requested additional small increase

The user requested a little more output after the previous calibration and
had already raised Windows endpoint volume to 0 dB. The source ceiling was
changed from 1/4 to 1/3 (+2.50 dB), using signed division by 768 for PCM
conversion plus attenuation. Full-scale values remain within 24-bit bounds
(-2,796,202 through 2,796,202). Status reports 9.54 dB attenuation.

This lies between the previous -12 and -6 dB acoustic checks; it is not a
new acoustically certified clean maximum. No fresh audible test tones are
started for this adjustment because headphone placement is not confirmed.
The codec's analog gain and speaker hold remain unchanged.

Source `90b9722` passed [WDK build, independent INF verification and host tests
38530664398](https://github.com/chevydan6/intel-sof-windows/actions/runs/38530664398).
Installed as `oem43.inf`, version `7.47.54.802`. The installed binary hash
matches the pinned artifact:
`1C606819CE87D564CD7AEA39308848106D3221406A5762A56B4927950CCDC854`.
Two silent playback cycles submitted 3,532 packets and completed 3,530 with
zero late packets and one PCM rearm; both verified normal stream shutdown.
Readback confirms the 9.54 dB ceiling, headphone-only mode and speaker hold.
Windows volume is restored to 0 dB, unmuted, as found before this request;
audio services are running automatically and the test task is removed.
This validation checks transport/lifecycle, not acoustic distortion at the
new intermediate ceiling. No OS reboot or audible calibration tones were used.
