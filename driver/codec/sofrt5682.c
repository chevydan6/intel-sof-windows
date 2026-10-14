// SPDX-License-Identifier: GPL-2.0-only
// First RT5682 bring-up: identify the codec through its ACPI I2C connection.
// No codec configuration, DAC power, audio clock, or speaker GPIO writes.
#include <ntddk.h>
#include <wdf.h>
#include <spb.h>
#include <acpiioct.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD CodecAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE CodecPrepare;
EVT_WDF_DEVICE_RELEASE_HARDWARE CodecRelease;
EVT_WDF_DEVICE_D0_ENTRY CodecEnter;
EVT_WDF_DEVICE_D0_EXIT CodecExit;

typedef struct _CODEC_CONTEXT {
    LARGE_INTEGER Connection;
    WDFIOTARGET I2c;
    ULONG DeviceId, VendorId, VersionId;
    ULONG Reads;
} CODEC_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CODEC_CONTEXT, CodecContext);

static VOID Report(WDFDEVICE device, ULONG stage, NTSTATUS status)
{
    WDFKEY key;
    CODEC_CONTEXT *c = CodecContext(device);
    const PCWSTR names[] = { L"CodecStage", L"CodecStatus", L"CodecDeviceId",
        L"CodecVendorId", L"CodecVersionId", L"CodecReads" };
    ULONG values[] = { stage, (ULONG)status, c->DeviceId, c->VendorId, c->VersionId, c->Reads };
    ULONG i;
    if (NT_SUCCESS(WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE,
            KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        for (i = 0; i < RTL_NUMBER_OF(names); ++i) {
            UNICODE_STRING name;
            RtlInitUnicodeString(&name, names[i]);
            (VOID)WdfRegistryAssignULong(key, &name, values[i]);
        }
        WdfRegistryClose(key);
    }
}

static NTSTATUS ReadRegister(WDFDEVICE device, USHORT reg, PULONG value)
{
    CODEC_CONTEXT *c = CodecContext(device);
    // Linux regmap: 16-bit address and value, both big endian on the bus.
    UCHAR address[2] = { (UCHAR)(reg >> 8), (UCHAR)reg };
    UCHAR data[2] = {0};
    SPB_TRANSFER_LIST_AND_ENTRIES(2) sequence;
    WDF_MEMORY_DESCRIPTOR input;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR transferred = 0;
    NTSTATUS status;
    if (c->I2c == NULL) return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&sequence, sizeof(sequence));
    SPB_TRANSFER_LIST_INIT(&sequence.List, 2);
    sequence.List.Transfers[0] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(
        SpbTransferDirectionToDevice, 0, address, sizeof(address));
    sequence.List.Transfers[1] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(
        SpbTransferDirectionFromDevice, 0, data, sizeof(data));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&input, &sequence, sizeof(sequence));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    status = WdfIoTargetSendIoctlSynchronously(c->I2c, NULL,
        IOCTL_SPB_EXECUTE_SEQUENCE, &input, NULL, &options, &transferred);
    if (!NT_SUCCESS(status)) return status;
    if (transferred != sizeof(address) + sizeof(data)) return STATUS_DEVICE_DATA_ERROR;
    *value = ((ULONG)data[0] << 8) | data[1];
    ++c->Reads;
    return STATUS_SUCCESS;
}

static VOID SaveCrs(WDFDEVICE device)
{
    ACPI_EVAL_INPUT_BUFFER request;
    ULONG storage[256] = {0};
    PACPI_EVAL_OUTPUT_BUFFER output = (PACPI_EVAL_OUTPUT_BUFFER)storage;
    WDF_MEMORY_DESCRIPTOR in, out;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR returned = 0;
    const ULONG prefix = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) +
        FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data);
    WDFKEY key;
    DECLARE_CONST_UNICODE_STRING(name, L"CodecCrs");
    RtlZeroMemory(&request, sizeof(request));
    request.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    RtlCopyMemory(request.MethodName, "_CRS", 4);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&in, &request, sizeof(request));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&out, storage, sizeof(storage));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    if (!NT_SUCCESS(WdfIoTargetSendIoctlSynchronously(WdfDeviceGetIoTarget(device),
            NULL, IOCTL_ACPI_EVAL_METHOD, &in, &out, &options, &returned))) return;
    if (returned < prefix || returned > sizeof(storage) ||
        output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        output->Length < prefix || output->Length > returned || output->Count != 1 ||
        output->Argument[0].Type != ACPI_METHOD_ARGUMENT_BUFFER ||
        output->Argument[0].DataLength > output->Length - prefix) return;
    if (NT_SUCCESS(WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE,
            KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        (VOID)WdfRegistryAssignValue(key, &name, REG_BINARY,
            output->Argument[0].DataLength, output->Argument[0].Data);
        WdfRegistryClose(key);
    }
}

NTSTATUS DriverEntry(PDRIVER_OBJECT object, PUNICODE_STRING path)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, CodecAdd);
    return WdfDriverCreate(object, path, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS CodecAdd(WDFDRIVER driver, PWDFDEVICE_INIT init)
{
    WDF_PNPPOWER_EVENT_CALLBACKS callbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    UNREFERENCED_PARAMETER(driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = CodecPrepare;
    callbacks.EvtDeviceReleaseHardware = CodecRelease;
    callbacks.EvtDeviceD0Entry = CodecEnter;
    callbacks.EvtDeviceD0Exit = CodecExit;
    WdfDeviceInitSetPnpPowerEventCallbacks(init, &callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    return WdfDeviceCreate(&init, &attributes, &device);
}

NTSTATUS CodecPrepare(WDFDEVICE device, WDFCMRESLIST raw, WDFCMRESLIST translated)
{
    CODEC_CONTEXT *c = CodecContext(device);
    ULONG i, found = 0;
    UNREFERENCED_PARAMETER(raw);
    c->Connection.QuadPart = 0;
    c->DeviceId = c->VendorId = c->VersionId = MAXULONG;
    c->Reads = 0;
    Report(device, 1, STATUS_PENDING);
    SaveCrs(device);
    for (i = 0; i < WdfCmResourceListGetCount(translated); ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR r = WdfCmResourceListGetDescriptor(translated, i);
        if (r == NULL) return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (r->Type != CmResourceTypeConnection) continue;
        if (r->u.Connection.Class != CM_RESOURCE_CONNECTION_CLASS_SERIAL ||
            r->u.Connection.Type != CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        ++found;
        c->Connection.LowPart = r->u.Connection.IdLowPart;
        c->Connection.HighPart = r->u.Connection.IdHighPart;
    }
    return found == 1 ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS CodecEnter(WDFDEVICE device, WDF_POWER_DEVICE_STATE previous)
{
    CODEC_CONTEXT *c = CodecContext(device);
    WCHAR buffer[100];
    UNICODE_STRING path;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS open;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(previous);
    RtlInitEmptyUnicodeString(&path, buffer, sizeof(buffer));
    status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&path,
        c->Connection.LowPart, c->Connection.HighPart);
    if (!NT_SUCCESS(status)) goto finish;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfIoTargetCreate(device, &attributes, &c->I2c);
    if (!NT_SUCCESS(status)) goto finish;
    // Writes here are register-address selection, not configuration writes.
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &path, GENERIC_READ | GENERIC_WRITE);
    open.ShareAccess = 0;
    status = WdfIoTargetOpen(c->I2c, &open);
    if (!NT_SUCCESS(status)) goto finish;
    Report(device, 2, STATUS_PENDING);
    status = ReadRegister(device, 0x00ff, &c->DeviceId);
    if (!NT_SUCCESS(status)) goto finish;
    if (c->DeviceId != 0x6530) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish; }
    status = ReadRegister(device, 0x00fe, &c->VendorId);
    if (!NT_SUCCESS(status)) goto finish;
    status = ReadRegister(device, 0x00fd, &c->VersionId);
finish:
    Report(device, 3, status);
    if (!NT_SUCCESS(status) && c->I2c != NULL) {
        WdfObjectDelete(c->I2c);
        c->I2c = NULL;
    }
    return status;
}

NTSTATUS CodecExit(WDFDEVICE device, WDF_POWER_DEVICE_STATE target)
{
    CODEC_CONTEXT *c = CodecContext(device);
    UNREFERENCED_PARAMETER(target);
    if (c->I2c != NULL) {
        WdfIoTargetClose(c->I2c);
        WdfObjectDelete(c->I2c);
        c->I2c = NULL;
    }
    return STATUS_SUCCESS;
}

NTSTATUS CodecRelease(WDFDEVICE device, WDFCMRESLIST resources)
{
    UNREFERENCED_PARAMETER(resources);
    // D0Exit normally closes the connection; also handle failed D0Entry.
    return CodecExit(device, WdfPowerDeviceInvalid);
}
