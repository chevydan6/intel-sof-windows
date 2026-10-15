# N100 Windows DMA allocation check

On 2026-10-13, CI run [38377214934](https://github.com/chevydan6/intel-sof-windows/actions/runs/38377214934) built version 0.7.0.0 of the development probe and passed independent INF verification. The package hashes and transient test signer were checked on the N100. The driver started on PCI `8086:54c8` and the `-TestDmaAllocation` IOCTL returned:

| Field | Result |
| --- | --- |
| BDL logical address | `0x000000016AA83000` |
| Payload logical address | `0x00000001446FF000` |
| BDL allocation | 128 bytes |
| Payload allocation | 4096 bytes |
| BDL entry size | 16 bytes |

Both device-visible addresses were aligned to 128 bytes. The driver used a WDF 64-bit scatter/gather DMA enabler, built a single HDA buffer-descriptor entry referring to the payload allocation, and freed both common buffers before completing the request. These addresses are evidence of successful DMA allocation, not persistent resources. No stream register was programmed and the controller performed no DMA transfer. A full firmware loader still needs a lifetime-managed firmware buffer, a descriptor list for its 512 KiB payload, stream setup, ROM control, interrupts or bounded polling, and cleanup on every path.

The [Intel HDA specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf) requires 128-byte BDL alignment. [Microsoft's WDF common-buffer guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-common-buffers) describes the allocation and device-visible logical addresses used here.

After the test, the driver package, diagnostic values, transient certificate trust, and test-signing BCD value were removed. After the 18:37:32 reboot, the rollback script verified the controller was stopped with no driver bound, no probe package or service, no diagnostic values, no certificate trust, and test signing disabled.
