// SPDX-License-Identifier: GPL-2.0-only
// PUJJOTEEN default-off amplifier control. Only a kernel client can request
// a short diagnostic lease. No raw MMIO or user-mode enable operation.
#include <ntddk.h>
#include <wdf.h>
#include <acpiioct.h>
#include <gpio.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include "sofamp-policy.h"
#include "amp-interface.h"
#include "amp-lifecycle.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofAmpAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofAmpPrepare;
EVT_WDF_DEVICE_RELEASE_HARDWARE SofAmpRelease;
EVT_WDF_DEVICE_D0_ENTRY SofAmpEnter;
EVT_WDF_DEVICE_D0_EXIT SofAmpExit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL SofAmpControl;
EVT_WDF_TIMER SofAmpTimer;
EVT_WDF_FILE_CLEANUP SofAmpFileCleanup;

typedef struct _SOFAMP_CONTEXT {
    LARGE_INTEGER Connection;
    WDFIOTARGET Gpio;
    BOOLEAN Validated;
    SOFAMP_STATE State;
    WDFWAITLOCK Lock;
    WDFTIMER Timer;
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
        SOFAMP_STATE *s = &SofAmpContext(device)->State;
        const PCWSTR names[] = {L"SofAmpActive",L"SofAmpFaulted",L"SofAmpOutputRequest",
            L"SofAmpOnWrites",L"SofAmpWatchdogTrips",L"SofAmpFaultStatus",L"SofAmpCleanupStatus"};
        ULONG values[] = {s->Active,s->Faulted,s->OutputRequest,s->OnWrites,s->WatchdogTrips,
            (ULONG)s->FaultStatus,(ULONG)s->CleanupStatus};
        ULONG i;
        (VOID)WdfRegistryAssignULong(key, &countName, s->OffWrites);
        for (i = 0; i < RTL_NUMBER_OF(names); ++i) {
            UNICODE_STRING name;
            RtlInitUnicodeString(&name,names[i]);
            (VOID)WdfRegistryAssignULong(key,&name,values[i]);
        }
        WdfRegistryClose(key);
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "SOFAMP: stage=%lu status=%08lx off-writes=%lu\n",
        stage, status, SofAmpContext(device)->State.OffWrites));
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

static ULONGLONG SofAmpNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static NTSTATUS SofAmpDriveRaw(PVOID io, UCHAR value)
{
    WDFDEVICE device = (WDFDEVICE)io;
    SOFAMP_CONTEXT *context = SofAmpContext(device);
    UCHAR requested = value;
    WDF_MEMORY_DESCRIPTOR data;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR transferred = 0;
    NTSTATUS status;
    if (!context->Validated || context->Gpio == NULL || value > 1)
        return STATUS_DEVICE_NOT_READY;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&data, &value, sizeof(value));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_MS(100));
    status = WdfIoTargetSendIoctlSynchronously(context->Gpio, NULL,
        IOCTL_GPIO_WRITE_PINS, &data, &data, &options, &transferred);
    if (NT_SUCCESS(status) && (transferred != sizeof(value) || value != requested))
        status = STATUS_DEVICE_DATA_ERROR;
    // The GPIO buffer is an echo, not electrical or temperature readback.
    return status;
}

static VOID SofAmpArmTimer(SOFAMP_CONTEXT *c)
{
    ULONGLONG now = SofAmpNowMs();
    ULONG wait = c->State.Deadline > now ? (ULONG)(c->State.Deadline - now) : 1;
    if (c->State.Active) WdfTimerStart(c->Timer, WDF_REL_TIMEOUT_IN_MS(wait));
}

VOID SofAmpTimer(WDFTIMER timer)
{
    WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(timer);
    SOFAMP_CONTEXT *c = SofAmpContext(device);
    WdfWaitLockAcquire(c->Lock, NULL);
    if (c->State.Ready && c->State.Active) {
        NTSTATUS status = SofAmpStateExpired(&c->State, device);
        // A renewal can make an already queued timer callback early.
        SofAmpArmTimer(c);
        if (!NT_SUCCESS(status)) SofAmpReport(device, 6, status);
    }
    WdfWaitLockRelease(c->Lock);
}

VOID SofAmpFileCleanup(WDFFILEOBJECT file)
{
    WDFDEVICE device = WdfFileObjectGetDevice(file);
    SOFAMP_CONTEXT *c = SofAmpContext(device);
    WdfWaitLockAcquire(c->Lock, NULL);
    SofAmpStateFileCleanup(&c->State, device, file);
    WdfWaitLockRelease(c->Lock);
}

VOID SofAmpControl(WDFQUEUE queue, WDFREQUEST request, size_t outputBytes,
                   size_t inputBytes, ULONG code)
{
    WDFDEVICE device = WdfIoQueueGetDevice(queue);
    SOFAMP_CONTEXT *c = SofAmpContext(device);
    SOFAMP_STATUS *result;
    NTSTATUS status;
    WDFFILEOBJECT owner = WdfRequestGetFileObject(request);
    if (inputBytes != 0 || outputBytes < sizeof(*result) ||
        (code != IOCTL_SOFAMP_QUERY && code != IOCTL_SOFAMP_STOP &&
         code != IOCTL_SOFAMP_START && code != IOCTL_SOFAMP_START_LISTEN && code != IOCTL_SOFAMP_RENEW &&
         code != IOCTL_SOFAMP_QUERY_PREVIEW && code != IOCTL_SOFAMP_START_PREVIEW &&
         code != IOCTL_SOFAMP_QUERY_PREVIEW_30 && code != IOCTL_SOFAMP_START_PREVIEW_30)) {
        WdfRequestComplete(request, STATUS_INVALID_PARAMETER); return;
    }
    if ((code == IOCTL_SOFAMP_START || code == IOCTL_SOFAMP_START_LISTEN ||
         code == IOCTL_SOFAMP_START_PREVIEW || code == IOCTL_SOFAMP_START_PREVIEW_30 || code == IOCTL_SOFAMP_RENEW) &&
        WdfRequestGetRequestorMode(request) != KernelMode) {
        WdfRequestComplete(request, STATUS_ACCESS_DENIED); return;
    }
    status = WdfRequestRetrieveOutputBuffer(request, sizeof(*result), (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) { WdfRequestComplete(request, status); return; }
    WdfWaitLockAcquire(c->Lock, NULL);
    if (!c->State.Ready) { status = STATUS_DEVICE_NOT_READY; goto done; }
    if (code == IOCTL_SOFAMP_START || code == IOCTL_SOFAMP_START_LISTEN ||
        code == IOCTL_SOFAMP_START_PREVIEW || code == IOCTL_SOFAMP_START_PREVIEW_30) {
        LARGE_INTEGER delay;
        // Match the board's sdmode-delay; the parent must establish valid,
        // quiet clocks/data before asking for an amplifier lease.
        delay.QuadPart = -(LONGLONG)SOFAMP_START_DELAY_MS * 10000;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        status = code == IOCTL_SOFAMP_START_PREVIEW_30
            ? SofAmpStateStartBounded(&c->State, device, owner, SOFAMP_PREVIEW_30_MAXIMUM_MS)
            : code == IOCTL_SOFAMP_START_PREVIEW
            ? SofAmpStateStartBounded(&c->State, device, owner, SOFAMP_PREVIEW_MAXIMUM_MS)
            : code == IOCTL_SOFAMP_START_LISTEN
            ? SofAmpStateStartBounded(&c->State, device, owner, SOFAMP_LISTEN_MAXIMUM_MS)
            : SofAmpStateStart(&c->State, device, owner);
        SofAmpArmTimer(c);
    } else if (code == IOCTL_SOFAMP_RENEW) {
        status = SofAmpStateRenew(&c->State, device, owner);
        SofAmpArmTimer(c);
    } else if (code == IOCTL_SOFAMP_STOP) {
        status = SofAmpStateOff(&c->State, device);
    } else {
        (VOID)SofAmpStateExpired(&c->State, device);
        status = STATUS_SUCCESS; // Faults remain visible in the returned state.
    }
    SofAmpReport(device, c->State.Active ? 5 : 3,
        c->State.Faulted ? c->State.FaultStatus : status);
    if (NT_SUCCESS(status)) {
        RtlZeroMemory(result, sizeof(*result));
        result->Version = 2; result->Ready = c->State.Ready;
        result->Active = c->State.Active; result->Faulted = c->State.Faulted;
        result->OutputRequest = c->State.OutputRequest;
        result->OnWrites = c->State.OnWrites; result->OffWrites = c->State.OffWrites;
        result->WatchdogTrips = c->State.WatchdogTrips;
        result->FaultStatus = (ULONG)c->State.FaultStatus;
        result->CleanupStatus = (ULONG)c->State.CleanupStatus;
        result->LeaseMs = SOFAMP_LEASE_MS; result->MaximumMs = SOFAMP_LISTEN_MAXIMUM_MS;
        if (code == IOCTL_SOFAMP_QUERY_PREVIEW || code == IOCTL_SOFAMP_START_PREVIEW)
            result->MaximumMs = SOFAMP_PREVIEW_MAXIMUM_MS;
        if (code == IOCTL_SOFAMP_QUERY_PREVIEW_30 || code == IOCTL_SOFAMP_START_PREVIEW_30)
            result->MaximumMs = SOFAMP_PREVIEW_30_MAXIMUM_MS;
        WdfRequestSetInformation(request, sizeof(*result));
    }
done:
    WdfWaitLockRelease(c->Lock);
    WdfRequestComplete(request, status);
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
    WDF_FILEOBJECT_CONFIG files;
    WDF_IO_QUEUE_CONFIG queue;
    WDF_TIMER_CONFIG timer;
    WDFDEVICE device;
    SOFAMP_CONTEXT *c;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = SofAmpPrepare;
    callbacks.EvtDeviceReleaseHardware = SofAmpRelease;
    callbacks.EvtDeviceD0Entry = SofAmpEnter;
    callbacks.EvtDeviceD0Exit = SofAmpExit;
    WdfDeviceInitSetPnpPowerEventCallbacks(init, &callbacks);
    WDF_FILEOBJECT_CONFIG_INIT(&files, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, SofAmpFileCleanup);
    WdfDeviceInitSetFileObjectConfig(init, &files, WDF_NO_OBJECT_ATTRIBUTES);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SOFAMP_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    attributes.SynchronizationScope = WdfSynchronizationScopeNone;
    status = WdfDeviceCreate(&init, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;
    c = SofAmpContext(device);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ParentObject = device;
    status = WdfWaitLockCreate(&attributes, &c->Lock);
    if (!NT_SUCCESS(status)) return status;
    WDF_TIMER_CONFIG_INIT(&timer, SofAmpTimer);
    timer.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfTimerCreate(&timer, &attributes, &c->Timer);
    if (!NT_SUCCESS(status)) return status;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue, WdfIoQueueDispatchSequential);
    queue.EvtIoDeviceControl = SofAmpControl;
    status = WdfIoQueueCreate(device, &queue, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) return status;
    return WdfDeviceCreateDeviceInterface(device, &SofAmpInterfaceGuid, NULL);
}

NTSTATUS SofAmpPrepare(WDFDEVICE device, WDFCMRESLIST raw, WDFCMRESLIST translated)
{
    SOFAMP_CONTEXT *context = SofAmpContext(device);
    ULONG i, connections = 0;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(raw);
    context->Validated = FALSE;
    context->Connection.QuadPart = 0;
    RtlZeroMemory(&context->State, sizeof(context->State));
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
    status = SofAmpStateOff(&context->State, device);
    context->State.Ready = NT_SUCCESS(status);
finish:
    // Failed D0Entry is followed by ReleaseHardware, not D0Exit. Release
    // closes any open target. Failure is never reported as physical shutdown.
    SofAmpReport(device, 3, status);
    return status;
}

NTSTATUS SofAmpExit(WDFDEVICE device, WDF_POWER_DEVICE_STATE target)
{
    SOFAMP_CONTEXT *c = SofAmpContext(device);
    NTSTATUS status;
    UNREFERENCED_PARAMETER(target);
    WdfWaitLockAcquire(c->Lock, NULL);
    c->State.Ready = FALSE;
    status = SofAmpStateOff(&c->State, device);
    SofAmpReport(device, 4, status);
    WdfWaitLockRelease(c->Lock);
    // Never wait for a callback while holding the lock it needs.
    WdfTimerStop(c->Timer, TRUE);
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
    context->State.Ready = context->State.Active = FALSE;
    context->State.Owner = NULL;
    context->State.OutputRequest = SOFAMP_OUTPUT_UNKNOWN;
    return STATUS_SUCCESS;
}
