// SPDX-License-Identifier: GPL-2.0-only
// Read a fixed set of HDA and DSP status registers. No device register writes,
// DMA, firmware, interrupts, or codec access. Boot values are saved in the
// device registry key; an explicit IOCTL returns a fresh read-only snapshot.

#include <ntddk.h>
#include <wdf.h>
#include "sofprobe-ioctl.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofProbeEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofProbeEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE SofProbeEvtReleaseHardware;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL SofProbeEvtIoDeviceControl;

typedef struct _SOFPROBE_DEVICE_CONTEXT {
    PHYSICAL_ADDRESS HdaStart;
    PHYSICAL_ADDRESS DspStart;
    BOOLEAN ResourcesReady;
} SOFPROBE_DEVICE_CONTEXT, *PSOFPROBE_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOFPROBE_DEVICE_CONTEXT, SofProbeGetContext)

// {F6B7AB70-381C-4A67-A85C-4A006A7B01DD}
static const GUID SofProbeInterfaceGuid =
    { 0xf6b7ab70, 0x381c, 0x4a67, { 0xa8, 0x5c, 0x4a, 0x00, 0x6a, 0x7b, 0x01, 0xdd } };

static NTSTATUS
SofProbeSaveValue(_In_ WDFKEY Key, _In_ PCWSTR Name, _In_ ULONG Value)
{
    UNICODE_STRING valueName;

    RtlInitUnicodeString(&valueName, Name);
    return WdfRegistryAssignULong(Key, &valueName, Value);
}

static NTSTATUS
SofProbeReadRegisters(_In_ WDFDEVICE Device,
                      _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR HdaMemory,
                      _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR DspMemory)
{
    PUCHAR registers;
    PUCHAR dspRegisters;
    WDFKEY key;
    NTSTATUS status;
    ULONG gcap;
    ULONG vmin;
    ULONG vmaj;
    ULONG gctl;
    ULONG statests;
    ULONG gcap2;
    ULONG llch;
    ULONG capPointer;
    ULONG capHeader;
    ULONG capOffsets[10];
    ULONG capHeaders[10];
    ULONG capCount;
    ULONG nextPointer;
    ULONG previous;
    ULONG ppFound = 0;
    ULONG ppctl = 0;
    ULONG ppsts = 0;
    ULONG adspcs;
    ULONG adspis;
    ULONG hipct;
    ULONG hipci;
    static PCWSTR const capOffsetNames[10] = {
        L"SofProbeCap0Offset", L"SofProbeCap1Offset", L"SofProbeCap2Offset",
        L"SofProbeCap3Offset", L"SofProbeCap4Offset", L"SofProbeCap5Offset",
        L"SofProbeCap6Offset", L"SofProbeCap7Offset", L"SofProbeCap8Offset",
        L"SofProbeCap9Offset"
    };
    static PCWSTR const capHeaderNames[10] = {
        L"SofProbeCap0Header", L"SofProbeCap1Header", L"SofProbeCap2Header",
        L"SofProbeCap3Header", L"SofProbeCap4Header", L"SofProbeCap5Header",
        L"SofProbeCap6Header", L"SofProbeCap7Header", L"SofProbeCap8Header",
        L"SofProbeCap9Header"
    };

    // Match the target's HDA and DSP BAR lengths before accessing MMIO.
    if (HdaMemory->u.Memory.Length != 0x4000 ||
        DspMemory->u.Memory.Length != 0x100000) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    registers = (PUCHAR)MmMapIoSpaceEx(HdaMemory->u.Memory.Start, 0x4000,
                                       PAGE_READONLY | PAGE_NOCACHE);
    if (registers == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Offsets and widths follow Linux include/sound/hda_register.h.
    gcap = READ_REGISTER_USHORT((PUSHORT)(registers + 0x00));
    vmin = READ_REGISTER_UCHAR(registers + 0x02);
    vmaj = READ_REGISTER_UCHAR(registers + 0x03);
    gctl = READ_REGISTER_ULONG((PULONG)(registers + 0x08));
    statests = READ_REGISTER_USHORT((PUSHORT)(registers + 0x0e));
    gcap2 = READ_REGISTER_USHORT((PUSHORT)(registers + 0x12));
    llch = READ_REGISTER_ULONG((PULONG)(registers + 0x14));
    capPointer = llch & 0xffff;
    nextPointer = capPointer;
    for (capCount = 0; capCount < 10 && nextPointer != 0; ++capCount) {
        if (nextPointer < 0x100 || nextPointer > 0x3ffc ||
            (nextPointer & 3) != 0) {
            MmUnmapIoSpace(registers, 0x4000);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        for (previous = 0; previous < capCount; ++previous) {
            if (capOffsets[previous] == nextPointer) {
                MmUnmapIoSpace(registers, 0x4000);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
        }
        capOffsets[capCount] = nextPointer;
        capHeaders[capCount] = READ_REGISTER_ULONG((PULONG)(registers + nextPointer));
        if (((capHeaders[capCount] >> 16) & 0xfff) == 3) {
            if (nextPointer > 0x3ff4) {
                MmUnmapIoSpace(registers, 0x4000);
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            ppFound = 1;
            ppctl = READ_REGISTER_ULONG((PULONG)(registers + nextPointer + 4));
            ppsts = READ_REGISTER_ULONG((PULONG)(registers + nextPointer + 8));
        }
        nextPointer = capHeaders[capCount] & 0xffff;
    }
    if (nextPointer != 0 || capCount == 0) {
        MmUnmapIoSpace(registers, 0x4000);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    capHeader = capHeaders[0];
    MmUnmapIoSpace(registers, 0x4000);

    // BAR4 on the Linux HDA/SOF path: read status only, never power cores on.
    dspRegisters = (PUCHAR)MmMapIoSpaceEx(DspMemory->u.Memory.Start, 0x100,
                                          PAGE_READONLY | PAGE_NOCACHE);
    if (dspRegisters == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    adspcs = READ_REGISTER_ULONG((PULONG)(dspRegisters + 0x04));
    adspis = READ_REGISTER_ULONG((PULONG)(dspRegisters + 0x0c));
    hipct = READ_REGISTER_ULONG((PULONG)(dspRegisters + 0x40));
    hipci = READ_REGISTER_ULONG((PULONG)(dspRegisters + 0x48));
    MmUnmapIoSpace(dspRegisters, 0x100);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "SOFPROBE: GCAP=%04lx version=%lu.%lu GCTL=%08lx "
               "STATESTS=%04lx GCAP2=%04lx LLCH=%08lx\n",
               gcap, vmaj, vmin, gctl, statests, gcap2, llch));
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "SOFPROBE: cap@%04lx=%08lx ADSPCS=%08lx ADSPIS=%08lx "
               "HIPCT=%08lx HIPCI=%08lx\n",
               capPointer, capHeader, adspcs, adspis, hipct, hipci));
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "SOFPROBE: %lu capabilities PP=%lu PPCTL=%08lx PPSTS=%08lx\n",
               capCount, ppFound, ppctl, ppsts));

    status = WdfDeviceOpenRegistryKey(Device, PLUGPLAY_REGKEY_DEVICE,
                                      KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES,
                                      &key);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = SofProbeSaveValue(key, L"SofProbeGcap", gcap);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeVmin", vmin);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeVmaj", vmaj);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeGctl", gctl);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeStatests", statests);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeGcap2", gcap2);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeLlch", llch);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeCapOffset", capPointer);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeCapHeader", capHeader);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeCapCount", capCount);
    for (previous = 0; previous < capCount && NT_SUCCESS(status); ++previous) {
        status = SofProbeSaveValue(key, capOffsetNames[previous], capOffsets[previous]);
        if (NT_SUCCESS(status)) {
            status = SofProbeSaveValue(key, capHeaderNames[previous], capHeaders[previous]);
        }
    }
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbePpFound", ppFound);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbePpCtl", ppctl);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbePpSts", ppsts);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeAdspcs", adspcs);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeAdspis", adspis);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeHipct", hipct);
    if (NT_SUCCESS(status)) status = SofProbeSaveValue(key, L"SofProbeHipci", hipci);
    WdfRegistryClose(key);
    return status;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, SofProbeEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath,
                           WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS
SofProbeEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_PNPPOWER_EVENT_CALLBACKS callbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFDEVICE device;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = SofProbeEvtPrepareHardware;
    callbacks.EvtDeviceReleaseHardware = SofProbeEvtReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SOFPROBE_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = SofProbeEvtIoDeviceControl;
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES,
                              WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) return status;
    return WdfDeviceCreateDeviceInterface(device, &SofProbeInterfaceGuid, NULL);
}

VOID
SofProbeEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PSOFPROBE_DEVICE_CONTEXT context = SofProbeGetContext(device);
    PSOFPROBE_SNAPSHOT result;
    PUCHAR hda = NULL;
    PUCHAR dsp = NULL;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(InputBufferLength);
    if (IoControlCode != IOCTL_SOFPROBE_READ_SNAPSHOT) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
    if (!context->ResourcesReady) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }
    if (OutputBufferLength < sizeof(SOFPROBE_SNAPSHOT)) {
        WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SOFPROBE_SNAPSHOT),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    hda = (PUCHAR)MmMapIoSpaceEx(context->HdaStart, 0x1000,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (hda == NULL) {
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    dsp = (PUCHAR)MmMapIoSpaceEx(context->DspStart, 0x100,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (dsp == NULL) {
        MmUnmapIoSpace(hda, 0x1000);
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    result->Version = 1;
    result->Gctl = READ_REGISTER_ULONG((PULONG)(hda + 0x08));
    result->Ppctl = READ_REGISTER_ULONG((PULONG)(hda + 0x804));
    result->Ppsts = READ_REGISTER_ULONG((PULONG)(hda + 0x808));
    result->Adspcs = READ_REGISTER_ULONG((PULONG)(dsp + 0x04));
    result->Adspis = READ_REGISTER_ULONG((PULONG)(dsp + 0x0c));
    result->Hipct = READ_REGISTER_ULONG((PULONG)(dsp + 0x40));
    result->Hipci = READ_REGISTER_ULONG((PULONG)(dsp + 0x48));
    MmUnmapIoSpace(dsp, 0x100);
    MmUnmapIoSpace(hda, 0x1000);
    WdfRequestSetInformation(Request, sizeof(SOFPROBE_SNAPSHOT));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

NTSTATUS
SofProbeEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    SofProbeGetContext(Device)->ResourcesReady = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
SofProbeEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    ULONG index;
    ULONG count;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR hdaMemory = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR dspMemory = NULL;

    UNREFERENCED_PARAMETER(ResourcesRaw);

    count = WdfCmResourceListGetCount(ResourcesTranslated);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "SOFPROBE: %lu translated PCI resources\n", count));
    for (index = 0; index < count; ++index) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR resource;

        resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, index);
        if (resource == NULL) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        switch (resource->Type) {
        case CmResourceTypeMemory:
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                       "SOFPROBE: memory %lu physical=%08lx:%08lx length=%lu\n",
                       index, resource->u.Memory.Start.HighPart,
                       resource->u.Memory.Start.LowPart,
                       resource->u.Memory.Length));
            if (resource->u.Memory.Length == 0x4000) {
                if (hdaMemory != NULL) {
                    return STATUS_DEVICE_CONFIGURATION_ERROR;
                }
                hdaMemory = resource;
            } else if (resource->u.Memory.Length == 0x100000) {
                if (dspMemory != NULL) {
                    return STATUS_DEVICE_CONFIGURATION_ERROR;
                }
                dspMemory = resource;
            }
            break;
        case CmResourceTypeInterrupt:
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                       "SOFPROBE: interrupt %lu vector=%lu level=%lu flags=%u\n",
                       index, resource->u.Interrupt.Vector,
                       resource->u.Interrupt.Level, resource->Flags));
            break;
        case CmResourceTypePort:
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                       "SOFPROBE: port %lu start=%08lx length=%lu\n",
                       index, resource->u.Port.Start.LowPart,
                       resource->u.Port.Length));
            break;
        default:
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                       "SOFPROBE: resource %lu type=%u\n",
                       index, resource->Type));
            break;
        }
    }
    if (hdaMemory == NULL || dspMemory == NULL) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    {
        NTSTATUS status = SofProbeReadRegisters(Device, hdaMemory, dspMemory);
        if (NT_SUCCESS(status)) {
            PSOFPROBE_DEVICE_CONTEXT context = SofProbeGetContext(Device);
            context->HdaStart = hdaMemory->u.Memory.Start;
            context->DspStart = dspMemory->u.Memory.Start;
            context->ResourcesReady = TRUE;
        }
        return status;
    }
}
