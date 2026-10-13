# N100 guarded code-loader stream setup

On 2026-10-13, CI run [38380889261](https://github.com/chevydan6/windows-sof/actions/runs/38380889261) built the Windows probe and passed separate INF verification. On the N100 target, a test-signed package bound to `8086:54c8`. An exact idle-register check and a previously staged, locally verified IPC3 firmware image were required before the one-shot stream setup IOCTL could write any registers.

The probe reset output stream 0, waited for reset acknowledgment and clearance, configured the code-loader parameters, read them back, and restored the original registers before returning. It never set RUN, released a DSP core, or requested firmware execution.

| Register or result | Configured | Restored |
| --- | --- | --- |
| Stream control | `0x140000` | `0x040000` |
| Cyclic buffer length | `524288` | `0` |
| Last valid index | `1` | `0` |
| Format | `0x0040` | `0x0000` |
| BDL address | `0x00000001747EE000` | `0` |
| Processing-pipe control | `0x40000080` | `0x40000000` |
| Reset acknowledgment and result | `true`, `0` | — |

An independent read-only snapshot after the IOCTL found the same original values, including status `0` and both BDL address halves zero. The full 512 KiB firmware payload remained in memory but was not fetched by hardware. This proves the stream accepts the setup and can be restored; it does not prove that DMA or firmware boot works.

The package was uninstalled and deleted. After the 19:08:53 reboot, verification found no bound driver, probe service, package, diagnostic values, test-certificate trust, or test-signing boot option. The controller returned to its stopped, driverless state.

The next milestone is a separately guarded ROM-entry test. It must implement the Linux boot ordering, capture IPC and ROM status, and restore the controller even on timeout.
