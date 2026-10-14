// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Administrator-only fixed operations; no arbitrary register access.
static const GUID CodecInterfaceGuid =
    {0x128f66be,0x4874,0x44dd,{0x92,0x70,0x90,0xa8,0xf7,0x9e,0x28,0x34}};
#define IOCTL_CODEC_QUERY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CODEC_PREPARE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_CODEC_STOP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_BUFFERED, FILE_WRITE_ACCESS)
typedef struct {
    ULONG Version, DeviceId, VendorId, Initialized, Prepared;
    ULONG HpMute, DacControl, DigitalPower, LastStatus;
} CODEC_STATUS;
