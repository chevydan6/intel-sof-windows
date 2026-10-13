# N100 bounded ICCMAX capture DMA test

On 2026-10-13, CI run [38384472650](https://github.com/chevydan6/windows-sof/actions/runs/38384472650) built a one-shot capture DMA probe and passed separate INF verification. On the N100, the bound driver checked the exact idle capture, DSP, IPC, PCI command, and LTRP values. It created a 4 KiB WDF common buffer and a two-entry HDA BDL, configured input stream 0, applied Linux's temporary LTRP guardband, set RUN for about 2 ms, and stopped it before releasing either buffer.

| Observation | Value |
| --- | --- |
| PCI command | `0x0006` (memory access and bus master enabled) |
| Configured stream control | `0x00140000` |
| RUN acknowledged | `0x00140002` |
| STOP acknowledged | `0x00140000` after one stop poll |
| Position after the short run | `0` |
| Capture status | `0x00` (no descriptor or FIFO error) |
| Processing-pipe control during setup | `0x40000001` |
| Driver result | `0` |

The zero position means this test verifies RUN/STOP control and clean register restoration, but does **not** establish that capture data was transferred. No DSP core was powered, no ROM IPC was sent, and firmware did not run. An independent read-only snapshot afterward found capture control/status `0x040000/0x00`, all capture transfer registers zero, PPCTL `0x40000000`, LTRP `0xA9`, ADSPCS `0x00000F0F`, and clear IPC registers.

The test package and temporary trust were removed. After the 19:40:08 reboot, verification found the controller stopped and driverless, with no probe service, package, diagnostics, certificate trust, or test-signing option.

The next milestone is a separately guarded ROM-control handshake with the prepared code-loader stream, followed by firmware DMA only after ROM initialization is verified. The Linux reference sequence is in [hda-loader.c](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-loader.c).
