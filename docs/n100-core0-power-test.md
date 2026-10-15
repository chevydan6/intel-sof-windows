# N100 core-0 power acknowledgment

On 2026-10-13, CI run [38375759572](https://github.com/chevydan6/intel-sof-windows/actions/runs/38375759572) built the guarded probe (version 0.6.0.0) and passed independent INF verification. The package hashes and transient test signer were checked on the N100. The controller started with the same read-only baseline as before: `GCTL=1`, `PPCTL=0x40000000`, `ADSPCS=0x00000F0F`, and zero `PPSTS`, `ADSPIS`, `HIPCT`, and `HIPCI`.

The opt-in power IOCTL required that exact baseline and used a sequential, passive-level queue. It changed only core 0's `SPA` bit, never released reset or stall, polled the hardware `CPA` acknowledgment for a bounded number of reads, and always cleared `SPA`. The result was:

| Step | ADSPCS |
| --- | --- |
| Before | `0x00000F0F` |
| SPA requested | `0x00010F0F` |
| CPA acknowledged | `0x01010F0F` |
| SPA cleared, CPA off | `0x00000F0F` |

The driver reported `UpPolls=1`, `DownPolls=0`, and `Result=0`. A second pulse in the same driver instance returned `STATUS_DEVICE_BUSY` (reported to the client as resource in use). A subsequent read-only snapshot again returned `ADSPCS=0x00000F0F` and the original values for all other sampled registers. This proves that Windows can power core 0 through the SOF controller and read the response; it does **not** show that firmware has booted or that audio works.

The sequence follows the core-0 host-managed mask and ADSPCS field definitions in the [Linux SOF Tiger Lake/Alder Lake description](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/tgl.c), [Intel HDA/SOF register definitions](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda.h), and [core power sequencing](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-dsp.c). The Windows test intentionally left reset and stall asserted and did not load firmware or enable interrupts.

After the test, the driver package, diagnostic values, transient certificate trust, and test-signing BCD value were removed. After the 18:26:00 reboot, the rollback script verified the controller was stopped with no driver bound, no probe package or service, no diagnostic values, no certificate trust, and test signing disabled.
