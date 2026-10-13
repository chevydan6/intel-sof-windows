// SPDX-License-Identifier: GPL-2.0-only
// Read a fixed set of HDA and DSP status registers. No device register writes,
// DMA, firmware, interrupts, or codec access. Values are saved in the device's
// registry key so the target can be inspected without a kernel debugger.

#include <ntddk.h>
#include <wdf.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofProbeEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofProbeEvtPrepareHardware;

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
    ULONG adspcs;
    ULONG adspis;
    ULONG hipct;
    ULONG hipci;

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
    if (capPointer < 0x100 || capPointer > 0x3ffc ||
        (capPointer & 3) != 0) {
        MmUnmapIoSpace(registers, 0x4000);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    capHeader = READ_REGISTER_ULONG((PULONG)(registers + capPointer));
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
    WDFDEVICE device;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = SofProbeEvtPrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &callbacks);

    return WdfDeviceCreate(&DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
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
    return SofProbeReadRegisters(Device, hdaMemory, dspMemory);
}
