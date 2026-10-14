// SPDX-License-Identifier: GPL-2.0-only
#pragma once
enum SOF_RIGHT_STOP_REASON {
    SofRightNotStopped = 0, SofRightDeadline, SofRightLateTick,
    SofRightAdvanceError, SofRightInvalidPcm, SofRightNoProgress,
    SofRightInactive, SofRightAmpStartError, SofRightAmpRenewError,
    SofRightPolicyError, SofRightPause, SofRightAmpStopError
};
// Fixed-width read-only diagnostic ABI, independent of the original status.
typedef struct {
    ULONG Version, Reason, PhaseAtStop, Ticks;
    ULONG ElapsedMs, GapMs, PreviousPositionLow, PreviousPositionHigh;
    ULONG ObservedPositionLow, ObservedPositionHigh, TransportFlags, AdvanceStatus;
} SOF_RIGHT_PREVIEW_DIAGNOSTICS;
