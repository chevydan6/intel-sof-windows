# N100 guarded DSP ROM-entry test

On 2026-10-13, CI run [38385449331](https://github.com/chevydan6/intel-sof-windows/actions/runs/38385449331) built the Windows probe and passed separate INF verification. The temporary, test-signed driver bound to the N100's `8086:54c8` audio controller. An exact idle-register check and a locally validated IPC3 image staged in DMA memory were required before the one-shot ROM-entry diagnostic could write registers.

The probe configured output stream 0 for the firmware payload without setting its RUN bit. It powered DSP core 0, saved and set the three SSP SSC1 bits used by the Linux boot sequence, sent ROM control `0x81004000`, released core reset and stall, and waited for the ROM acknowledgment and initialization state. It then restored the SSP bits, powered the core down, and reset the stream. It did **not** start firmware DMA or expose an audio device.

| Observation | Value |
| --- | --- |
| Core power acknowledged | ADSPCS `0x01010F0F` |
| Reset released | ADSPCS `0x01010F0E` |
| Stall released | ADSPCS `0x01010E0E` |
| ROM-control IPC | `0x81004000` |
| IPC acknowledgment | `0x80000000` after 1 poll |
| ROM status | `0x05000001` after 4 polls (initialization done) |
| ROM error | `0x00000000` |
| Driver result | `0` |

The driver reported its restored ADSPCS `0x00000F0F`, output control `0x00040000`, PPCTL `0x40000000`, and clear IPC registers. An independent read-only snapshot found the capture stream idle with no status errors, PPCTL `0x40000000`, LTRP `0xA9`, ADSPCS `0x00000F0F`, and clear IPC registers. Output stream 0 independently read back its original control `0x040000`, zero length/format/BDL, and no stream error.

This proves that the N100 DSP ROM accepts the host's entry sequence and reaches the point where it can accept firmware. It does not prove that the firmware payload transfers or starts. The next milestone is a guarded code-loader DMA RUN/STOP test with bounded waits, firmware-entered status capture, and restoration on every exit path.

The test package and temporary trust were removed. After the 19:50:07 reboot, verification found the controller stopped and driverless, with no probe service, package, diagnostics, certificate trust, or test-signing option.
