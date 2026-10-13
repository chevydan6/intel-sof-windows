// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <devioctl.h>

// {F6B7AB70-381C-4A67-A85C-4A006A7B01DD}
#define SOFPROBE_INTERFACE_GUID_STRING "{F6B7AB70-381C-4A67-A85C-4A006A7B01DD}"
#define IOCTL_SOFPROBE_READ_SNAPSHOT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SOFPROBE_PULSE_CORE0 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

typedef struct _SOFPROBE_SNAPSHOT {
    ULONG Version;
    ULONG Gctl;
    ULONG Ppctl;
    ULONG Ppsts;
    ULONG Adspcs;
    ULONG Adspis;
    ULONG Hipct;
    ULONG Hipci;
} SOFPROBE_SNAPSHOT, *PSOFPROBE_SNAPSHOT;

typedef struct _SOFPROBE_POWER_RESULT {
    ULONG Version;
    ULONG Before;
    ULONG Requested;
    ULONG Powered;
    ULONG Restored;
    ULONG UpPolls;
    ULONG DownPolls;
    ULONG Result; // 0: success, 1: power-up timeout, 2: power-down timeout
} SOFPROBE_POWER_RESULT, *PSOFPROBE_POWER_RESULT;
