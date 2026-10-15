# Right-speaker normal-use trial

The user requested ordinary application playback instead of further timed
previews. `RightTrial=true` builds a separate package that starts on stream
preparation and can play beyond thirty seconds. It rearms after a successful
pause/stop, while faults remain latched until device restart. Normal enumeration
still leaves firmware and the amplifier off until an application opens a stream.

The physical left channel remains zero. Stereo app content is mixed into the
right channel through a DC filter and fixed peak ceiling. The original trial
used -12.04 dB; the volume correction below raises it to the board
configuration's -9 dB. This
speaker-only trial does not integrate automatic headphone-jack routing.

A distinct amplifier START_STREAM command retains its 100 ms renewal lease
without an absolute playback cap. The parent still checks DMA advancement,
valid PCM and callback timing before renewing. Stream pause, cleanup and power
exit request off. A running stream with no nonzero PCM in the buffer for 250 ms
also requests off; new valid audio can enable again. Faults never auto-rearm.
Power transitions preserve a parent fault latch. These are software protections,
not a measurement of speaker temperature.

`RightTrialDry=true` builds the same lifecycle with amplifier enables disabled,
for checking repeated playback before installing the powered trial. The older
bounded preview builds and commands retain their limits. Trial mode rejects
manual ARM commands, so a user process cannot change its policy.

Offline tests exercise two minutes of renewals, silence/off/audio-resume,
repeated normal stream arming, query failures, lost heartbeats, invalid PCM,
DMA errors, callback delays, ownership and kernel-only enable authorization.
Source `c909fda` passed all four CI workflows, including WDK compilation and
independent INF checks. ACX run `38664599750` built both trial variants and all
older previews; amplifier run `38664599689` passed stream/diagnostic lifecycle
checks. Amplifier oem65.inf (1.15.12.359), dry parent oem66.inf (1.15.32.16),
and powered parent oem67.inf (1.15.14.481) installed without a Windows reboot.
User-mode enable and renewal requests, including START_STREAM, were rejected
with the amplifier remaining off.

## Application verification

`tools/invoke-right-speaker-trial.ps1` runs the desktop worker from SSH. The
fixture is a 50-second, 48 kHz stereo PCM WAV at the previous test gain, with
exact silence from seconds 3 through 6. No manual ARM request is used. The
worker checks output after starting an ordinary SoundPlayer stream, observes
silence/off and resumed audio, verifies the stream is still active at 35 seconds,
stops, and plays a second two-second session without restarting the device.

Both dry and powered checks passed. Powered stream durations observed by the
driver were 34738 ms and 1889 ms (application startup precedes the stream).
There were no driver errors, late packets, amplifier faults or watchdog trips.
The second session performed one PCM reset. The amplifier was off during the
silent section, on after sound resumed, and off after both stops, observed at
51 ms and 53 ms from the stop requests. Enable counts were exactly three:
initial sound, return from silence, and the second stream. The captured left
channel was all zero; right peak 378,762,180 stayed below the existing ceiling.

The private 50.987-second microphone recording had no clipped samples on either
channel after startup. This does not establish long-duration temperature or
sound quality. Evidence is retained under ignored
`build/speaker-repair/right-trial-c909fda/`.

At the user's explicit request the powered trial is left installed for ordinary
application testing, with Windows audio services running. It is no longer
switched back to the separate headphone package after verification. Broader
normal-use feedback and sleep/resume testing remain outstanding. A ten-minute
read-only status logger records preview state and amplifier faults during the
initial user trial; it does not record microphone audio or change playback.

## Quiet playback report

The user reported low output with both Windows and the video player's volume
at maximum. The endpoint was verified at 0 dB, unmuted. The first trial retained
an extra -3.04 dB of attenuation from the bounded speaker tests. The trial-only
conversion now uses `(left + right) * 11626 / 65536`, with symmetric ceiling
761,921,535. This gives the averaged stereo content the board configuration's
-9 dB maximum; it does not boost above that board setting. Earlier bounded
preview modes keep their original -12.04 dB setting and now report it accurately.
The left slot, DC filter, idle gate, fault latch and amplifier watchdog remain.
New-package build and listening verification are pending.
