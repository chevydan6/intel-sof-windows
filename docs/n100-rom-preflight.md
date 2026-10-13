# N100 ROM-entry preflight

On 2026-10-13, CI run [38381751162](https://github.com/chevydan6/windows-sof/actions/runs/38381751162) built a read-only probe and passed separate INF verification. The probe bound to `8086:54c8`, then read the first HDA input stream and the DSP's Cannon Lake-style IPC registers before any ROM-entry write.

| Register | Observed value |
| --- | --- |
| GCAP | `0x9701` |
| Input stream 0 control/status | `0x040000` / `0x00` |
| Input stream 0 CBL/LVI/format/BDL | all zero |
| Processing-pipe control | `0x40000000` |
| LTRP guardband register | `0xA9` |
| ADSPCS | `0x00000F0F` |
| HIPCIDR / HIPCIDA / HIPCCTL | all zero |
| ADSPIC / ADSPIS | both zero |

Linux prepares an ICCMAX capture stream and starts its DMA before releasing the DSP core. The measured input stream is idle and its processing-pipe bit is clear, so a future controlled test can require these values before modifying either. The guardband's low six bits are `0x29`; its upper bits must be preserved if applying Linux's temporary 95 µs setting.

The probe was uninstalled and the package and temporary trust removed. After the 19:15:36 reboot, verification found the controller driverless and stopped, with no probe service, package, diagnostics, test certificate, or test-signing option. SSH took longer than the previous reboots to become reachable, but the post-boot verification passed.

Sources: [Linux HDA loader](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-loader.c), [stream setup](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-stream.c), and [register definitions](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda.h).
