# Read-only PCI probe

`sofprobe.c` is the first kernel driver source. It asks KMDF for the PCI
resources Windows assigned to the `8086:54c8` controller and prints their
types and addresses to the kernel debugger. It does not map or touch those
resources and exposes no audio endpoint.

The WDK project and INF are prepared but have not been compiled or validated
with the Windows Driver Kit yet. The INF matches the N100 controller's exact
PCI device and subsystem IDs. It needs WDK build/INF verification, test signing,
and a recovery procedure before installation on the laptop. A successful first test is a
clean bind, a resource report, and a clean uninstall with no audio regression.

The implementation follows the KMDF resource enumeration API described in
[Microsoft's hardware resource guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/finding-and-mapping-hardware-resources).
