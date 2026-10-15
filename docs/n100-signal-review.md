# Signal investigation with playback held, 2026-10-14

This investigation follows the [amplifier thermal review](n100-amplifier-thermal-review.md). It did not play sound or lift the thermal hold. The reported buzzing and heating remain unresolved.

## Windows format negotiation

A guarded, format-only check temporarily started the Windows audio services, queried the endpoint, and attempted client initialization. It never started a client or submitted samples. The held driver rejected hardware preparation with STATUS_DEVICE_NOT_READY, as expected. Both audio services were stopped again afterward.

The driver observed WAVE_FORMAT_EXTENSIBLE, stereo 48 kHz integer PCM, 32-bit containers and 24 valid bits. Readback retained ThermalHold 1 and RunCalls 0. This establishes the format offered during this check, not the format of every historical stream. A floating-point WASAPI loopback capture does not establish that the hardware driver received floating-point samples.

Microsoft specifies that valid bits are left-aligned within a larger PCM container. Consequently, this 24-in-32 input is compatible with the signed 32-bit transport used by the driver; shifting it left again would be wrong. See [WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible).

The previous acceptance check tested rate, channel count, and block alignment without fully validating the sample encoding. Commit `5cefe8d` adds a complete integer-PCM format guard, including the full subtype GUID, extension length, widths, channel mask, and byte rate. This closes a real validation gap but is not evidence that a floating-point stream caused the original distortion.

Local sanitizer tests exercised the production format and attenuation helpers. The [Windows build and INF verification](https://github.com/chevydan6/intel-sof-windows/actions/runs/38496792997) passed for this commit. The format change has been pushed but has not been installed on the laptop. The previously verified held ACX and shutdown-only amplifier packages remain installed.

## Offline sample checks

A sine wave passed through the production attenuation helper with measured gain -9.000392 dB and fitted residual about 0.492 signed-32 sample units. The earlier three-second WASAPI loopback recording was also converted offline to 24-in-32 PCM and passed through that helper. Output peaks were approximately 0.186 and 0.188 of full scale, with no near-full-scale output samples. The measured gain was again -9.000392 dB; maximum normalized rounding/conversion error against scaled input was about 2.16e-8.

These checks did not reproduce clipping in the conversion or attenuation arithmetic. The recording is a software loopback capture, not a microphone or amplifier-output measurement. It cannot reveal downstream DMA, DSP, serial-interface, amplifier, or speaker faults. No recording content was uploaded. Local evidence is under ignored `build/distortion/`, including `held-format-result.json` and `offline-signal-report.json`.

## Packaged Linux topology comparison

`tools/inspect-speaker-link.py` decoded the binary SSP1 link in the packaged `sof-adl-max98360a-rt5682.tplg`. Its SHA-256 is `52fab042fb589cecb009be56c3deda85d6ec2291d49e4b2bd95854b54b88be80`, matching the decompressed installed Ubuntu topology.

The reference specifies I2S, 48 kHz, a 3.072 MHz bit clock, two 32-bit slots, normal clock polarity, and transmit/receive slot masks of 3. These agree with the Windows speaker configuration. This is a packaged reference comparison, not a successful Linux playback test on this laptop.

The reference requests a 19.2 MHz master clock, while the Windows configuration requests 24 MHz. The MAX98360 does not consume a master-clock input, but SOF clock-source selection can still depend on the requested master clock. SOF's divider code can derive the requested bit clock through several clock sources. Therefore this difference alone does not prove an incorrect physical bit clock. No speculative clock change was made.

References: [MAX98360 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98360A-MAX98360D.pdf), [SOF v2.2 SSP configuration](https://github.com/thesofproject/sof/blob/v2.2/src/drivers/intel/ssp/ssp.c), and [SOF clock dividers](https://github.com/thesofproject/sof/blob/v2.2/src/drivers/intel/ssp/mn.c). The source version is an implementation reference, not independent identification of the installed firmware version.

## Remaining boundary

The shutdown-only amplifier driver previously reported that the GPIO controller accepted an output-low request. That is not an independent voltage or temperature measurement. Actual serial-audio timing, amplifier output, speaker integrity, and the original thermal cause remain unverified. Keep speaker playback held; these software checks do not establish that powered speaker tests are safe.

## Continued review: final packets and DMA feeding

The prototype ignored `Flags` and `EosPacketLength` in its render callback. Consequently it copied an entire client packet even when Windows specified only a shorter valid prefix, including a zero-length final packet. This can publish stale samples after the requested end of audio. It is a concrete defect, but does not establish the cause of continuous buzzing or heating.

Commit `ff05c1b` passes those fields into the locked ring writer. It validates flags and stereo-frame alignment, copies only the valid prefix, clears the remaining packet bytes across ring wrap, and rejects submissions after an accepted end-of-stream. Successful buffer preparation resets that state. Notification timing is unchanged. Microsoft documents these requirements in [EvtAcxStreamSetRenderPacket](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/acxstreams/nc-acxstreams-evt_acx_stream_set_render_packet).

The sanitizer test executes the production preparation, cursor, and packet-copy routines. It checks all 481 possible stereo-frame lengths of a 3,840-byte final packet at both an ordinary and a wrapping ring position, verifies that surrounding queued data is preserved, rejects subsequent submissions and malformed arguments, and verifies that a fresh preparation permits playback packets again. Existing attenuation, underrun, late-packet, and format/thermal-hold tests also pass. The first WDK build exposed a missing prerequisite header for `ks.h`; `83228eb` adds it. None of these changes has been installed during the thermal hold.

The pinned PUJJOTEEN MAX-amplifier [dsp.ini](https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/93dea50b71007195324090989cb5b0d8759bc177/overlay-nissa/chromeos-base/chromeos-bsp-nissa/files/pujjoteen/audio/cras-config.max98357/dsp.ini) contains source/sink definitions without EQ or compressor coefficients; the accompanying `board.ini` and `apm.ini` are empty defaults. These files do not supply an additional speaker filter to port. This does not prove that every ChromeOS version or processing layer lacks protection.

Linux v6.8 [hda-pcm.c](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/v6.8/sound/soc/sof/intel/hda-pcm.c) enables SPIB only when its `disable_rewinds` option is enabled, otherwise explicitly disabling it. Its acknowledgement callback publishes the application position when that mechanism is used. Therefore the Windows prototype's lack of SPIB publication is not by itself proof of a broken transport. Real scheduling, underruns, and serial output still need measurement; no speculative SPIB register change was made. The Windows cursor calculation still assumes observation within one 128 ms ring revolution.

A subsequent read-only target check again returned ThermalHold 1, HardwareReady 0, zero runs/submissions/completions, both audio services Stopped, and amplifier stage 3/status 0/off-write count 1. No playback, firmware startup, driver installation, or reboot occurred during this continuation.

The corrected commit `83228eb` passed the [ACX Windows build, INF verification, and offline tests](https://github.com/chevydan6/intel-sof-windows/actions/runs/38497684602) and the [standalone probe build](https://github.com/chevydan6/intel-sof-windows/actions/runs/38497684706). This verifies the software change's build and tested buffer behavior, not audible or thermal repair.
