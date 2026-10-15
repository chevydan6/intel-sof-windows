# Right-speaker normal-use trial

The user requested ordinary application playback instead of further timed
previews. `RightTrial=true` builds a separate package that starts on stream
preparation and can play beyond thirty seconds. It rearms after a successful
pause/stop, while faults remain latched until device restart. Normal enumeration
still leaves firmware and the amplifier off until an application opens a stream.

The physical left channel remains zero. Stereo app content is mixed into the
right channel at the previously tested gain, DC filter and peak ceiling. This
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
Windows builds and hardware validation are pending.
