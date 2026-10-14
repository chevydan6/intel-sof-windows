// SPDX-License-Identifier: GPL-2.0-only
// Included after the SOF device context; only used by the headphone build.
#include "../codec/codec-interface.h"

static NTSTATUS HeadphoneCodecCommand(WDFIOTARGET target, ULONG code)
{
    CODEC_STATUS result = {0};
    WDF_MEMORY_DESCRIPTOR out;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR returned = 0;
    NTSTATUS status;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&out, &result, sizeof(result));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(5));
    status = WdfIoTargetSendIoctlSynchronously(target, NULL, code,
        NULL, &out, &options, &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned != sizeof(result) || result.Version != 1 || result.DeviceId != 0x6530 ||
        result.VendorId != 0x10ec || result.LastStatus != 0)
        return STATUS_DEVICE_DATA_ERROR;
    if (code == IOCTL_CODEC_STREAM_START) {
        BOOLEAN active = (result.HpMute & 0x8080) == 0 &&
                         (result.DacControl & 0xe000) == 0x6000;
        BOOLEAN waiting = (result.HpMute & 0x8080) == 0x8080 &&
                          (result.DacControl & 0xe000) == 0x8000;
        if (!active && !waiting) return STATUS_DEVICE_DATA_ERROR;
        if (active && (!result.Initialized || !result.Prepared)) return STATUS_DEVICE_DATA_ERROR;
    } else if ((result.HpMute & 0x8080) != 0x8080 ||
               (result.DacControl & 0xe000) != 0x8000) return STATUS_DEVICE_DATA_ERROR;
    if (code == IOCTL_CODEC_STOP && (result.Initialized || result.Prepared || result.DigitalPower))
        return STATUS_DEVICE_DATA_ERROR;
    if (code == IOCTL_CODEC_PREPARE && (!result.Initialized || !result.Prepared))
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

static NTSTATUS HeadphoneCodecOpen(WDFDEVICE device)
{
    PSOFPROBE_DEVICE_CONTEXT c = SofProbeGetContext(device);
    PWSTR links = NULL;
    UNICODE_STRING name;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS open;
    NTSTATUS status;
    if (c->HeadphoneCodec != NULL) return HeadphoneCodecCommand(c->HeadphoneCodec, IOCTL_CODEC_QUERY);
    status = IoGetDeviceInterfaces(&CodecInterfaceGuid, NULL, 0, &links);
    if (!NT_SUCCESS(status)) return status;
    if (links == NULL || links[0] == 0) { status = STATUS_DEVICE_NOT_READY; goto finish; }
    RtlInitUnicodeString(&name, links);
    if (links[name.Length / sizeof(WCHAR) + 1] != 0) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish; }
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfIoTargetCreate(device, &attributes, &c->HeadphoneCodec);
    if (!NT_SUCCESS(status)) goto finish;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &name, GENERIC_READ | GENERIC_WRITE);
    open.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
    status = WdfIoTargetOpen(c->HeadphoneCodec, &open);
    if (NT_SUCCESS(status)) status = HeadphoneCodecCommand(c->HeadphoneCodec, IOCTL_CODEC_QUERY);
    if (!NT_SUCCESS(status)) { WdfObjectDelete(c->HeadphoneCodec); c->HeadphoneCodec = NULL; }
finish:
    if (links != NULL) ExFreePool(links);
    return status;
}

static VOID HeadphoneCodecClose(PSOFPROBE_DEVICE_CONTEXT c)
{
    if (c->HeadphoneCodec != NULL) {
        (VOID)HeadphoneCodecCommand(c->HeadphoneCodec, IOCTL_CODEC_STOP);
        WdfIoTargetClose(c->HeadphoneCodec);
        WdfObjectDelete(c->HeadphoneCodec);
        c->HeadphoneCodec = NULL;
    }
}
