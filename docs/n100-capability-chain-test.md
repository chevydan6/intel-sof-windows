# N100 complete HDA capability chain (2026-10-13)

The bounded read-only probe from [CI run 38373417589](https://github.com/chevydan6/intel-sof-windows/actions/runs/38373417589) built with the WDK, passed x64 `InfVerif /w`, and loaded on the N100. Its package files matched the CI hashes before installation. It followed the HDA extended-capability pointers within the validated 16 KiB HDA resource and stopped after five entries:

| Order | Offset | Header | ID | Capability |
| ---: | ---: | ---: | ---: | --- |
| 0 | `0x0c00` | `0x00020800` | 2 | Multi-Link |
| 1 | `0x0800` | `0x00030500` | 3 | Processing Pipe |
| 2 | `0x0500` | `0x00011f00` | 1 | Global Time Synchronization |
| 3 | `0x1f00` | `0x00050700` | 5 | DMA Resume |
| 4 | `0x0700` | `0x00040000` | 4 | Software Position Based FIFO; end of list |

The Processing Pipe control register at `0x0804` read `0x40000000`, with the GPROCEN bit set. Its status register at `0x0808` read zero. DSP `ADSPCS` remained `0x00000f0f`, consistent with the four cores reset, stalled, and powered off. The probe made no device writes. Capability IDs and bit positions follow Linux's [HDA register](https://github.com/torvalds/linux/blob/master/include/sound/hda_register.h) and [Intel SOF HDA](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda.h) definitions.

The package, test certificate, boot test-signing setting, and diagnostic registry values were removed, then the laptop was rebooted. Final verification found no probe service or driver package and no test-mode residue; `pnputil` reported the controller stopped without a driver.
