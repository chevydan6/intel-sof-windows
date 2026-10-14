// SPDX-License-Identifier: GPL-2.0-only
// RT5682 bring-up through ACPI I2C: identify, calibrate, then remain muted.
// No stream, audio clocks, user register access, or speaker GPIO operation.
#include <ntddk.h>
#include <wdf.h>
#include <spb.h>
#include <acpiioct.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include "codec-policy.h"
#include "codec-interface.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD CodecAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE CodecPrepare;
EVT_WDF_DEVICE_RELEASE_HARDWARE CodecRelease;
EVT_WDF_DEVICE_D0_ENTRY CodecEnter;
EVT_WDF_DEVICE_D0_EXIT CodecExit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL CodecControl;

typedef struct _CODEC_CONTEXT {
    LARGE_INTEGER Connection;
    WDFIOTARGET I2c;
    ULONG DeviceId, VendorId, VersionId;
    ULONG Reads;
    BOOLEAN Identified;
    BOOLEAN Initialized;
    BOOLEAN Prepared;
    NTSTATUS LastStatus;
    ULONG HpMute, DacControl, DigitalPower;
} CODEC_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CODEC_CONTEXT, CodecContext);

static VOID Report(WDFDEVICE device, ULONG stage, NTSTATUS status)
{
    WDFKEY key;
    CODEC_CONTEXT *c = CodecContext(device);
    const PCWSTR names[] = { L"CodecStage", L"CodecStatus", L"CodecDeviceId",
        L"CodecVendorId", L"CodecVersionId", L"CodecReads", L"CodecInitialized",
        L"CodecHpMute", L"CodecDacControl", L"CodecDigitalPower" };
    ULONG values[] = { stage, (ULONG)status, c->DeviceId, c->VendorId, c->VersionId, c->Reads,
        c->Initialized, c->HpMute, c->DacControl, c->DigitalPower };
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

static NTSTATUS SelectI2cMode(WDFDEVICE device)
{
    UCHAR bytes[4] = { 0xff, 0xff, 0x00, 0x01 };
    WDF_MEMORY_DESCRIPTOR input;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR transferred = 0;
    NTSTATUS status;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&input, bytes, sizeof(bytes));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    status = WdfIoTargetSendWriteSynchronously(CodecContext(device)->I2c,
        NULL, &input, NULL, &options, &transferred);
    if (NT_SUCCESS(status) && transferred != sizeof(bytes)) return STATUS_DEVICE_DATA_ERROR;
    return status;
}

static VOID CodecDelay(ULONG milliseconds)
{
    LARGE_INTEGER delay;
    delay.QuadPart = -(LONGLONG)milliseconds * 10000;
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
}

static NTSTATUS WriteRegister(WDFDEVICE device, USHORT reg, USHORT value)
{
    CODEC_CONTEXT *c = CodecContext(device);
    UCHAR bytes[4] = { (UCHAR)(reg >> 8), (UCHAR)reg,
        (UCHAR)(value >> 8), (UCHAR)value };
    WDF_MEMORY_DESCRIPTOR input;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR transferred = 0;
    NTSTATUS status;
    if (!c->Identified || c->I2c == NULL) return STATUS_DEVICE_NOT_READY;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&input, bytes, sizeof(bytes));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    status = WdfIoTargetSendWriteSynchronously(c->I2c, NULL, &input, NULL, &options, &transferred);
    if (NT_SUCCESS(status) && transferred != sizeof(bytes)) return STATUS_DEVICE_DATA_ERROR;
    return status;
}

#include "rt5682-init.h"
#include "rt5682-playback.h"

static NTSTATUS ValidateAndSaveCrs(WDFDEVICE device)
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
    NTSTATUS status;
    DECLARE_CONST_UNICODE_STRING(name, L"CodecCrs");
    RtlZeroMemory(&request, sizeof(request));
    request.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    RtlCopyMemory(request.MethodName, "_CRS", 4);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&in, &request, sizeof(request));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&out, storage, sizeof(storage));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    status = WdfIoTargetSendIoctlSynchronously(WdfDeviceGetIoTarget(device),
            NULL, IOCTL_ACPI_EVAL_METHOD, &in, &out, &options, &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned < prefix || returned > sizeof(storage) ||
        output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        output->Length < prefix || output->Length > returned || output->Count != 1 ||
        output->Argument[0].Type != ACPI_METHOD_ARGUMENT_BUFFER ||
        output->Argument[0].DataLength > output->Length - prefix)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (NT_SUCCESS(WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE,
            KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        (VOID)WdfRegistryAssignValue(key, &name, REG_BINARY,
            output->Argument[0].DataLength, output->Argument[0].Data);
        WdfRegistryClose(key);
    }
    return CodecMatchesResource(output->Argument[0].Data, output->Argument[0].DataLength)
        ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
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
    WDF_IO_QUEUE_CONFIG queue;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = CodecPrepare;
    callbacks.EvtDeviceReleaseHardware = CodecRelease;
    callbacks.EvtDeviceD0Entry = CodecEnter;
    callbacks.EvtDeviceD0Exit = CodecExit;
    WdfDeviceInitSetPnpPowerEventCallbacks(init, &callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CODEC_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&init, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;
    status = WdfDeviceCreateDeviceInterface(device, &CodecInterfaceGuid, NULL);
    if (!NT_SUCCESS(status)) return status;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue, WdfIoQueueDispatchSequential);
    queue.EvtIoDeviceControl = CodecControl;
    return WdfIoQueueCreate(device, &queue, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

NTSTATUS CodecPrepare(WDFDEVICE device, WDFCMRESLIST raw, WDFCMRESLIST translated)
{
    CODEC_CONTEXT *c = CodecContext(device);
    ULONG i, found = 0;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(raw);
    c->Connection.QuadPart = 0;
    c->DeviceId = c->VendorId = c->VersionId = MAXULONG;
    c->Reads = 0;
    c->Identified = c->Initialized = FALSE;
    c->Prepared = FALSE;
    c->LastStatus = STATUS_DEVICE_NOT_READY;
    c->HpMute = c->DacControl = c->DigitalPower = MAXULONG;
    Report(device, 1, STATUS_PENDING);
    status = ValidateAndSaveCrs(device);
    if (!NT_SUCCESS(status)) { Report(device, 1, status); return status; }
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
    LARGE_INTEGER delay;
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
    // Realtek's Linux probe waits >=300 ms after supplies, selects I2C mode,
    // then waits >=10 ms before reading DEVICE_ID. No output is enabled.
    delay.QuadPart = -3000000;
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    status = SelectI2cMode(device);
    if (!NT_SUCCESS(status)) goto finish;
    delay.QuadPart = -100000;
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    status = ReadRegister(device, 0x00ff, &c->DeviceId);
    if (!NT_SUCCESS(status)) goto finish;
    if (c->DeviceId != 0x6530) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish; }
    status = ReadRegister(device, 0x00fe, &c->VendorId);
    if (!NT_SUCCESS(status)) goto finish;
    status = ReadRegister(device, 0x00fd, &c->VersionId);
    if (!NT_SUCCESS(status)) goto finish;
    if (c->VendorId != 0x10ec) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish; }
    c->Identified = TRUE;
    Report(device, 4, STATUS_PENDING);
    status = CodecInitializeMuted(device);
    if (!NT_SUCCESS(status)) goto finish;
    status = ReadRegister(device, RT5682_HP_CTRL_1, &c->HpMute);
    if (!NT_SUCCESS(status)) goto finish;
    status = ReadRegister(device, RT5682_HP_CTRL_2, &c->DacControl);
    if (!NT_SUCCESS(status)) goto finish;
    status = ReadRegister(device, RT5682_PWR_DIG_1, &c->DigitalPower);
    if (!NT_SUCCESS(status)) goto finish;
    if ((c->HpMute & 0x8080) != 0x8080 || (c->DacControl & 0xe000) != 0x8000 ||
        c->DigitalPower != 0) { status = STATUS_DEVICE_DATA_ERROR; goto finish; }
    c->Initialized = TRUE;
finish:
    c->LastStatus = status;
    Report(device, c->Identified ? 5 : 3, status);
    if (!NT_SUCCESS(status) && c->I2c != NULL) {
        if (c->Identified) (VOID)CodecResetMuted(device);
        WdfObjectDelete(c->I2c);
        c->I2c = NULL;
    }
    return status;
}

NTSTATUS CodecExit(WDFDEVICE device, WDF_POWER_DEVICE_STATE target)
{
    CODEC_CONTEXT *c = CodecContext(device);
    NTSTATUS status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(target);
    if (c->I2c != NULL) {
        if (c->Identified) status = CodecResetMuted(device);
        c->Initialized = FALSE;
        c->Prepared = FALSE;
        Report(device, 6, status);
        WdfIoTargetClose(c->I2c);
        WdfObjectDelete(c->I2c);
        c->I2c = NULL;
    }
    c->Identified = FALSE;
    return status;
}

VOID CodecControl(WDFQUEUE queue, WDFREQUEST request, size_t outputBytes,
                  size_t inputBytes, ULONG code)
{
    WDFDEVICE device = WdfIoQueueGetDevice(queue);
    CODEC_CONTEXT *c = CodecContext(device);
    CODEC_STATUS *result;
    NTSTATUS status;
    if (inputBytes != 0 || outputBytes < sizeof(*result) ||
        (code != IOCTL_CODEC_QUERY && code != IOCTL_CODEC_PREPARE && code != IOCTL_CODEC_STOP)) {
        WdfRequestComplete(request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(request, sizeof(*result), (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) { WdfRequestComplete(request, status); return; }
    if (!c->Identified || c->I2c == NULL) { WdfRequestComplete(request, STATUS_DEVICE_NOT_READY); return; }
    if (code == IOCTL_CODEC_STOP) {
        status = CodecResetMuted(device);
        c->Initialized = c->Prepared = FALSE;
    } else if (code == IOCTL_CODEC_PREPARE) {
        status = STATUS_SUCCESS;
        if (!c->Initialized) {
            status = CodecInitializeMuted(device);
            c->Initialized = NT_SUCCESS(status);
        }
        if (NT_SUCCESS(status) && !c->Prepared) {
            status = CodecPrepareHeadphoneMuted(device);
            c->Prepared = NT_SUCCESS(status);
            if (!NT_SUCCESS(status)) c->Initialized = FALSE;
        }
    } else status = STATUS_SUCCESS;
    if (NT_SUCCESS(status)) status = ReadRegister(device, RT5682_HP_CTRL_1, &c->HpMute);
    if (NT_SUCCESS(status)) status = ReadRegister(device, RT5682_HP_CTRL_2, &c->DacControl);
    if (NT_SUCCESS(status)) status = ReadRegister(device, RT5682_PWR_DIG_1, &c->DigitalPower);
    if (!NT_SUCCESS(status)) {
        (VOID)CodecResetMuted(device);
        c->Initialized = c->Prepared = FALSE;
    }
    c->LastStatus = status;
    Report(device, c->Prepared ? 7 : 5, status);
    if (!NT_SUCCESS(status)) { WdfRequestComplete(request, status); return; }
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1; result->DeviceId = c->DeviceId; result->VendorId = c->VendorId;
    result->Initialized = c->Initialized; result->Prepared = c->Prepared;
    result->HpMute = c->HpMute; result->DacControl = c->DacControl;
    result->DigitalPower = c->DigitalPower; result->LastStatus = (ULONG)c->LastStatus;
    WdfRequestCompleteWithInformation(request, status, sizeof(*result));
}

NTSTATUS CodecRelease(WDFDEVICE device, WDFCMRESLIST resources)
{
    UNREFERENCED_PARAMETER(resources);
    // D0Exit normally closes the connection; also handle failed D0Entry.
    return CodecExit(device, WdfPowerDeviceInvalid);
}
