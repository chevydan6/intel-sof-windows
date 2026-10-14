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
19.2 MHz MCLK. Only one channel receives a 1 kHz sine at -48.16 dBFS peak;
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
The last header word contains mode 1..4 plus flags: `0x400` output DMA advanced,
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
