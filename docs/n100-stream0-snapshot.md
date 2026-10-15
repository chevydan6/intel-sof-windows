# N100 first output-stream baseline

On 2026-10-13, CI run [38379768219](https://github.com/chevydan6/intel-sof-windows/actions/runs/38379768219) built version 1.0.0.0 and passed independent INF verification. After package hash and signer checks, the read-only driver captured the first HDA output stream. `GCAP=0x9701` gives seven input streams, so output stream 0 starts at HDA BAR + `0x160`.

| Register | Observed value |
| --- | --- |
| Stream control, low 24 bits | `0x040000` |
| Stream status | `0x00` |
| Cyclic buffer length | `0` |
| Last valid index | `0` |
| Format | `0x0000` |
| BDL address | `0x0000000000000000` |
| Processing-pipe control | `0x40000000` |

The stream RUN and reset bits are clear; the stream is idle. The existing control value `0x040000` must be preserved when assigning a stream tag. The processing-pipe enable bit for stream index 7 is clear. These values provide exact preconditions for a future guarded stream-setup test, rather than assuming the whole control register starts at zero.

The [Intel HDA specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf) defines the stream register layout, and the [Linux SOF stream code](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-stream.c) shows the reset, BDL, tag, format, and processing-pipe sequence. No stream register was written during this test.

The test driver, diagnostic values, temporary certificate trust, and test-signing boot option were removed. After the 18:59:18 reboot, verification found the controller stopped with no driver bound, no probe package or service, no diagnostic values, no certificate trust, and test signing disabled.
