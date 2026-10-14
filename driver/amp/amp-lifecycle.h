// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Serialized by the driver lock. Times are monotonic milliseconds.
// These are software deadlines, not hardware-enforced energy/thermal limits.
#define SOFAMP_LEASE_MS 100UL
#define SOFAMP_MAXIMUM_MS 250UL
#define SOFAMP_LISTEN_MAXIMUM_MS 2200UL
#define SOFAMP_START_DELAY_MS 5UL
#define SOFAMP_OUTPUT_UNKNOWN 0UL
#define SOFAMP_OUTPUT_OFF 1UL
#define SOFAMP_OUTPUT_ON 2UL

typedef struct {
    BOOLEAN Ready, Active, Faulted;
    ULONG OutputRequest, OnWrites, OffWrites, WatchdogTrips;
    NTSTATUS FaultStatus, CleanupStatus;
    PVOID Owner;
    ULONGLONG Deadline, MaximumDeadline;
} SOFAMP_STATE;

// Implemented by the GPIO adapter; unit tests inject transfer failures here.
static NTSTATUS SofAmpDriveRaw(PVOID io, UCHAR value);
static ULONGLONG SofAmpNowMs(VOID);

static NTSTATUS SofAmpStateOff(SOFAMP_STATE *s, PVOID io)
{
    NTSTATUS status;
    s->Active = FALSE;
    s->Owner = NULL;
    s->Deadline = s->MaximumDeadline = 0;
    s->OutputRequest = SOFAMP_OUTPUT_UNKNOWN;
    status = SofAmpDriveRaw(io, 0);
    s->CleanupStatus = status;
    if (NT_SUCCESS(status)) {
        s->OutputRequest = SOFAMP_OUTPUT_OFF;
        ++s->OffWrites;
    } else {
        s->Faulted = TRUE;
        if (NT_SUCCESS(s->FaultStatus)) s->FaultStatus = status;
    }
    return status;
}

static NTSTATUS SofAmpStateExpired(SOFAMP_STATE *s, PVOID io)
{
    if (!s->Active || SofAmpNowMs() < s->Deadline) return STATUS_SUCCESS;
    ++s->WatchdogTrips;
    s->Faulted = TRUE;
    s->FaultStatus = STATUS_IO_TIMEOUT;
    (VOID)SofAmpStateOff(s, io);
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS SofAmpStateStartBounded(SOFAMP_STATE *s, PVOID io, PVOID owner, ULONG maximumMs)
{
    NTSTATUS status;
    ULONGLONG now;
    if (maximumMs != SOFAMP_MAXIMUM_MS && maximumMs != SOFAMP_LISTEN_MAXIMUM_MS)
        return STATUS_INVALID_PARAMETER;
    if (!s->Ready || s->Faulted) return STATUS_DEVICE_NOT_READY;
    if (owner == NULL) return STATUS_INVALID_PARAMETER;
    if (s->Active) {
        status = SofAmpStateExpired(s, io);
        return NT_SUCCESS(status) ? STATUS_DEVICE_BUSY : status;
    }
    if (s->OutputRequest != SOFAMP_OUTPUT_OFF) return STATUS_DEVICE_NOT_READY;
    now = SofAmpNowMs();
    s->Owner = owner;
    s->Deadline = now + SOFAMP_LEASE_MS;
    s->MaximumDeadline = now + maximumMs;
    s->OutputRequest = SOFAMP_OUTPUT_UNKNOWN;
    status = SofAmpDriveRaw(io, 1);
    if (!NT_SUCCESS(status)) {
        s->Faulted = TRUE;
        s->FaultStatus = status;
        (VOID)SofAmpStateOff(s, io); // Failed enable may still have changed EN.
        return status;
    }
    s->OutputRequest = SOFAMP_OUTPUT_ON;
    ++s->OnWrites;
    s->Active = TRUE;
    // A slow GPIO completion must not buy a fresh lease.
    return SofAmpStateExpired(s, io);
}

static NTSTATUS SofAmpStateStart(SOFAMP_STATE *s, PVOID io, PVOID owner)
{
    return SofAmpStateStartBounded(s, io, owner, SOFAMP_MAXIMUM_MS);
}

static NTSTATUS SofAmpStateRenew(SOFAMP_STATE *s, PVOID io, PVOID owner)
{
    NTSTATUS status;
    ULONGLONG deadline;
    if (!s->Ready || s->Faulted || !s->Active) return STATUS_DEVICE_NOT_READY;
    // Check expiration before ownership so stale requests cannot revive output.
    status = SofAmpStateExpired(s, io);
    if (!NT_SUCCESS(status)) return status;
    if (owner == NULL || s->Owner != owner) return STATUS_ACCESS_DENIED;
    deadline = SofAmpNowMs() + SOFAMP_LEASE_MS;
    s->Deadline = deadline < s->MaximumDeadline ? deadline : s->MaximumDeadline;
    return SofAmpStateExpired(s, io);
}

static VOID SofAmpStateFileCleanup(SOFAMP_STATE *s, PVOID io, PVOID owner)
{
    if (owner != NULL && s->Owner == owner) (VOID)SofAmpStateOff(s, io);
}
