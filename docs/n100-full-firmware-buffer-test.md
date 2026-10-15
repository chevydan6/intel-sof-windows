# N100 firmware-sized DMA buffer allocation

On 2026-10-13, CI run [38377910454](https://github.com/chevydan6/intel-sof-windows/actions/runs/38377910454) built version 0.8.0.0 and passed independent INF verification. The signed package hashes were checked on the N100 before loading. The opt-in DMA allocation IOCTL returned:

| Field | Value |
| --- | --- |
| BDL logical address | `0x00000001697C8000` |
| Payload logical address | `0x000000017AC00000` |
| BDL allocation | 128 bytes |
| Payload allocation | 524,288 bytes |
| BDL entry size | 16 bytes |

Both device-visible addresses are aligned to 128 bytes. The BDL had two entries, each describing a 262,144-byte half of the payload buffer. The entries were built in memory but were never given to a hardware stream. Both buffers were freed before the IOCTL completed, so these addresses are not reusable. This test establishes that the target can allocate a contiguous DMA buffer large enough for the local IPC3 firmware payload; it does not prove firmware can load or that a DMA stream can run.

The two-entry layout follows the [Intel HDA specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf), which requires at least two BDL entries for an active stream and 128-byte alignment. The next driver stage must retain these buffers for the transfer lifetime, copy the validated firmware payload into the allocation, configure the stream and ROM control, and release everything on success or timeout.

The test driver, diagnostic values, temporary certificate trust, and test-signing boot option were removed. After the 18:43:14 reboot, verification found the controller stopped with no driver bound, no probe package or service, no diagnostic values, no certificate trust, and test signing disabled.
