// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// LONG and LONGLONG come from the WDK; do not include user-mode CRT headers.

// PUJJOTEEN cras-config.max98357/sof-rt5682.card_settings specifies
// db_at_100 = -900 (hundredths of a dB). See docs/n100-speaker-gain.md.
// Apply the board's -9 dB ceiling after Windows' user/session volume.
#define SOFPLAY_SPEAKER_ATTENUATION_CENTIDB 900UL

static __inline LONG SofPlayAttenuateSpeakerSample(LONG sample)
{
    // 11626 / 32768 = 0.35479736, approximately -9.00039 dB.
    // Widen before multiplication; even INT32_MIN cannot overflow.
    // Division avoids implementation-defined right shifts of negatives.
    return (LONG)(((LONGLONG)sample * 11626) / 32768);
}
