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
#define IOCTL_SOFPROBE_TEST_ROM_ENTRY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80a, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_TEST_FIRMWARE_DMA \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80b, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_TEST_IPC3_READY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80c, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_READ_IPC3_INFO \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80d, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_ACK_IPC3_READY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80e, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_QUERY_IPC3_MEMORY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80f, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_START_SESSION \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_SESSION_MEMORY \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_STOP_SESSION \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SOFPROBE_SESSION_EXCHANGE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x813, METHOD_BUFFERED, FILE_WRITE_ACCESS)

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

typedef struct _SOFPROBE_ROM_ENTRY_RESULT {
    ULONG Version;
    ULONG BeforeAdspcs;
    ULONG PoweredAdspcs;
    ULONG ResetReleasedAdspcs;
    ULONG RunningAdspcs;
    ULONG RomControl;
    ULONG IpcAck;
    ULONG RomStatus;
    ULONG RomError;
    ULONG AckPolls;
    ULONG RomPolls;
    ULONG RestoredAdspcs;
    ULONG RestoredOutputControl;
    ULONG RestoredPpctl;
    ULONG RestoredHipcidr;
    ULONG RestoredHipcida;
    ULONG Ssp0Before;
    ULONG Ssp0After;
    ULONG Ssp1Before;
    ULONG Ssp1After;
    ULONG Ssp2Before;
    ULONG Ssp2After;
    ULONG Result;
    ULONG Reserved;
} SOFPROBE_ROM_ENTRY_RESULT, *PSOFPROBE_ROM_ENTRY_RESULT;

typedef struct _SOFPROBE_FIRMWARE_DMA_RESULT {
    SOFPROBE_ROM_ENTRY_RESULT Rom;
    ULONG RunningOutputControl;
    ULONG StoppedOutputControl;
    ULONG OutputPosition;
    ULONG OutputStatus;
    ULONG FirmwareStatus;
    ULONG FirmwareError;
    ULONG FirmwarePolls;
    ULONG StopPolls;
    ULONG SpibControlBefore;
    ULONG SpibValueBefore;
    ULONG SpibControlConfigured;
    ULONG SpibValueConfigured;
    ULONG SpibControlRestored;
    ULONG SpibValueRestored;
} SOFPROBE_FIRMWARE_DMA_RESULT, *PSOFPROBE_FIRMWARE_DMA_RESULT;

typedef struct _SOFPROBE_IPC3_READY_RESULT {
    SOFPROBE_FIRMWARE_DMA_RESULT Dma;
    ULONG Hipctdr;
    ULONG Hipctdd;
    ULONG Hipctda;
    ULONG MailboxSize;
    ULONG MailboxCommand;
    ULONG ReadyPolls;
    ULONG HipctdrAfterCleanup;
    ULONG HipctdaAfterCleanup;
    ULONG CleanupFlags;
    ULONG SspUnavailableMask;
} SOFPROBE_IPC3_READY_RESULT, *PSOFPROBE_IPC3_READY_RESULT;

typedef struct _SOFPROBE_IPC3_INFO_RESULT {
    SOFPROBE_IPC3_READY_RESULT Ready;
    ULONG ReadyWords[27]; // Complete 108-byte sof_ipc_fw_ready structure.
} SOFPROBE_IPC3_INFO_RESULT, *PSOFPROBE_IPC3_INFO_RESULT;

typedef struct _SOFPROBE_IPC3_ACK_RESULT {
    SOFPROBE_IPC3_INFO_RESULT Info;
    ULONG HipctdrAfterAck;
    ULONG HipctdaAfterAck;
    ULONG AckPolls;
} SOFPROBE_IPC3_ACK_RESULT, *PSOFPROBE_IPC3_ACK_RESULT;

typedef struct _SOFPROBE_IPC3_MEMORY_RESULT {
    SOFPROBE_IPC3_ACK_RESULT Ack;
    ULONG ReplyDoorbell;
    ULONG ReplySize;
    ULONG ReplyCommand;
    LONG ReplyError;
    ULONG ReplyPolls;
    ULONG ReplyWords[96]; // IPC3's 384-byte maximum mailbox message.
} SOFPROBE_IPC3_MEMORY_RESULT, *PSOFPROBE_IPC3_MEMORY_RESULT;

typedef struct _SOFPROBE_IPC3_SESSION_START_RESULT {
    SOFPROBE_IPC3_MEMORY_RESULT Boot;
    ULONG SessionActive;
} SOFPROBE_IPC3_SESSION_START_RESULT,
  *PSOFPROBE_IPC3_SESSION_START_RESULT;

typedef struct _SOFPROBE_IPC3_SESSION_MEMORY_RESULT {
    ULONG Version;
    ULONG ReplyDoorbell;
    ULONG ReplySize;
    ULONG ReplyCommand;
    LONG ReplyError;
    ULONG ReplyPolls;
    ULONG Result;
    ULONG ReplyWords[96];
} SOFPROBE_IPC3_SESSION_MEMORY_RESULT,
  *PSOFPROBE_IPC3_SESSION_MEMORY_RESULT;

typedef struct _SOFPROBE_IPC3_SESSION_STOP_RESULT {
    ULONG Version;
    ULONG BeforeAdspcs;
    ULONG RestoredAdspcs;
    ULONG SspUnavailableMask;
    ULONG CleanupFlags;
    ULONG Result;
} SOFPROBE_IPC3_SESSION_STOP_RESULT,
  *PSOFPROBE_IPC3_SESSION_STOP_RESULT;

typedef struct _SOFPROBE_IPC3_EXCHANGE_REQUEST {
    ULONG Version;
    ULONG RequestSize;
    ULONG RequestWords[96];
} SOFPROBE_IPC3_EXCHANGE_REQUEST,
  *PSOFPROBE_IPC3_EXCHANGE_REQUEST;
