// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Include after WAVEFORMATEXTENSIBLE/GUID definitions. The packet copy path
// interprets every sample as signed S32; float has the same block alignment
// and must never be accepted merely because its rate/channels match.
static __inline int
SofPlaySupportsFormat(const WAVEFORMATEXTENSIBLE *wave)
{
    static const GUID pcm = {
        0x00000001, 0x0000, 0x0010,
        {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}
    };
    if (wave == NULL || wave->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        wave->Format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return 0;
    return wave->Format.nChannels == 2 &&
        wave->Format.nSamplesPerSec == 48000 &&
        wave->Format.nAvgBytesPerSec == 384000 &&
        wave->Format.nBlockAlign == 8 && wave->Format.wBitsPerSample == 32 &&
        (wave->Samples.wValidBitsPerSample == 24 ||
         wave->Samples.wValidBitsPerSample == 32) &&
        wave->dwChannelMask == KSAUDIO_SPEAKER_STEREO &&
        RtlEqualMemory(&wave->SubFormat, &pcm, sizeof(pcm));
}
