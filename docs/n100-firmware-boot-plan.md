# N100 firmware boot path

The Windows probe has bound to `8086:54c8`, read the HDA and DSP register map, exposed a working user-mode IOCTL, and demonstrated a reversible power request for DSP core 0. The core stayed reset and stalled during that test. No firmware has run yet.

Linux associates Alder Lake-N with `tgl_chip_info`: four DSP cores, only core 0 host-managed, and IPC3 as the default. Its cold boot path is:

1. Load and validate `sof-adl-n.ri`. For IPC3, skip the extended `XMan` manifest and place only the remaining payload in the code-loader DMA buffer.
2. Configure an HDA playback stream with a stream tag, format, 128-byte-aligned buffer-descriptor list, and DMA buffers for the firmware payload. The Intel HDA specification requires at least two descriptor entries before the stream can run. Configure the capture stream used for ICCMAX by this platform's Linux path.
3. Power core 0 and send the ROM-control IPC request, including the code-loader stream tag and purge bit.
4. Clear core 0 reset and stall, wait for the ROM's IPC acknowledgment, then wait for ROM status at DSP BAR + `0x80000` to report initialization done.
5. Start the code-loader DMA stream and wait for firmware-entered status. Stop DMA and release boot-only streams on every exit path.
6. Only after firmware boot, add interrupt handling, IPC3 command/reply exchange, topology loading, audio streams, and the RT5682 codec path.

The local Ubuntu IPC3 image at `/usr/lib/firmware/intel/sof/intel-signed/sof-adl-n.ri.zst` expands to 525,056 bytes. `tools/inspect-ipc3-firmware.py` validated a 768-byte `XMan` extended manifest with seven bounded elements and a 524,288-byte DMA payload. The payload SHA-256 is `388cf4656a6a8bd1611b32480cf72cdc1fa94eb282addaafbc4ed85c6ab21795`. The IPC4 image has a different `$AE1` header; it is not an IPC3 payload and must not be sent through this path. Neither firmware image is bundled in this repository.

A [Windows DMA allocation test](n100-dma-allocation-test.md) produced aligned logical addresses and a 16-byte HDA descriptor. A follow-up [full-payload test](n100-full-firmware-buffer-test.md) allocated the complete 512 KiB buffer and built two descriptor entries. The [IPC3 staging test](n100-ipc3-staging-test.md) now retains a 512 KiB payload and two descriptor entries until PnP removal. A [read-only stream-0 snapshot](n100-stream0-snapshot.md) found the first output stream idle at HDA BAR + `0x160` with control `0x040000`; that value must be preserved. The next milestone is configuring its tag, format, and descriptor pointer, with no core release until setup and cleanup are verified. A ROM-entry test should then report both IPC acknowledgment and ROM status, even if the later firmware transfer fails. The power-control command alone cannot start the ROM because Linux sends ROM control before releasing reset/stall and has already prepared the DMA stream.

Source references: [Alder Lake-N descriptor](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/pci-tgl.c), [core/IPC description](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/tgl.c), [boot ordering](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-loader.c), [register and status definitions](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda.h), [IPC3 manifest parser](https://github.com/torvalds/linux/blob/master/sound/soc/sof/ipc3-loader.c).
