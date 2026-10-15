// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Diagnostic modes use one explicit arm. Trial mode follows the stream lifecycle.
#if defined(SOFPLAY_RIGHT_TRIAL)
#define SOF_RIGHT_PREVIEW_MS 0ULL // Stream lifetime; renewal watchdog remains active.
#elif defined(SOFPLAY_RIGHT_PREVIEW_30)
#define SOF_RIGHT_PREVIEW_MS 30000ULL
#elif defined(SOFPLAY_RIGHT_PREVIEW_LONG)
#define SOF_RIGHT_PREVIEW_MS 8000ULL
#else
#define SOF_RIGHT_PREVIEW_MS 2000ULL
#endif
#define SOF_RIGHT_PREVIEW_GAP_MS 80ULL
#ifdef SOFPLAY_RIGHT_TRIAL
// User-requested trial level: -6 dB (16423/32768), +3 dB over board curve.
#define SOF_RIGHT_PREVIEW_PEAK 1076297727L
#else
#define SOF_RIGHT_PREVIEW_PEAK 536870911L
#endif
typedef struct {
    LONG PreviousInput, PreviousOutput;
} SOF_RIGHT_FILTER;

static __inline LONG SofRightPreviewSample(SOF_RIGHT_FILTER *filter, LONG left, LONG right)
{
    // Mono preserves content from either application channel; the physical
    // left slot is always zero. Widen before summing and scaling.
#ifdef SOFPLAY_RIGHT_TRIAL
    LONG input = (LONG)((((LONGLONG)left + right) * 16423) / 65536);
#else
    LONG input = (LONG)(((LONGLONG)left + right) / 8);
#endif
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
#ifdef SOFPLAY_RIGHT_TRIAL
    BOOLEAN AmpEnabled;
    ULONGLONG LastAudio;
#endif
    ULONGLONG Started, LastCheck, Position;
    NTSTATUS Error;
} SOF_RIGHT_PREVIEW;

static __inline BOOLEAN SofRightPreviewCanContinue(SOF_RIGHT_PREVIEW *s,
                                                   ULONGLONG now,
                                                   ULONGLONG position)
{
    return s->Phase == 3 && now >= s->LastCheck && now >= s->Started &&
#ifndef SOFPLAY_RIGHT_TRIAL
        now - s->Started < SOF_RIGHT_PREVIEW_MS &&
#endif
        now - s->LastCheck < SOF_RIGHT_PREVIEW_GAP_MS && position > s->Position;
}
