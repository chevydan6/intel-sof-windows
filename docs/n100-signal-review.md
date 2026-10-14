# Signal investigation with playback held, 2026-10-14

This investigation follows the [amplifier thermal review](n100-amplifier-thermal-review.md). It did not play sound or lift the thermal hold. The reported buzzing and heating remain unresolved.

## Windows format negotiation

A guarded, format-only check temporarily started the Windows audio services, queried the endpoint, and attempted client initialization. It never started a client or submitted samples. The held driver rejected hardware preparation with STATUS_DEVICE_NOT_READY, as expected. Both audio services were stopped again afterward.

The driver observed WAVE_FORMAT_EXTENSIBLE, stereo 48 kHz integer PCM, 32-bit containers and 24 valid bits. Readback retained ThermalHold 1 and RunCalls 0. This establishes the format offered during this check, not the format of every historical stream. A floating-point WASAPI loopback capture does not establish that the hardware driver received floating-point samples.

Microsoft specifies that valid bits are left-aligned within a larger PCM container. Consequently, this 24-in-32 input is compatible with the signed 32-bit transport used by the driver; shifting it left again would be wrong. See [WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible).

The previous acceptance check tested rate, channel count, and block alignment without fully validating the sample encoding. Commit `5cefe8d` adds a complete integer-PCM format guard, including the full subtype GUID, extension length, widths, channel mask, and byte rate. This closes a real validation gap but is not evidence that a floating-point stream caused the original distortion.

Local sanitizer tests exercised the production format and attenuation helpers. The [Windows build and INF verification](https://github.com/chevydan6/windows-sof/actions/runs/38496792997) passed for this commit. The format change has been pushed but has not been installed on the laptop. The previously verified held ACX and shutdown-only amplifier packages remain installed.

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
