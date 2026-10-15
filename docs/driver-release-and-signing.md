# Right-speaker package and Microsoft signing

The verified configuration is right-speaker output at 0 dB, with the physical
left channel muted. Keep this routing when producing future release builds.
The owner reports the current driver works well. Sleep/resume and broader
hardware compatibility remain unqualified.

## Published package

The public v0.1.0-rc.2 prerelease enables BOTH speaker channels at 0 dB.
It preserves stereo input with independent left/right DC-filter histories.
The right-only variant is a local workaround for the owner's laptop and is
not included in the public binary download. That laptop remains on oem70.inf;
the stereo release has not been installed there.

Stereo audio source: 50a1af0, build 38695927767, StereoTrial=true.
Amplifier source: c909fda, build 38664599689, version 1.15.12.359.
WDK compilation, independent INF checks and production-code tests passed.
The tests cover both-channel conversion, channel isolation, split-packet filter
continuity, left-only activity, silence shutoff and amplifier fault handling.
Stereo physical playback and sleep/resume have not been validated on hardware.
The earlier owner feedback applies to the separate right-only build.

The complete source archive accompanies the binary package. These are
WDK test-signed builds, not Microsoft-signed production drivers. The package
is for the reviewed PUJJOTEEN N100 board, not generic Intel N100 hardware.
It includes the GPL-2.0 license and SHA-256 checksums, with no private keys
or machine-specific recordings. The mistaken right-only public package was
withdrawn when the owner clarified the distribution requirements.

A fresh installation additionally needs the exact SOF IPC3 firmware at
C:\Windows\System32\drivers\sof-adl-n-ipc3.ri, SHA-256
8433390e4abfe40b251ff3dec41604e7b817501d1097d51aa2f51c11444d3bf0
(525056 bytes). Third-party firmware is not included. This is a development
package, not an automated clean-install kit. Headphone switching and a Windows
microphone endpoint are not included.

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


## Boot-policy experiment on the owner's laptop

On 2026-10-15 the owner authorized a reboot test with TESTSIGNING=No and
NOINTEGRITYCHECKS=Yes in a separate boot entry. Secure Boot and VBS were
already off. Windows 11 build 26300.9711 still blocked sofplay and sofrt5682;
the audio parent reported Code 52 and Code Integrity event 3004 named the
binaries. NOINTEGRITYCHECKS was therefore insufficient on this machine.

The original boot entry was restored and the trial entry removed. Final
verification showed TESTSIGNING=Yes, no NOINTEGRITYCHECKS setting, and audio,
amplifier, codec and keyboard devices all OK. The installed audio driver
remains the owner's 0 dB right-only version. The public stereo package was
not installed. No kernel patches or integrity-policy removal were attempted.
