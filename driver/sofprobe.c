// SPDX-License-Identifier: GPL-2.0-only
// First hardware milestone: enumerate assigned PCI resources without touching
// MMIO, DMA, firmware, interrupts, or codec registers.

#include <ntddk.h>
#include <wdf.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofProbeEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofProbeEvtPrepareHardware;

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

    UNREFERENCED_PARAMETER(Device);
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
    return STATUS_SUCCESS;
}
