# N100 guarded IPC3 firmware-transfer test

On 2026-10-13, the N100's `8086:54c8` controller was temporarily bound to a test-signed development probe. The locally validated `sof-adl-n.ri` IPC3 image was staged as a 524,288-byte payload in a Windows DMA common buffer with a two-entry HDA buffer descriptor list. Both probe versions passed the Windows WDK build and separate INF verification before hardware testing.

The first probe ([CI run 38388103746](https://github.com/chevydan6/windows-sof/actions/runs/38388103746)) entered the DSP ROM and set output stream 0 RUN. The RUN bit read back as `0x0014001e`, but its position stayed zero, no stream error appeared, and ROM status stayed `0x05000001` throughout the bounded firmware wait. The driver returned result 11 (firmware-entered timeout), stopped the stream, and restored the registers. An independent post-test snapshot found the original idle output, capture, DSP power, and IPC values. The package was removed and normal boot verified before another attempt.

The Linux SOF [stream preparation](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-stream.c) also enables Software Position Based FIFO (SPIB) for the code-loader stream and sets its byte limit to the firmware size. That step was absent from the first probe. The N100 capability chain places SPIB at HDA offset `0x700`. With seven capture streams, output stream 0 has global index 7, so its enable bit is `0x80` at `0x704` and its byte-limit register is at `0x740`.

The second probe ([CI run 38388893146](https://github.com/chevydan6/windows-sof/actions/runs/38388893146)) guarded and restored those SPIB registers. Its one-shot test reported:

| Observation | Value |
| --- | --- |
| ROM initialization and error | `0x05000001` and `0` |
| SPIB control before / configured / restored | `0x00000000` / `0x00000080` / `0x00000000` |
| SPIB byte limit before / configured / restored | `0` / `524288` / `0` |
| Output control RUN / STOP | `0x0014001e` / `0x00140000` |
| Firmware status | `0x00000005` (firmware entered) after 2 polls |
| Firmware error and output stream status | `0` and `0` |
| Driver result | `0` |

The output position sampled after firmware entry was zero, so that register alone does not measure bytes transferred. The firmware-entered status is the positive result used by the Linux code-loader path. The probe immediately stopped the boot stream and reset/powered down the DSP; it does not keep firmware running or provide Windows audio. Its own restoration readback found ADSPCS `0x00000f0f`, output control `0x00040000`, PPCTL `0x40000000`, and clear IPC registers. An independent read-only snapshot confirmed the original idle output and capture stream, DSP power state, LTRP, and IPC registers.

The next milestone is a persistent DSP boot path: retain the firmware-running core, receive IPC3 firmware-ready information, and implement bounded IPC request/reply handling before building Windows audio circuits. No audio endpoint has been exposed yet.

After the successful test, the temporary probe package and certificate trust were removed. Verification after the 20:18:57 reboot found the controller stopped and driverless, with no probe service, package, registry diagnostics, certificate trust, or test-signing option.
