// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <devioctl.h>

// {F6B7AB70-381C-4A67-A85C-4A006A7B01DD}
#define SOFPROBE_INTERFACE_GUID_STRING "{F6B7AB70-381C-4A67-A85C-4A006A7B01DD}"
#define IOCTL_SOFPROBE_READ_SNAPSHOT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SOFPROBE_PULSE_CORE0 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_TEST_DMA_ALLOCATION \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SOFPROBE_STAGE_IPC3_FIRMWARE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_READ_STREAM0 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SOFPROBE_TEST_STREAM_SETUP \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_READ_BOOT_PREFLIGHT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SOFPROBE_TEST_CAPTURE_SETUP \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_READ_PCI_CONFIG \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SOFPROBE_TEST_CAPTURE_DMA \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_WRITE_ACCESS)

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

typedef struct _SOFPROBE_DMA_ALLOCATION_RESULT {
    ULONG Version;
    ULONG BdlAddressLow;
    ULONG BdlAddressHigh;
    ULONG PayloadAddressLow;
    ULONG PayloadAddressHigh;
    ULONG BdlBytes;
    ULONG PayloadBytes;
    ULONG BdlEntryBytes;
} SOFPROBE_DMA_ALLOCATION_RESULT, *PSOFPROBE_DMA_ALLOCATION_RESULT;

typedef struct _SOFPROBE_STREAM_SNAPSHOT {
    ULONG Control;
    ULONG Status;
    ULONG CyclicBufferLength;
    ULONG LastValidIndex;
    ULONG Format;
    ULONG BdlAddressLow;
    ULONG BdlAddressHigh;
    ULONG ProcessingPipeControl;
} SOFPROBE_STREAM_SNAPSHOT, *PSOFPROBE_STREAM_SNAPSHOT;

typedef struct _SOFPROBE_STREAM_SETUP_RESULT {
    ULONG Version;
    ULONG BeforeControl;
    ULONG ResetAcknowledged;
    ULONG ConfiguredControl;
    ULONG ConfiguredCbl;
    ULONG ConfiguredLvi;
    ULONG ConfiguredFormat;
    ULONG ConfiguredBdlLow;
    ULONG ConfiguredBdlHigh;
    ULONG ConfiguredPpctl;
    ULONG RestoredControl;
    ULONG RestoredCbl;
    ULONG RestoredLvi;
    ULONG RestoredFormat;
    ULONG RestoredBdlLow;
    ULONG Result; // 0: success, 1: reset failure, 2: setup mismatch, 3: restore mismatch
} SOFPROBE_STREAM_SETUP_RESULT, *PSOFPROBE_STREAM_SETUP_RESULT;

typedef struct _SOFPROBE_BOOT_PREFLIGHT {
    ULONG Version;
    ULONG Gcap;
    ULONG CaptureControlAndStatus;
    ULONG CaptureCbl;
    ULONG CaptureLvi;
    ULONG CaptureFormat;
    ULONG CaptureBdlLow;
    ULONG CaptureBdlHigh;
    ULONG ProcessingPipeControl;
    ULONG Ltrp;
    ULONG Adspcs;
    ULONG Hipcidr;
    ULONG Hipcida;
    ULONG Hipcctl;
    ULONG Adspic;
    ULONG Adspis;
} SOFPROBE_BOOT_PREFLIGHT, *PSOFPROBE_BOOT_PREFLIGHT;

typedef struct _SOFPROBE_PCI_SNAPSHOT {
    ULONG Version;
    ULONG VendorDevice;
    ULONG CommandStatus;
    ULONG RevisionClass;
    ULONG HeaderInfo;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} SOFPROBE_PCI_SNAPSHOT, *PSOFPROBE_PCI_SNAPSHOT;

typedef struct _SOFPROBE_CAPTURE_DMA_RESULT {
    ULONG Version;
    ULONG BeforeControl;
    ULONG ConfiguredControl;
    ULONG RunningControl;
    ULONG StoppedControl;
    ULONG Position;
    ULONG CaptureStatus;
    ULONG ConfiguredPpctl;
    ULONG RestoredControl;
    ULONG RestoredPpctl;
    ULONG RestoredCbl;
    ULONG RestoredLvi;
    ULONG RestoredFormat;
    ULONG RestoredBdlLow;
    ULONG RestoredBdlHigh;
    ULONG RestoredLtrp;
    ULONG Adspcs;
    ULONG PciCommand;
    ULONG StopPolls;
    ULONG Result;
} SOFPROBE_CAPTURE_DMA_RESULT, *PSOFPROBE_CAPTURE_DMA_RESULT;
