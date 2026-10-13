# Upstream implementation map

The Linux SOF code is a hardware/protocol reference, not a Windows driver that
can be compiled unchanged. The Linux-facing ALSA, ASoC, PCI, DMA, firmware,
interrupt, and power-management calls need Windows equivalents.

| Concern | Linux reference | Windows implementation target |
| --- | --- | --- |
| Alder Lake-N PCI identity, firmware selection | [pci-tgl.c](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/pci-tgl.c), [pci_ids.h](https://github.com/torvalds/linux/blob/master/include/linux/pci_ids.h) | Narrow PnP INF match and KMDF PCI device |
| HDA capability register offsets and bit fields | [hda_register.h](https://github.com/torvalds/linux/blob/master/include/sound/hda_register.h) | Read-only mapping of the HDA resource and fixed register snapshot |
| Controller initialization and DSP boot | [intel/hda.c](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda.c), [intel/hda-loader.c](https://github.com/torvalds/linux/blob/master/sound/soc/sof/intel/hda-loader.c) | PCI resources, MMIO, interrupts, DMA, firmware loader |
| IPC3 protocol and transport | [ipc.c](https://github.com/torvalds/linux/blob/master/sound/soc/sof/ipc.c), [ipc3.c](https://github.com/torvalds/linux/blob/master/sound/soc/sof/ipc3.c) | Serialized command/reply transport with bounded timeouts |
| RT5682 topology and routing | [ADL machine table](https://github.com/torvalds/linux/blob/master/sound/soc/intel/common/soc-acpi-intel-adl-match.c), [sof_rt5682.c](https://github.com/torvalds/linux/blob/master/sound/soc/intel/boards/sof_rt5682.c) | ACPI/I2C codec support and ACX circuits |
| Windows audio abstraction | [Microsoft ACX overview](https://learn.microsoft.com/windows-hardware/drivers/audio/acx-audio-class-extensions-overview) | ACX playback/capture circuits and streams |

The Linux Alder Lake-N descriptor selects `sof-adl-n.ri` and defaults to IPC3,
though the controller supports IPC4 too. Firmware and topology versions must be
matched to the protocol used by the Windows driver. The first probe must report
the actual controller capabilities before DSP initialization is attempted.

Code copied from Linux files must retain attribution and comply with each
file's SPDX license. Microsoft's WDK samples use MS-PL, so their source should
be treated as design examples rather than copied into this GPL tree.
