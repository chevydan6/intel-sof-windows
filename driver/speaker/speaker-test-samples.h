// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Fixed 1 kHz S32 test, -42.14 dBFS peak; exactly periodic in the 6144-frame ring.
// The other speaker channel is exactly zero. No arbitrary level/frequency input.
static VOID SofSpeakerFillTestSamples(LONG *samples, ULONG frames, ULONG channel)
{
    static const LONG sine[48] = {0,1094933,2171132,3210181,4194304,5106661,5931642,6655130,7264748,7750063,8102773,8316842,8388608,8316842,8102773,7750063,7264748,6655130,5931642,5106661,4194304,3210181,2171132,1094933,0,-1094933,-2171132,-3210181,-4194304,-5106661,-5931642,-6655130,-7264748,-7750063,-8102773,-8316842,-8388608,-8316842,-8102773,-7750063,-7264748,-6655130,-5931642,-5106661,-4194304,-3210181,-2171132,-1094933};
    ULONG i;
    for (i=0;i<frames;++i) {
        samples[2*i] = channel == 0 ? 2 * sine[i % 48] : 0;
        samples[2*i+1] = channel == 1 ? 2 * sine[i % 48] : 0;
    }
}

// Validate the host ring after firmware preparation and immediately before EN.
// This establishes host-memory contents only, not the signal at the SSP pins.
static BOOLEAN SofSpeakerTestSamplesValid(const LONG *samples, ULONG frames, ULONG channel)
{
    LONG reference[96];
    ULONG i;
    if (samples == NULL || frames != 6144 || channel > 1) return FALSE;
    SofSpeakerFillTestSamples(reference, 48, channel);
    for (i = 0; i < frames * 2; ++i)
        if (samples[i] != reference[i % 96]) return FALSE;
    return TRUE;
}
