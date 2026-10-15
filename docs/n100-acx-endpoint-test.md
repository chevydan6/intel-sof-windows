# N100 ACX render endpoint enumeration

The experimental ACX driver on `feature/acx-speaker` built with the Windows WDK and passed INF verification in [run 38406309549](https://github.com/chevydan6/intel-sof-windows/actions/runs/38406309549). During a temporary Windows 11 test-signing boot on the Lenovo 14e Chromebook Gen 3, the `sofplay` MEDIA-class driver bound to the Intel `8086:54C8` controller and started without a PnP error. Windows created an `AudioEndpoint` device named **Speakers (N100 SOF speaker (development))** with status `OK`.

The stream callback currently returns `STATUS_NOT_SUPPORTED`. This verifies endpoint enumeration only; apps cannot play audio through this build. The package, signer trust, and test-signing option were removed, and the 22:40:13 normal-boot rollback check found the controller stopped without a bound driver or remaining test package.

Next, combine the existing proven SOF IPC3/SSP1/DMA speaker path with an ACX real-time render stream. The ACX packet buffers must be mapped for both Windows and the HDA DMA engine, with bounded stream start/stop, packet-completion notifications, and power-state cleanup before app playback can be tested.
