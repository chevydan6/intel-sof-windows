# N100 on-demand snapshot test

On 2026-10-13, CI run [38374898453](https://github.com/chevydan6/windows-sof/actions/runs/38374898453) built version 0.5.0.0 and independently passed `InfVerif /w`. The package files were checked by SHA-256 on the N100 before installation. The package was signed with a transient WDK test certificate; Secure Boot and BitLocker were off, and a BCD backup was taken before enabling test signing.

The driver bound to `PCI\VEN_8086&DEV_54C8&SUBSYS_00000000&REV_00\3&11583659&0&FB`, reported `Started`, and exposed one instance of the project's device interface. `tools/read-live-snapshot.ps1` opened that interface and called the read-only IOCTL successfully. The response was:

| Register | Value |
| --- | --- |
| GCTL | `0x00000001` |
| PPCTL | `0x40000000` |
| PPSTS | `0x00000000` |
| ADSPCS | `0x00000F0F` |
| ADSPIS | `0x00000000` |
| HIPCT | `0x00000000` |
| HIPCI | `0x00000000` |

This matched the earlier boot-time registry snapshot. The IOCTL maps fixed HDA/DSP offsets read-only, returns a versioned 32-byte response, then unmaps both ranges. It does not power the DSP, load firmware, set up DMA, or access the codec.

After the reading, `pnputil` removed the package, the registry diagnostics were removed, the transient certificate was removed from Root and TrustedPublisher, and the test-signing BCD value was deleted. After the 18:18:52 reboot, rollback verification found the controller stopped with no bound driver, no probe package or service, no diagnostic values, no certificate trust, and test signing disabled.
