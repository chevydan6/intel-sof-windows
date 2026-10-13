# Lenovo 14e Chromebook Gen 3 N100 speaker tone test

On 2026-10-13, WDK build [38404366170](https://github.com/chevydan6/windows-sof/actions/runs/38404366170) compiled and passed INF verification. Its signed development probe was temporarily installed on the Intel `8086:54C8` controller in a Windows test boot. The target identifies as Google PUJJOTEEN / Lenovo 14e Chromebook Gen 3 and exposes the `MX98360A` speaker amplifier on SSP1.

The one-shot test in [`tools/test-speaker-tone.ps1`](../tools/test-speaker-tone.ps1) created a host-buffer-SSP1 SOF IPC3 pipeline, allocated a 48 KiB stereo S32 48 kHz triangle-wave buffer, sent PCM parameters, started HDA DMA and the DSP stream, ran for three seconds, then stopped and freed the stream. All IPC replies reported error 0. The final HDA stream position was 23,424 bytes and status was `0x20` (`SD_STS_FIFO_READY`, with no descriptor or FIFO error bits). Dan reported hearing the beep. The managed session stop restored `ADSPCS` to `0x00000F0F` with cleanup flags 0.

This verifies physical speaker playback from the test path. It does not install a Windows audio endpoint or support normal app playback. After rebooting, the rollback check confirmed that the temporary driver package, service, trusted test certificate, and test-signing boot setting were all gone, and the controller had returned to its normal stopped state.
