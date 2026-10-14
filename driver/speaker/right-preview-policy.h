// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// One explicitly armed application stream. No automatic retry or rearming.
#if defined(SOFPLAY_RIGHT_PREVIEW_30)
#define SOF_RIGHT_PREVIEW_MS 30000ULL
#elif defined(SOFPLAY_RIGHT_PREVIEW_LONG)
#define SOF_RIGHT_PREVIEW_MS 8000ULL
#else
#define SOF_RIGHT_PREVIEW_MS 2000ULL
#endif
#define SOF_RIGHT_PREVIEW_GAP_MS 80ULL
#define SOF_RIGHT_PREVIEW_PEAK 536870911L
typedef struct {
    LONG PreviousInput, PreviousOutput;
} SOF_RIGHT_FILTER;

static __inline LONG SofRightPreviewSample(SOF_RIGHT_FILTER *filter, LONG left, LONG right)
{
    // Mono preserves content from either application channel; the physical
    // left slot is always zero. Widen before summing, then attenuate by 12 dB.
    LONG input = (LONG)(((LONGLONG)left + right) / 8);
    LONGLONG output = (LONGLONG)input - filter->PreviousInput +
        ((LONGLONG)filter->PreviousOutput * 32640) / 32768;
    // First-order DC blocker (~30 Hz at 48 kHz), with a fixed peak ceiling.
    if (output > SOF_RIGHT_PREVIEW_PEAK) output = SOF_RIGHT_PREVIEW_PEAK;
    if (output < -SOF_RIGHT_PREVIEW_PEAK) output = -SOF_RIGHT_PREVIEW_PEAK;
    filter->PreviousInput = input;
    filter->PreviousOutput = (LONG)output;
    return (LONG)output;
}

typedef struct {
    ULONG Phase; // 0 unarmed, 1 armed, 2 waiting for DMA, 3 active, 4 finished
    BOOLEAN Powered;
    ULONGLONG Started, LastCheck, Position;
    NTSTATUS Error;
} SOF_RIGHT_PREVIEW;

static __inline BOOLEAN SofRightPreviewCanContinue(SOF_RIGHT_PREVIEW *s,
                                                   ULONGLONG now,
                                                   ULONGLONG position)
{
    return s->Phase == 3 && now >= s->LastCheck && now >= s->Started &&
        now - s->Started < SOF_RIGHT_PREVIEW_MS &&
        now - s->LastCheck < SOF_RIGHT_PREVIEW_GAP_MS && position > s->Position;
}
