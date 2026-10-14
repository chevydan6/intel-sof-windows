// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Query/stop are administrator diagnostics. Start/renew require KernelMode.
// This revision permits only a short diagnostic lease, not continuous playback.
static const GUID SofAmpInterfaceGuid =
    {0x1d4a273b,0x20e6,0x49eb,{0xb7,0xca,0x19,0x36,0xea,0x22,0xc7,0x54}};
#define IOCTL_SOFAMP_QUERY CTL_CODE(FILE_DEVICE_UNKNOWN,0x940,METHOD_BUFFERED,FILE_READ_ACCESS)
#define IOCTL_SOFAMP_STOP CTL_CODE(FILE_DEVICE_UNKNOWN,0x941,METHOD_BUFFERED,FILE_WRITE_ACCESS)
#define IOCTL_SOFAMP_START CTL_CODE(FILE_DEVICE_UNKNOWN,0x942,METHOD_BUFFERED,FILE_WRITE_ACCESS)
#define IOCTL_SOFAMP_RENEW CTL_CODE(FILE_DEVICE_UNKNOWN,0x943,METHOD_BUFFERED,FILE_WRITE_ACCESS)
typedef struct {
    ULONG Version, Ready, Active, Faulted, OutputRequest;
    ULONG OnWrites, OffWrites, WatchdogTrips, FaultStatus, CleanupStatus;
    ULONG LeaseMs, MaximumMs;
} SOFAMP_STATUS;
