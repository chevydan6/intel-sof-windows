# N100 IPC3 firmware staging

On 2026-10-13, CI run [38378731177](https://github.com/chevydan6/windows-sof/actions/runs/38378731177) built version 0.9.0.0 and passed independent INF verification. The package hashes and transient test signer were checked on the N100. The local Alder Lake-N IPC3 image was decompressed, and the Windows client checked the entire image SHA-256 (`8433390e4abfe40b251ff3dec41604e7b817501d1097d51aa2f51c11444d3bf0`) before sending it to the driver.

The driver accepted the exact 525,056-byte image, checked its `XMan` header and 768-byte manifest boundary, allocated retained DMA buffers, copied the 524,288-byte payload after the manifest, and built two 262,144-byte HDA buffer descriptors. The upload returned:

| Field | Value |
| --- | --- |
| BDL logical address | `0x00000001788A1000` |
| Payload logical address | `0x000000017FB73000` |
| BDL allocation | 128 bytes |
| Payload allocation | 524,288 bytes |
| BDL entry size | 16 bytes |

A second upload to the same driver instance was rejected as busy. A live read-only snapshot after staging still returned `ADSPCS=0x00000F0F`, `PPCTL=0x40000000`, and zero `PPSTS`, `ADSPIS`, `HIPCT`, and `HIPCI`. No ROM command, stream configuration, DMA transfer, or firmware execution occurred. The client currently checks the full-image hash; the kernel checks exact size and header fields, but does not independently authenticate the payload. The ROM's firmware verification is not tested yet.

The staged buffers are released by the driver's hardware-release callback during PnP removal. The package, diagnostic values, temporary certificate trust, and test-signing boot option were removed. After the 18:50:54 reboot, verification found the controller stopped with no driver bound, no probe package or service, no diagnostic values, no certificate trust, and test signing disabled.
