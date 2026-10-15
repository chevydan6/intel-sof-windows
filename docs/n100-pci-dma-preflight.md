# N100 PCI bus-master preflight

On 2026-10-13, CI run [38383597548](https://github.com/chevydan6/intel-sof-windows/actions/runs/38383597548) built a read-only PCI configuration probe and passed separate INF verification. Using Windows' `BUS_INTERFACE_STANDARD`, the bound driver read the first 16 bytes of PCI configuration space through the parent bus driver.

| Field | Observed value |
| --- | --- |
| Vendor/device | `0x54C88086` (`8086:54c8`) |
| PCI command | `0x0006` |
| PCI command/status | `0x00100006` |
| Revision/class | `0x04010000` |
| Header information | `0x00004010` |

Command bits 1 and 2 show that Windows already enabled memory-space access and bus mastering while the probe was bound. No PCI configuration bit was changed. This removes one prerequisite for a bounded DMA test; it does not prove that a DMA transaction succeeds.

The driver package and temporary certificate trust were removed. After the 19:31:45 reboot, verification found no bound probe, service, package, diagnostic values, certificate trust, or test-signing option. The controller was stopped and driverless.

Windows requires the bus interface or configuration IRPs for PCI configuration reads and reserves writes to the standard configuration header for the operating system: [Microsoft PCI configuration guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/pci/accessing-pci-device-configuration-space). The common-buffer logical address is the address the device should use for DMA: [Microsoft WDF common-buffer guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-common-buffers).
