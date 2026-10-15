# N100 extended capability and DSP status test (2026-10-13)

The read-only probe from [CI run 38372426287](https://github.com/chevydan6/intel-sof-windows/actions/runs/38372426287) built with the Windows Driver Kit and passed x64 `InfVerif /w`. Its three package files matched the CI hashes on the N100. The package was test-signed, loaded with test signing enabled, and removed afterward using the same controlled procedure as the [previous register test](n100-register-test.md).

The driver read the HDA resource and the first `0x100` bytes of the Intel DSP resource. It made no hardware writes. The HDA values from the prior test were unchanged. New values were:

| Register | Resource offset | Value | Meaning |
| --- | ---: | ---: | --- |
| Extended capability header | HDA `0x0c00` | `0x00020800` | Capability ID 2 (Multi-Link); next header at `0x0800` |
| ADSPCS | DSP `0x0004` | `0x00000f0f` | Four cores have reset and stall bits set; requested and current power bits clear |
| ADSPIS | DSP `0x000c` | `0x00000000` | No DSP interrupt status bits set at read time |
| HIPCT | DSP `0x0040` | `0x00000000` | No pending DSP-to-host IPC message observed |
| HIPCI | DSP `0x0048` | `0x00000000` | No pending host-to-DSP IPC message observed |

The capability header format and DSP register definitions come from Linux [HDA registers](https://github.com/torvalds/linux/blob/master/include/sound/hda_register.h) and [Intel SOF HDA definitions](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda.h). These readings establish the power-off baseline. They do not demonstrate that DSP power-up, firmware loading, or IPC works.

After the test, the probe package and service were removed, test-certificate trust and test signing were removed, diagnostic registry values were deleted, and Windows was rebooted. Verification showed `pnputil` reporting the controller stopped with no driver, and no remaining probe package, service, certificate trust, test-signing setting, or diagnostic values.
