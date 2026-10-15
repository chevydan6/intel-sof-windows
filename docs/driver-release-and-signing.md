# Right-speaker package and Microsoft signing

The verified configuration is right-speaker output at 0 dB, with the physical
left channel muted. Keep this routing when producing future release builds.
The owner reports the current driver works well. Sleep/resume and broader
hardware compatibility remain unqualified.

## Published package

The v0.1.0-rc.1 prerelease preserves the exact installed audio and amplifier
packages, exported through PnPUtil. The audio source is 8a3bbfe (build
38695073543); amplifier source is c909fda (build 38664599689).
The matching complete source archive accompanies the binary package.
These binaries are test-signed WDK builds, not Microsoft-signed production
drivers. The package is for the reviewed PUJJOTEEN N100 board and is not a
generic Intel N100 audio driver.

Audio package: sofplay, version 6.52.50.105, installed as oem70.inf.
Amplifier package: sofamp, version 1.15.12.359, installed as oem65.inf.
The release includes the GPL-2.0 license and SHA-256 checksums. It includes
no private keys or machine-specific diagnostic recordings.

The current laptop already has both packages installed and needs no update.
A fresh installation additionally needs the exact SOF IPC3 firmware at
C:\Windows\System32\drivers\sof-adl-n-ipc3.ri, SHA-256
8433390e4abfe40b251ff3dec41604e7b817501d1097d51aa2f51c11444d3bf0
(525056 bytes). Third-party firmware is not included in this release.
This is a preservation/update package, not an automated clean-install kit.
Headphone switching and a Windows microphone endpoint are not included.

## Exit Windows test mode

The current packages use self-issued WDK test certificates. Trusting those
certificates locally does not replace Microsoft kernel-mode signing. Turning
TESTSIGNING off would prevent these drivers loading on a normal boot.
The laptop therefore retains TESTSIGNING=Yes to preserve working audio.

1. Register and verify a Windows Hardware Developer Program account in
   Partner Center. Associate an EV code-signing certificate with the account.
2. Prepare and review both the audio and amplifier driver packages for
   submission, including Release builds and their INF/catalog files. The
   current public package is a Debug/test-signed build, not submission-ready.
3. For trusted testing on Windows desktop, request attestation signing. For
   Windows certification and retail Windows Update distribution, complete
   the applicable HLK tests and submit through WHCP. Attestation signing
   establishes trust, not compatibility certification.
4. Download the Microsoft-signed packages, verify their signatures under
   kernel signing policy, install them and verify both devices and playback.
5. Check other active development drivers (including the separate RT5682
   codec) before disabling test signing. Sign or replace any still required.
6. Once required drivers satisfy normal signing policy, run elevated
   `bcdedit /set testsigning off`, reboot, then verify the boot setting,
   device status and playback.

Microsoft references:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings
- https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option
