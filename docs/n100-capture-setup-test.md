# N100 guarded ICCMAX capture-stream setup

On 2026-10-13, CI run [38382856189](https://github.com/chevydan6/windows-sof/actions/runs/38382856189) built the one-shot capture setup probe and passed separate INF verification. The driver checked exact idle capture, processing-pipe, IPC, and DSP baseline values before any stream write. It allocated a 4 KiB aligned capture buffer and two 2048-byte descriptors, as the Linux ICCMAX path requires two HDA descriptor entries.

The probe reset input stream 0, waited for acknowledgment, programmed the parameters, read them back, and restored the original values before returning. It did **not** set RUN, fetch DMA data, power the DSP core, or enter the ROM.

| Register or result | Configured | Restored |
| --- | --- | --- |
| Stream control | `0x140000` | `0x040000` |
| Cyclic buffer length | `4096` | `0` |
| Last valid index | `1` | `0` |
| Format | `0x0040` | `0x0000` |
| BDL address | `0x00000001514FF000` | `0` |
| Processing-pipe control | `0x40000001` | `0x40000000` |
| Reset acknowledgment and result | `true`, `0` | — |

A separate read-only preflight immediately afterward confirmed capture control/status `0x040000/0x00`, all capture transfer registers zero, processing-pipe control `0x40000000`, LTRP `0xA9`, ADSPCS `0x00000F0F`, and clear IPC registers. The DMA buffers were freed only after the stream had been restored.

The driver package and temporary trust were removed. After the 19:25:37 reboot, verification found no bound driver, probe service, package, diagnostic values, test certificate, or test-signing option. The controller was stopped and driverless.

The next boot test must additionally start and stop ICCMAX capture DMA, initialize the ROM by its IPC handshake, and guarantee DMA quiescence before freeing any buffer.
