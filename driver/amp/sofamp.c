// SPDX-License-Identifier: GPL-2.0-only
// PUJJOTEEN amplifier shutdown-only development driver. There is deliberately
// no enable operation, user IOCTL interface, audio stream, or raw MMIO access.
#include <ntddk.h>
#include <wdf.h>
#include <acpiioct.h>
#include <gpio.h>
#include <reshub.h>
#include "sofamp-policy.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofAmpAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofAmpPrepare;
EVT_WDF_DEVICE_RELEASE_HARDWARE SofAmpRelease;
EVT_WDF_DEVICE_D0_ENTRY SofAmpEnter;
EVT_WDF_DEVICE_D0_EXIT SofAmpExit;

typedef struct _SOFAMP_CONTEXT {
    LARGE_INTEGER Connection;
    WDFIOTARGET Gpio;
    BOOLEAN Validated;
    ULONG OffWrites;
} SOFAMP_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOFAMP_CONTEXT, SofAmpContext);

static VOID
SofAmpReport(WDFDEVICE device, ULONG stage, NTSTATUS status)
{
    WDFKEY key;
    DECLARE_CONST_UNICODE_STRING(stageName, L"SofAmpStage");
    DECLARE_CONST_UNICODE_STRING(statusName, L"SofAmpStatus");
    DECLARE_CONST_UNICODE_STRING(countName, L"SofAmpOffWrites");
    if (NT_SUCCESS(WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE,
            KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        (VOID)WdfRegistryAssignULong(key, &stageName, stage);
        (VOID)WdfRegistryAssignULong(key, &statusName, (ULONG)status);
        (VOID)WdfRegistryAssignULong(key, &countName, SofAmpContext(device)->OffWrites);
        WdfRegistryClose(key);
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "SOFAMP: stage=%lu status=%08lx off-writes=%lu\n",
        stage, status, SofAmpContext(device)->OffWrites));
}

static NTSTATUS
SofAmpValidateCrs(WDFDEVICE device)
{
    ACPI_EVAL_INPUT_BUFFER input;
    // ULONG alignment accommodates the ACPI output header/argument fields.
    ULONG storage[128] = {0};
    PACPI_EVAL_OUTPUT_BUFFER output = (PACPI_EVAL_OUTPUT_BUFFER)storage;
    WDF_MEMORY_DESCRIPTOR in, out;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR returned = 0;
    NTSTATUS status;
    const ULONG prefix = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) +
                         FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data);
    RtlZeroMemory(&input, sizeof(input));
    input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    RtlCopyMemory(input.MethodName, "_CRS", 4);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&in, &input, sizeof(input));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&out, storage, sizeof(storage));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    status = WdfIoTargetSendIoctlSynchronously(WdfDeviceGetIoTarget(device),
        NULL, IOCTL_ACPI_EVAL_METHOD, &in, &out, &options, &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned < prefix || returned > sizeof(storage) ||
        output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        output->Length < prefix || output->Length > returned ||
        output->Count != 1 || output->Argument[0].Type != ACPI_METHOD_ARGUMENT_BUFFER ||
        output->Argument[0].DataLength > output->Length - prefix ||
        !SofAmpMatchesResource(output->Argument[0].Data, output->Argument[0].DataLength))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

static NTSTATUS
SofAmpWriteOff(WDFDEVICE device)
{
    SOFAMP_CONTEXT *context = SofAmpContext(device);
    // Bit 0 is the ONLY pin in the validated resource. Zero drives EN low.
    UCHAR value = 0;
    WDF_MEMORY_DESCRIPTOR data;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR transferred = 0;
    NTSTATUS status;
    if (!context->Validated || context->Gpio == NULL) return STATUS_DEVICE_NOT_READY;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&data, &value, sizeof(value));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
    // METHOD_BUFFERED requires an equally sized output buffer. Its echoed
    // value is NOT a measurement of pin voltage or amplifier temperature.
    status = WdfIoTargetSendIoctlSynchronously(context->Gpio, NULL,
        IOCTL_GPIO_WRITE_PINS, &data, &data, &options, &transferred);
    if (NT_SUCCESS(status) && (transferred != sizeof(value) || value != 0))
        status = STATUS_DEVICE_DATA_ERROR;
    if (NT_SUCCESS(status)) ++context->OffWrites;
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT object, PUNICODE_STRING path)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, SofAmpAdd);
    return WdfDriverCreate(object, path, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS SofAmpAdd(WDFDRIVER driver, PWDFDEVICE_INIT init)
{
    WDF_PNPPOWER_EVENT_CALLBACKS callbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    UNREFERENCED_PARAMETER(driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = SofAmpPrepare;
    callbacks.EvtDeviceReleaseHardware = SofAmpRelease;
    callbacks.EvtDeviceD0Entry = SofAmpEnter;
    callbacks.EvtDeviceD0Exit = SofAmpExit;
    WdfDeviceInitSetPnpPowerEventCallbacks(init, &callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SOFAMP_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    return WdfDeviceCreate(&init, &attributes, &device);
}

NTSTATUS SofAmpPrepare(WDFDEVICE device, WDFCMRESLIST raw, WDFCMRESLIST translated)
{
    SOFAMP_CONTEXT *context = SofAmpContext(device);
    ULONG i, connections = 0;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(raw);
    context->Validated = FALSE;
    context->Connection.QuadPart = 0;
    context->OffWrites = 0;
    SofAmpReport(device, 1, STATUS_PENDING);
    for (i = 0; i < WdfCmResourceListGetCount(translated); ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR r = WdfCmResourceListGetDescriptor(translated, i);
        if (r == NULL) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish; }
        if (r->Type != CmResourceTypeConnection) continue;
        if (r->u.Connection.Class != CM_RESOURCE_CONNECTION_CLASS_GPIO ||
            r->u.Connection.Type != CM_RESOURCE_CONNECTION_TYPE_GPIO_IO) {
            status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish;
        }
        ++connections;
        context->Connection.LowPart = r->u.Connection.IdLowPart;
        context->Connection.HighPart = r->u.Connection.IdHighPart;
    }
    if (connections != 1) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish; }
    status = SofAmpValidateCrs(device);
    context->Validated = NT_SUCCESS(status);
finish:
    SofAmpReport(device, 2, status);
    return status;
}

NTSTATUS SofAmpEnter(WDFDEVICE device, WDF_POWER_DEVICE_STATE previous)
{
    SOFAMP_CONTEXT *context = SofAmpContext(device);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS open;
    WCHAR buffer[100];
    UNICODE_STRING path;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(previous);
    if (!context->Validated) return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (context->Gpio == NULL) {
        RtlInitEmptyUnicodeString(&path, buffer, sizeof(buffer));
        status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&path,
            context->Connection.LowPart, context->Connection.HighPart);
        if (!NT_SUCCESS(status)) goto finish;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = device;
        status = WdfIoTargetCreate(device, &attributes, &context->Gpio);
        if (!NT_SUCCESS(status)) goto finish;
        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &path, GENERIC_WRITE);
        open.ShareAccess = 0;
        status = WdfIoTargetOpen(context->Gpio, &open);
        if (!NT_SUCCESS(status)) {
            WdfObjectDelete(context->Gpio);
            context->Gpio = NULL;
            goto finish;
        }
    }
    status = SofAmpWriteOff(device);
finish:
    // Failed D0Entry is followed by ReleaseHardware, not D0Exit. Release
    // closes any open target. Failure is never reported as physical shutdown.
    SofAmpReport(device, 3, status);
    return status;
}

NTSTATUS SofAmpExit(WDFDEVICE device, WDF_POWER_DEVICE_STATE target)
{
    NTSTATUS status;
    UNREFERENCED_PARAMETER(target);
    status = SofAmpWriteOff(device);
    SofAmpReport(device, 4, status);
    return status;
}

NTSTATUS SofAmpRelease(WDFDEVICE device, WDFCMRESLIST resources)
{
    SOFAMP_CONTEXT *context = SofAmpContext(device);
    UNREFERENCED_PARAMETER(resources);
    // No GPIO I/O here: surprise removal may already have removed hardware.
    // Do not promise that a GPIO controller retains low after close/removal.
    if (context->Gpio != NULL) {
        WdfIoTargetClose(context->Gpio);
        WdfObjectDelete(context->Gpio);
        context->Gpio = NULL;
    }
    context->Validated = FALSE;
    return STATUS_SUCCESS;
}
