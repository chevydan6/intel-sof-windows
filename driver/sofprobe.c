// SPDX-License-Identifier: GPL-2.0-only
// Development probe for bounded HDA/DSP observations and opt-in tests.
// Hardware tests are opt-in and one-shot, with bounded waits and restoration.

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <wdmguid.h>
#include "sofprobe-ioctl.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofProbeEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofProbeEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE SofProbeEvtReleaseHardware;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL SofProbeEvtIoDeviceControl;

typedef struct _SOFPROBE_DEVICE_CONTEXT {
    PHYSICAL_ADDRESS HdaStart;
    PHYSICAL_ADDRESS DspStart;
    WDFDMAENABLER DmaEnabler;
    WDFCOMMONBUFFER FirmwareBdl;
    WDFCOMMONBUFFER FirmwarePayload;
    BOOLEAN ResourcesReady;
    BOOLEAN PowerPulseAttempted;
    BOOLEAN StreamSetupAttempted;
    BOOLEAN CaptureSetupAttempted;
    BOOLEAN CaptureDmaAttempted;
    BOOLEAN RomEntryAttempted;
    BOOLEAN UnstoppedFirmwareDma;
    WDFCOMMONBUFFER UnstoppedCaptureBdl;
    WDFCOMMONBUFFER UnstoppedCapturePayload;
} SOFPROBE_DEVICE_CONTEXT, *PSOFPROBE_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOFPROBE_DEVICE_CONTEXT, SofProbeGetContext)

// {F6B7AB70-381C-4A67-A85C-4A006A7B01DD}
static const GUID SofProbeInterfaceGuid =
    { 0xf6b7ab70, 0x381c, 0x4a67, { 0xa8, 0x5c, 0x4a, 0x00, 0x6a, 0x7b, 0x01, 0xdd } };

typedef struct _SOFPROBE_BDL_ENTRY {
    ULONGLONG Address;
    ULONG Length;
    ULONG Flags;
} SOFPROBE_BDL_ENTRY;
C_ASSERT(sizeof(SOFPROBE_BDL_ENTRY) == 16);

static VOID
SofProbeTestDmaAllocation(_In_ WDFREQUEST Request,
                          _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                          _In_ size_t OutputBufferLength,
                          _In_ size_t InputBufferLength)
{
    PSOFPROBE_DMA_ALLOCATION_RESULT result;
    WDFCOMMONBUFFER bdl = NULL;
    WDFCOMMONBUFFER payload = NULL;
    PHYSICAL_ADDRESS bdlAddress;
    PHYSICAL_ADDRESS payloadAddress;
    SOFPROBE_BDL_ENTRY *entry;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    // This IOCTL only tests Windows DMA allocation and BDL layout. It never
    // programs a stream register or asks the hardware to fetch these buffers.
    status = WdfCommonBufferCreate(Context->DmaEnabler, 128,
                                   WDF_NO_OBJECT_ATTRIBUTES, &bdl);
    if (!NT_SUCCESS(status)) goto finish;
    status = WdfCommonBufferCreate(Context->DmaEnabler, 524288,
                                   WDF_NO_OBJECT_ATTRIBUTES, &payload);
    if (!NT_SUCCESS(status)) goto finish;
    bdlAddress = WdfCommonBufferGetAlignedLogicalAddress(bdl);
    payloadAddress = WdfCommonBufferGetAlignedLogicalAddress(payload);
    if ((bdlAddress.QuadPart & 127) != 0 ||
        (payloadAddress.QuadPart & 127) != 0) {
        status = STATUS_DATATYPE_MISALIGNMENT;
        goto finish;
    }
    RtlZeroMemory(WdfCommonBufferGetAlignedVirtualAddress(bdl), 128);
    RtlZeroMemory(WdfCommonBufferGetAlignedVirtualAddress(payload), 524288);
    entry = (SOFPROBE_BDL_ENTRY *)WdfCommonBufferGetAlignedVirtualAddress(bdl);
    entry->Address = (ULONGLONG)payloadAddress.QuadPart;
    entry->Length = 262144;
    entry->Flags = 0;
    entry[1].Address = (ULONGLONG)payloadAddress.QuadPart + 262144;
    entry[1].Length = 262144;
    entry[1].Flags = 0;
    result->Version = 1;
    result->BdlAddressLow = bdlAddress.LowPart;
    result->BdlAddressHigh = bdlAddress.HighPart;
    result->PayloadAddressLow = payloadAddress.LowPart;
    result->PayloadAddressHigh = payloadAddress.HighPart;
    result->BdlBytes = 128;
    result->PayloadBytes = 524288;
    result->BdlEntryBytes = sizeof(*entry);
    WdfRequestSetInformation(Request, sizeof(*result));

finish:
    if (payload != NULL) WdfObjectDelete(payload);
    if (bdl != NULL) WdfObjectDelete(bdl);
    WdfRequestComplete(Request, status);
}

static VOID
SofProbeStageFirmware(_In_ WDFREQUEST Request,
                      _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                      _In_ size_t OutputBufferLength,
                      _In_ size_t InputBufferLength)
{
    typedef struct _IPC3_MANIFEST_HEADER {
        ULONG Magic;
        ULONG FullSize;
        ULONG HeaderSize;
        ULONG Version;
    } IPC3_MANIFEST_HEADER;
    IPC3_MANIFEST_HEADER header;
    PUCHAR image;
    PSOFPROBE_DMA_ALLOCATION_RESULT result;
    WDFCOMMONBUFFER bdl = NULL;
    WDFCOMMONBUFFER payload = NULL;
    PHYSICAL_ADDRESS bdlAddress;
    PHYSICAL_ADDRESS payloadAddress;
    SOFPROBE_BDL_ENTRY *entry;
    NTSTATUS status;

    if (Context->FirmwarePayload != NULL) {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }
    if (InputBufferLength != 525056 ||
        OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength,
                                           (PVOID *)&image, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    RtlCopyMemory(&header, image, sizeof(header));
    if (header.Magic != 0x6e614d58 || header.FullSize != 768 ||
        header.HeaderSize != 16 || header.Version != 0x01000000) {
        WdfRequestComplete(Request, STATUS_INVALID_IMAGE_FORMAT);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    status = WdfCommonBufferCreate(Context->DmaEnabler, 128,
                                   WDF_NO_OBJECT_ATTRIBUTES, &bdl);
    if (!NT_SUCCESS(status)) goto finish;
    status = WdfCommonBufferCreate(Context->DmaEnabler, 524288,
                                   WDF_NO_OBJECT_ATTRIBUTES, &payload);
    if (!NT_SUCCESS(status)) goto finish;
    bdlAddress = WdfCommonBufferGetAlignedLogicalAddress(bdl);
    payloadAddress = WdfCommonBufferGetAlignedLogicalAddress(payload);
    if ((bdlAddress.QuadPart & 127) != 0 ||
        (payloadAddress.QuadPart & 127) != 0) {
        status = STATUS_DATATYPE_MISALIGNMENT;
        goto finish;
    }
    RtlZeroMemory(WdfCommonBufferGetAlignedVirtualAddress(bdl), 128);
    RtlCopyMemory(WdfCommonBufferGetAlignedVirtualAddress(payload),
                  image + header.FullSize, 524288);
    entry = (SOFPROBE_BDL_ENTRY *)WdfCommonBufferGetAlignedVirtualAddress(bdl);
    entry[0].Address = (ULONGLONG)payloadAddress.QuadPart;
    entry[0].Length = 262144;
    entry[0].Flags = 0;
    entry[1].Address = (ULONGLONG)payloadAddress.QuadPart + 262144;
    entry[1].Length = 262144;
    entry[1].Flags = 0;

    // METHOD_BUFFERED uses the same system buffer for input and output, so
    // populate the response only after copying the image into DMA memory.
    result->Version = 1;
    result->BdlAddressLow = bdlAddress.LowPart;
    result->BdlAddressHigh = bdlAddress.HighPart;
    result->PayloadAddressLow = payloadAddress.LowPart;
    result->PayloadAddressHigh = payloadAddress.HighPart;
    result->BdlBytes = 128;
    result->PayloadBytes = 524288;
    result->BdlEntryBytes = sizeof(*entry);
    Context->FirmwareBdl = bdl;
    Context->FirmwarePayload = payload;
    bdl = NULL;
    payload = NULL;
    WdfRequestSetInformation(Request, sizeof(*result));

finish:
    if (payload != NULL) WdfObjectDelete(payload);
    if (bdl != NULL) WdfObjectDelete(bdl);
    WdfRequestComplete(Request, status);
}

static VOID
SofProbeTestStreamSetup(_In_ WDFREQUEST Request,
                        _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                        _In_ size_t OutputBufferLength,
                        _In_ size_t InputBufferLength)
{
    PSOFPROBE_STREAM_SETUP_RESULT result;
    PUCHAR hda = NULL;
    PUCHAR dsp = NULL;
    PULONG control;
    PULONG ppctl;
    ULONG beforeControl;
    ULONG beforePpctl;
    ULONG current;
    ULONG i;
    PHYSICAL_ADDRESS bdlAddress;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    if (Context->FirmwareBdl == NULL || Context->FirmwarePayload == NULL) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }
    if (Context->StreamSetupAttempted) {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    hda = (PUCHAR)MmMapIoSpaceEx(Context->HdaStart, 0x1000,
                                PAGE_READWRITE | PAGE_NOCACHE);
    dsp = (PUCHAR)MmMapIoSpaceEx(Context->DspStart, 0x100,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (hda == NULL || dsp == NULL) {
        if (dsp != NULL) MmUnmapIoSpace(dsp, 0x100);
        if (hda != NULL) MmUnmapIoSpace(hda, 0x1000);
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    control = (PULONG)(hda + 0x160);
    ppctl = (PULONG)(hda + 0x804);
    beforeControl = READ_REGISTER_ULONG(control);
    beforePpctl = READ_REGISTER_ULONG(ppctl);
    if (READ_REGISTER_USHORT((PUSHORT)hda) != 0x9701 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x08)) != 1 ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0x04)) != 0x00000f0f ||
        beforeControl != 0x00040000 || beforePpctl != 0x40000000 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x168)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x172)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x178)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x17c)) != 0) {
        MmUnmapIoSpace(dsp, 0x100);
        MmUnmapIoSpace(hda, 0x1000);
        WdfRequestComplete(Request, STATUS_DEVICE_CONFIGURATION_ERROR);
        return;
    }
    MmUnmapIoSpace(dsp, 0x100);
    bdlAddress = WdfCommonBufferGetAlignedLogicalAddress(Context->FirmwareBdl);
    if ((bdlAddress.QuadPart & 127) != 0) {
        MmUnmapIoSpace(hda, 0x1000);
        WdfRequestComplete(Request, STATUS_DATATYPE_MISALIGNMENT);
        return;
    }
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1;
    result->BeforeControl = beforeControl;
    Context->StreamSetupAttempted = TRUE;

    // Reset only this idle output stream. Never set its RUN bit.
    WRITE_REGISTER_ULONG(control, beforeControl | 1);
    for (i = 0; i < 100; ++i) {
        if (READ_REGISTER_ULONG(control) & 1) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto restore;
    }
    WRITE_REGISTER_ULONG(control, beforeControl);
    KeStallExecutionProcessor(3);
    for (i = 0; i < 100; ++i) {
        if ((READ_REGISTER_ULONG(control) & 1) == 0) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto restore;
    }
    result->ResetAcknowledged = 1;

    current = READ_REGISTER_ULONG(control) & 0x00ffffff;
    WRITE_REGISTER_ULONG(control, (current & ~0x00f00000) | 0x00100000);
    WRITE_REGISTER_ULONG(ppctl, beforePpctl | 0x80);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x168), 524288);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x172), 0x40);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x16c), 1);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x178), bdlAddress.LowPart);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x17c), bdlAddress.HighPart);

    result->ConfiguredControl = READ_REGISTER_ULONG(control) & 0x00ffffff;
    result->ConfiguredCbl = READ_REGISTER_ULONG((PULONG)(hda + 0x168));
    result->ConfiguredLvi = READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c));
    result->ConfiguredFormat = READ_REGISTER_USHORT((PUSHORT)(hda + 0x172));
    result->ConfiguredBdlLow = READ_REGISTER_ULONG((PULONG)(hda + 0x178));
    result->ConfiguredBdlHigh = READ_REGISTER_ULONG((PULONG)(hda + 0x17c));
    result->ConfiguredPpctl = READ_REGISTER_ULONG(ppctl);
    if (result->ConfiguredControl != 0x00140000 ||
        result->ConfiguredCbl != 524288 || result->ConfiguredLvi != 1 ||
        result->ConfiguredFormat != 0x40 ||
        result->ConfiguredBdlLow != bdlAddress.LowPart ||
        result->ConfiguredBdlHigh != (ULONG)bdlAddress.HighPart ||
        result->ConfiguredPpctl != (beforePpctl | 0x80)) {
        result->Result = 2;
    }

restore:
    WRITE_REGISTER_ULONG(ppctl, beforePpctl);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x178), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x17c), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x16c), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x172), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x168), 0);
    WRITE_REGISTER_ULONG(control, beforeControl);
    result->RestoredControl = READ_REGISTER_ULONG(control) & 0x00ffffff;
    result->RestoredCbl = READ_REGISTER_ULONG((PULONG)(hda + 0x168));
    result->RestoredLvi = READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c));
    result->RestoredFormat = READ_REGISTER_USHORT((PUSHORT)(hda + 0x172));
    result->RestoredBdlLow = READ_REGISTER_ULONG((PULONG)(hda + 0x178));
    if (result->RestoredControl != beforeControl ||
        result->RestoredCbl != 0 || result->RestoredLvi != 0 ||
        result->RestoredFormat != 0 || result->RestoredBdlLow != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x17c)) != 0 ||
        READ_REGISTER_ULONG(ppctl) != beforePpctl) {
        result->Result = 3;
    }
    MmUnmapIoSpace(hda, 0x1000);
    WdfRequestSetInformation(Request, sizeof(*result));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

static VOID
SofProbeTestRomEntry(_In_ WDFREQUEST Request,
                     _In_ WDFDEVICE Device,
                     _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                     _In_ size_t OutputBufferLength,
                     _In_ size_t InputBufferLength,
                     _In_ BOOLEAN RunFirmwareDma,
                     _In_ BOOLEAN CaptureIpc3Ready,
                     _In_ BOOLEAN CaptureIpc3Info,
                     _In_ BOOLEAN AcknowledgeIpc3Ready,
                     _In_ BOOLEAN QueryIpc3Memory)
{
    PSOFPROBE_ROM_ENTRY_RESULT result;
    PSOFPROBE_FIRMWARE_DMA_RESULT dmaResult = NULL;
    PSOFPROBE_IPC3_READY_RESULT readyResult = NULL;
    PSOFPROBE_IPC3_INFO_RESULT infoResult = NULL;
    PSOFPROBE_IPC3_ACK_RESULT ackResult = NULL;
    PSOFPROBE_IPC3_MEMORY_RESULT memoryResult = NULL;
    PUCHAR hda = NULL;
    PUCHAR dsp = NULL;
    PULONG outputControl;
    PULONG ppctl;
    PULONG adspcs;
    PULONG hipcidr;
    PULONG hipcida;
    PULONG spibControl;
    PULONG spibValue;
    ULONG beforeOutputControl;
    ULONG beforePpctl;
    ULONG current;
    ULONG i;
    PHYSICAL_ADDRESS bdlAddress;
    BUS_INTERFACE_STANDARD bus;
    UCHAR pciConfig[6];
    ULONG vendorDevice;
    USHORT pciCommand;
    LARGE_INTEGER delay;
    BOOLEAN powerRequested = FALSE;
    BOOLEAN sspTouched = FALSE;
    BOOLEAN dmaStarted = FALSE;
    BOOLEAN spibTouched = FALSE;
    BOOLEAN fwEntered = FALSE;
    ULONG sspUnavailableMask = 0;
    ULONGLONG firmwareDeadline;
    size_t dspMapBytes = QueryIpc3Memory ? 0x100000
                         : (CaptureIpc3Ready ? 0x82000 : 0x81000);
    size_t resultBytes = QueryIpc3Memory ? sizeof(SOFPROBE_IPC3_MEMORY_RESULT)
                       : AcknowledgeIpc3Ready ? sizeof(SOFPROBE_IPC3_ACK_RESULT)
                       : CaptureIpc3Info ? sizeof(SOFPROBE_IPC3_INFO_RESULT)
                       : CaptureIpc3Ready ? sizeof(SOFPROBE_IPC3_READY_RESULT)
                       : (RunFirmwareDma ? sizeof(SOFPROBE_FIRMWARE_DMA_RESULT)
                                         : sizeof(SOFPROBE_ROM_ENTRY_RESULT));
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < resultBytes) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    if (Context->RomEntryAttempted) {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }
    if (Context->FirmwareBdl == NULL || Context->FirmwarePayload == NULL) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, resultBytes,
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) goto finish;
    if (RunFirmwareDma) dmaResult = (PSOFPROBE_FIRMWARE_DMA_RESULT)result;
    if (CaptureIpc3Ready) readyResult = (PSOFPROBE_IPC3_READY_RESULT)result;
    if (CaptureIpc3Info) infoResult = (PSOFPROBE_IPC3_INFO_RESULT)result;
    if (AcknowledgeIpc3Ready) ackResult = (PSOFPROBE_IPC3_ACK_RESULT)result;
    if (QueryIpc3Memory) memoryResult = (PSOFPROBE_IPC3_MEMORY_RESULT)result;
    if (RunFirmwareDma) {
        RtlZeroMemory(&bus, sizeof(bus));
        status = WdfFdoQueryForInterface(Device, &GUID_BUS_INTERFACE_STANDARD,
                                         (PINTERFACE)&bus, sizeof(bus), 1, NULL);
        if (!NT_SUCCESS(status)) goto finish;
        RtlZeroMemory(pciConfig, sizeof(pciConfig));
        i = bus.GetBusData(bus.Context, PCI_WHICHSPACE_CONFIG,
                           pciConfig, 0, sizeof(pciConfig));
        bus.InterfaceDereference(bus.Context);
        if (i != sizeof(pciConfig)) {
            status = STATUS_DEVICE_DATA_ERROR;
            goto finish;
        }
        RtlCopyMemory(&vendorDevice, pciConfig, sizeof(vendorDevice));
        RtlCopyMemory(&pciCommand, pciConfig + 4, sizeof(pciCommand));
        if (vendorDevice != 0x54c88086 || (pciCommand & 6) != 6) {
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto finish;
        }
    }
    bdlAddress = WdfCommonBufferGetAlignedLogicalAddress(Context->FirmwareBdl);
    if ((bdlAddress.QuadPart & 127) != 0) {
        status = STATUS_DATATYPE_MISALIGNMENT;
        goto finish;
    }
    hda = (PUCHAR)MmMapIoSpaceEx(Context->HdaStart, 0x1000,
                                PAGE_READWRITE | PAGE_NOCACHE);
    dsp = (PUCHAR)MmMapIoSpaceEx(Context->DspStart, dspMapBytes,
                                PAGE_READWRITE | PAGE_NOCACHE);
    if (hda == NULL || dsp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto finish;
    }
    outputControl = (PULONG)(hda + 0x160);
    ppctl = (PULONG)(hda + 0x804);
    adspcs = (PULONG)(dsp + 0x04);
    hipcidr = (PULONG)(dsp + 0xd0);
    hipcida = (PULONG)(dsp + 0xd4);
    // GCAP 0x9701 exposes seven capture streams, so output stream 0 is
    // global stream index 7. SPIB capability is at 0x700 on this N100.
    spibControl = (PULONG)(hda + 0x704);
    spibValue = (PULONG)(hda + 0x740);
    beforeOutputControl = READ_REGISTER_ULONG(outputControl);
    beforePpctl = READ_REGISTER_ULONG(ppctl);
    if (READ_REGISTER_USHORT((PUSHORT)hda) != 0x9701 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x08)) != 1 ||
        beforeOutputControl != 0x00040000 || beforePpctl != 0x40000000 ||
        READ_REGISTER_UCHAR(hda + 0x163) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x168)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x172)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x178)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x17c)) != 0 ||
        READ_REGISTER_ULONG(adspcs) != 0x00000f0f ||
        READ_REGISTER_ULONG(hipcidr) != 0 ||
        READ_REGISTER_ULONG(hipcida) != 0 ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0xe8)) != 0 ||
        (CaptureIpc3Ready &&
         (READ_REGISTER_ULONG((PULONG)(dsp + 0xc0)) != 0 ||
          READ_REGISTER_ULONG((PULONG)(dsp + 0xc4)) != 0)) ||
        (RunFirmwareDma &&
         ((READ_REGISTER_ULONG(spibControl) & 0x80) != 0 ||
          READ_REGISTER_ULONG(spibValue) != 0))) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto finish;
    }
    RtlZeroMemory(result, resultBytes);
    result->Version = 1;
    result->BeforeAdspcs = READ_REGISTER_ULONG(adspcs);
    result->RomControl = 0x81004000;
    Context->RomEntryAttempted = TRUE;

    // Set up the staged code-loader BDL while the stream is stopped.
    WRITE_REGISTER_ULONG(outputControl, beforeOutputControl | 1);
    for (i = 0; i < 100; ++i) {
        if (READ_REGISTER_ULONG(outputControl) & 1) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto cleanup;
    }
    WRITE_REGISTER_ULONG(outputControl, beforeOutputControl);
    KeStallExecutionProcessor(3);
    for (i = 0; i < 100; ++i) {
        if ((READ_REGISTER_ULONG(outputControl) & 1) == 0) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto cleanup;
    }
    current = READ_REGISTER_ULONG(outputControl) & 0x00ffffff;
    WRITE_REGISTER_ULONG(outputControl,
                         (current & ~0x00f00000) | 0x00100000);
    WRITE_REGISTER_ULONG(ppctl, beforePpctl | 0x80);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x178), bdlAddress.LowPart);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x17c), bdlAddress.HighPart);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x168), 524288);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x16c), 1);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x172), 0x40);
    result->Reserved = READ_REGISTER_ULONG(outputControl) & 0x00ffffff;
    if (result->Reserved != 0x00140000 ||
        READ_REGISTER_ULONG(ppctl) != (beforePpctl | 0x80) ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x178)) != bdlAddress.LowPart ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x17c)) != (ULONG)bdlAddress.HighPart ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x168)) != 524288 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c)) != 1 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x172)) != 0x40) {
        result->Result = 2;
        goto cleanup;
    }
    if (RunFirmwareDma) {
        dmaResult->SpibControlBefore = READ_REGISTER_ULONG(spibControl);
        dmaResult->SpibValueBefore = READ_REGISTER_ULONG(spibValue);
        spibTouched = TRUE;
        WRITE_REGISTER_ULONG(spibControl,
                             dmaResult->SpibControlBefore | 0x80);
        WRITE_REGISTER_ULONG(spibValue, 524288);
        dmaResult->SpibControlConfigured = READ_REGISTER_ULONG(spibControl);
        dmaResult->SpibValueConfigured = READ_REGISTER_ULONG(spibValue);
        if (dmaResult->SpibControlConfigured !=
                (dmaResult->SpibControlBefore | 0x80) ||
            dmaResult->SpibValueConfigured != 524288) {
            result->Result = 13;
            goto cleanup;
        }
    }

    // Follow the Alder Lake-N Linux ROM ordering: power, ROM IPC, release
    // reset, then release stall. The SSP setting is restored before powerdown.
    powerRequested = TRUE;
    WRITE_REGISTER_ULONG(adspcs, READ_REGISTER_ULONG(adspcs) | 0x00010000);
    delay.QuadPart = -10000; // 1 ms between bounded polls
    for (i = 0; i < 50; ++i) {
        if (READ_REGISTER_ULONG(adspcs) & 0x01000000) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    result->PoweredAdspcs = READ_REGISTER_ULONG(adspcs);
    if (i == 50) {
        result->Result = 3;
        goto cleanup;
    }
    result->Ssp0Before = READ_REGISTER_ULONG((PULONG)(dsp + 0x10004));
    result->Ssp1Before = READ_REGISTER_ULONG((PULONG)(dsp + 0x11004));
    result->Ssp2Before = READ_REGISTER_ULONG((PULONG)(dsp + 0x12004));
    sspTouched = TRUE;
    WRITE_REGISTER_ULONG((PULONG)(dsp + 0x10004),
                         result->Ssp0Before | 0x03000000);
    WRITE_REGISTER_ULONG((PULONG)(dsp + 0x11004),
                         result->Ssp1Before | 0x03000000);
    WRITE_REGISTER_ULONG((PULONG)(dsp + 0x12004),
                         result->Ssp2Before | 0x03000000);
    WRITE_REGISTER_ULONG(hipcidr, result->RomControl);
    WRITE_REGISTER_ULONG(adspcs, READ_REGISTER_ULONG(adspcs) & ~1UL);
    for (i = 0; i < 50; ++i) {
        if ((READ_REGISTER_ULONG(adspcs) & 1) == 0) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    result->ResetReleasedAdspcs = READ_REGISTER_ULONG(adspcs);
    if (i == 50) {
        result->Result = 4;
        goto cleanup;
    }
    WRITE_REGISTER_ULONG(adspcs, READ_REGISTER_ULONG(adspcs) & ~0x100UL);
    for (i = 0; i < 50; ++i) {
        current = READ_REGISTER_ULONG(adspcs);
        if ((current & 0x01010000) == 0x01010000 &&
            (current & 0x00000101) == 0) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    result->RunningAdspcs = READ_REGISTER_ULONG(adspcs);
    if (i == 50) {
        result->Result = 5;
        goto cleanup;
    }
    for (i = 0; i < 500; ++i) {
        current = READ_REGISTER_ULONG(hipcida);
        if (current & 0x80000000) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    result->IpcAck = READ_REGISTER_ULONG(hipcida);
    result->AckPolls = i;
    if (i == 500) {
        result->Result = 6;
        goto cleanup;
    }
    WRITE_REGISTER_ULONG(hipcida, 0x80000000); // W1C ROM IPC DONE
    for (i = 0; i < 300; ++i) {
        current = READ_REGISTER_ULONG((PULONG)(dsp + 0x80000));
        if ((current & 0x00ffffff) == 1) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    result->RomPolls = i;
    result->RomStatus = READ_REGISTER_ULONG((PULONG)(dsp + 0x80000));
    result->RomError = READ_REGISTER_ULONG((PULONG)(dsp + 0x80004));
    if (i == 300) result->Result = 7;

    if (RunFirmwareDma && result->Result == 0) {
        // The staged WDF common buffer stays alive for the whole transfer.
        // ROM initialization has been observed before output DMA can start.
        KeMemoryBarrier();
        WRITE_REGISTER_ULONG(outputControl, 0x0014001e);
        dmaStarted = TRUE;
        for (i = 0; i < 100; ++i) {
            if (READ_REGISTER_ULONG(outputControl) & 2) break;
            KeStallExecutionProcessor(5);
        }
        dmaResult->RunningOutputControl = READ_REGISTER_ULONG(outputControl);
        if (i == 100) result->Result = 9;
        if (result->Result == 0) {
            // Linux allows up to 3 seconds for base firmware to enter.
            firmwareDeadline = KeQueryInterruptTime() + 30000000ULL;
            for (i = 0; KeQueryInterruptTime() < firmwareDeadline; ++i) {
                current = READ_REGISTER_ULONG((PULONG)(dsp + 0x80000));
                if ((current & 0x00ffffff) == 5) {
                    fwEntered = TRUE;
                    break;
                }
                if (READ_REGISTER_UCHAR(hda + 0x163) & 0x18) {
                    result->Result = 10;
                    break;
                }
                (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
            }
            dmaResult->FirmwarePolls = i;
            dmaResult->FirmwareStatus = READ_REGISTER_ULONG((PULONG)(dsp + 0x80000));
            dmaResult->FirmwareError = READ_REGISTER_ULONG((PULONG)(dsp + 0x80004));
            if (!fwEntered && result->Result == 0) result->Result = 11;
        }
        if (CaptureIpc3Ready && result->Result == 0) {
            // Observe the firmware-initiated IPC3 READY doorbell and uplink
            // mailbox without enabling an interrupt for which this probe
            // does not yet have an ISR.
            firmwareDeadline = KeQueryInterruptTime() + 10000000ULL;
            for (i = 0; KeQueryInterruptTime() < firmwareDeadline; ++i) {
                current = READ_REGISTER_ULONG((PULONG)(dsp + 0xc0));
                if (current & 0x80000000) break;
                (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
            }
            readyResult->ReadyPolls = i;
            readyResult->Hipctdr = READ_REGISTER_ULONG((PULONG)(dsp + 0xc0));
            readyResult->Hipctdd = READ_REGISTER_ULONG((PULONG)(dsp + 0xc8));
            readyResult->Hipctda = READ_REGISTER_ULONG((PULONG)(dsp + 0xc4));
            if ((readyResult->Hipctdr & 0x80000000) == 0) {
                result->Result = 14;
            } else {
                readyResult->MailboxSize =
                    READ_REGISTER_ULONG((PULONG)(dsp + 0x81000));
                readyResult->MailboxCommand =
                    READ_REGISTER_ULONG((PULONG)(dsp + 0x81004));
                if (readyResult->MailboxSize < 8 ||
                    readyResult->MailboxSize > 4096 ||
                    (readyResult->MailboxCommand & 0xf0000000) != 0x70000000) {
                    result->Result = 15;
                } else if (CaptureIpc3Info) {
                    if (readyResult->MailboxSize != sizeof(infoResult->ReadyWords)) {
                        result->Result = 16;
                    } else {
                        for (i = 0; i < RTL_NUMBER_OF(infoResult->ReadyWords); ++i)
                            infoResult->ReadyWords[i] = READ_REGISTER_ULONG(
                                (PULONG)(dsp + 0x81000 + i * sizeof(ULONG)));
                    }
                }
            }
            if (AcknowledgeIpc3Ready && result->Result == 0) {
                // The FW_READY mailbox has been copied. Follow the CNL
                // host-done sequence: clear the DSP busy doorbell, then
                // signal DONE. This stays within the one-shot boot test.
                WRITE_REGISTER_ULONG((PULONG)(dsp + 0xc0),
                                     readyResult->Hipctdr | 0x80000000);
                WRITE_REGISTER_ULONG((PULONG)(dsp + 0xc4), 0x80000000);
                for (i = 0; i < 50; ++i) {
                    if ((READ_REGISTER_ULONG((PULONG)(dsp + 0xc0)) &
                         0x80000000) == 0) break;
                    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
                }
                ackResult->AckPolls = i;
                ackResult->HipctdrAfterAck =
                    READ_REGISTER_ULONG((PULONG)(dsp + 0xc0));
                ackResult->HipctdaAfterAck =
                    READ_REGISTER_ULONG((PULONG)(dsp + 0xc4));
                if (i == 50) result->Result = 17;
            }
            if (QueryIpc3Memory && result->Result == 0) {
                // The validated XMan window descriptor places the IPC3
                // downbox at DSP BAR + 0xa0000 (8 KiB). Ask for a read-only
                // memory-usage report with an eight-byte command header.
                WRITE_REGISTER_ULONG((PULONG)(dsp + 0xa0000), 8);
                WRITE_REGISTER_ULONG((PULONG)(dsp + 0xa0004), 0xd0010000);
                KeMemoryBarrier();
                WRITE_REGISTER_ULONG(hipcidr, 0x80000000);
                for (i = 0; i < 500; ++i) {
                    current = READ_REGISTER_ULONG(hipcida);
                    if (current & 0x80000000) break;
                    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
                }
                memoryResult->ReplyPolls = i;
                memoryResult->ReplyDoorbell = READ_REGISTER_ULONG(hipcida);
                if (i == 500) {
                    result->Result = 18;
                } else {
                    memoryResult->ReplySize =
                        READ_REGISTER_ULONG((PULONG)(dsp + 0xa0000));
                    memoryResult->ReplyCommand =
                        READ_REGISTER_ULONG((PULONG)(dsp + 0xa0004));
                    memoryResult->ReplyError =
                        (LONG)READ_REGISTER_ULONG((PULONG)(dsp + 0xa0008));
                    if (memoryResult->ReplySize < 12 ||
                        memoryResult->ReplySize >
                            sizeof(memoryResult->ReplyWords) ||
                        (memoryResult->ReplySize & 3) != 0) {
                        result->Result = 19;
                    } else {
                        for (i = 0; i < memoryResult->ReplySize / sizeof(ULONG); ++i)
                            memoryResult->ReplyWords[i] = READ_REGISTER_ULONG(
                                (PULONG)(dsp + 0xa0000 + i * sizeof(ULONG)));
                    }
                    WRITE_REGISTER_ULONG(hipcida, 0x80000000); // W1C DONE
                }
            }
        }
        dmaResult->OutputPosition = READ_REGISTER_ULONG((PULONG)(hda + 0x164));
        dmaResult->OutputStatus = READ_REGISTER_UCHAR(hda + 0x163);
    }

cleanup:
    if (dmaStarted) {
        WRITE_REGISTER_UCHAR(hda + 0x160, 0);
        for (i = 0; i < 300; ++i) {
            if ((READ_REGISTER_UCHAR(hda + 0x160) & 2) == 0) break;
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        }
        dmaResult->StopPolls = i;
        dmaResult->StoppedOutputControl = READ_REGISTER_ULONG(outputControl);
        if (i == 300) {
            // Do not detach the BDL or free memory that hardware may own.
            Context->UnstoppedFirmwareDma = TRUE;
            result->Result = 12;
            WdfRequestSetInformation(Request, resultBytes);
            status = STATUS_SUCCESS;
            goto finish;
        }
        WRITE_REGISTER_UCHAR(hda + 0x163, 0x1c);
    }
    if (spibTouched) {
        WRITE_REGISTER_ULONG(spibValue, dmaResult->SpibValueBefore);
        WRITE_REGISTER_ULONG(spibControl, dmaResult->SpibControlBefore);
        dmaResult->SpibValueRestored = READ_REGISTER_ULONG(spibValue);
        dmaResult->SpibControlRestored = READ_REGISTER_ULONG(spibControl);
    }
    if (powerRequested) {
        if (result->RomStatus == 0 && result->Result != 0 &&
            (result->RunningAdspcs & 0x01000000) != 0) {
            result->RomStatus = READ_REGISTER_ULONG((PULONG)(dsp + 0x80000));
            result->RomError = READ_REGISTER_ULONG((PULONG)(dsp + 0x80004));
        }
        WRITE_REGISTER_ULONG(adspcs, READ_REGISTER_ULONG(adspcs) | 0x00000101);
        for (i = 0; i < 50; ++i) {
            if ((READ_REGISTER_ULONG(adspcs) & 0x00000101) == 0x00000101) break;
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        }
        // Firmware can update SSP settings while running. Restore them only
        // after the core is stalled and reset, while its MMIO is powered.
        if (sspTouched) {
            current = READ_REGISTER_ULONG((PULONG)(dsp + 0x10004));
            if (current == 0xffffffff) {
                sspUnavailableMask |= 1;
            } else {
                WRITE_REGISTER_ULONG((PULONG)(dsp + 0x10004),
                                     (current & ~0x03000000) |
                                     (result->Ssp0Before & 0x03000000));
            }
            current = READ_REGISTER_ULONG((PULONG)(dsp + 0x11004));
            if (current == 0xffffffff) {
                sspUnavailableMask |= 2;
            } else {
                WRITE_REGISTER_ULONG((PULONG)(dsp + 0x11004),
                                     (current & ~0x03000000) |
                                     (result->Ssp1Before & 0x03000000));
            }
            current = READ_REGISTER_ULONG((PULONG)(dsp + 0x12004));
            if (current == 0xffffffff) {
                sspUnavailableMask |= 4;
            } else {
                WRITE_REGISTER_ULONG((PULONG)(dsp + 0x12004),
                                     (current & ~0x03000000) |
                                     (result->Ssp2Before & 0x03000000));
            }
            result->Ssp0After = READ_REGISTER_ULONG((PULONG)(dsp + 0x10004));
            result->Ssp1After = READ_REGISTER_ULONG((PULONG)(dsp + 0x11004));
            result->Ssp2After = READ_REGISTER_ULONG((PULONG)(dsp + 0x12004));
        }
        WRITE_REGISTER_ULONG(adspcs,
                             READ_REGISTER_ULONG(adspcs) & ~0x00010000UL);
        for (i = 0; i < 50; ++i) {
            if ((READ_REGISTER_ULONG(adspcs) & 0x01000000) == 0) break;
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        }
    }
    WRITE_REGISTER_ULONG(ppctl, beforePpctl);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x178), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x17c), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x168), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x16c), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x172), 0);
    WRITE_REGISTER_ULONG(outputControl, beforeOutputControl);
    result->RestoredAdspcs = READ_REGISTER_ULONG(adspcs);
    result->RestoredOutputControl = READ_REGISTER_ULONG(outputControl);
    result->RestoredPpctl = READ_REGISTER_ULONG(ppctl);
    result->RestoredHipcidr = READ_REGISTER_ULONG(hipcidr);
    result->RestoredHipcida = READ_REGISTER_ULONG(hipcida);
    if (CaptureIpc3Ready) {
        readyResult->SspUnavailableMask = sspUnavailableMask;
        readyResult->HipctdrAfterCleanup =
            READ_REGISTER_ULONG((PULONG)(dsp + 0xc0));
        readyResult->HipctdaAfterCleanup =
            READ_REGISTER_ULONG((PULONG)(dsp + 0xc4));
        if (result->RestoredAdspcs != result->BeforeAdspcs)
            readyResult->CleanupFlags |= 1;
        if (result->RestoredOutputControl != beforeOutputControl)
            readyResult->CleanupFlags |= 2;
        if (result->RestoredPpctl != beforePpctl)
            readyResult->CleanupFlags |= 4;
        if (READ_REGISTER_ULONG((PULONG)(hda + 0x178)) != 0 ||
            READ_REGISTER_ULONG((PULONG)(hda + 0x17c)) != 0 ||
            READ_REGISTER_ULONG((PULONG)(hda + 0x168)) != 0 ||
            READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c)) != 0 ||
            READ_REGISTER_USHORT((PUSHORT)(hda + 0x172)) != 0)
            readyResult->CleanupFlags |= 8;
        if (dmaResult->SpibValueRestored != dmaResult->SpibValueBefore ||
            dmaResult->SpibControlRestored != dmaResult->SpibControlBefore)
            readyResult->CleanupFlags |= 16;
        if (((sspUnavailableMask & 1) == 0 &&
             ((result->Ssp0After ^ result->Ssp0Before) & 0x03000000) != 0) ||
            ((sspUnavailableMask & 2) == 0 &&
             ((result->Ssp1After ^ result->Ssp1Before) & 0x03000000) != 0) ||
            ((sspUnavailableMask & 4) == 0 &&
             ((result->Ssp2After ^ result->Ssp2Before) & 0x03000000) != 0))
            readyResult->CleanupFlags |= 32;
    }
    if (result->RestoredAdspcs != result->BeforeAdspcs ||
        result->RestoredOutputControl != beforeOutputControl ||
        result->RestoredPpctl != beforePpctl ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x178)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x17c)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x168)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x172)) != 0 ||
        (spibTouched &&
         (dmaResult->SpibValueRestored != dmaResult->SpibValueBefore ||
          dmaResult->SpibControlRestored != dmaResult->SpibControlBefore)) ||
        (sspTouched &&
         (((sspUnavailableMask & 1) == 0 &&
           ((result->Ssp0After ^ result->Ssp0Before) & 0x03000000) != 0) ||
          ((sspUnavailableMask & 2) == 0 &&
           ((result->Ssp1After ^ result->Ssp1Before) & 0x03000000) != 0) ||
          ((sspUnavailableMask & 4) == 0 &&
           ((result->Ssp2After ^ result->Ssp2Before) & 0x03000000) != 0)))) {
        result->Result = 8;
    }
    WdfRequestSetInformation(Request, resultBytes);
    status = STATUS_SUCCESS;

finish:
    if (dsp != NULL) MmUnmapIoSpace(dsp, dspMapBytes);
    if (hda != NULL) MmUnmapIoSpace(hda, 0x1000);
    WdfRequestComplete(Request, status);
}

static VOID
SofProbeTestCaptureDma(_In_ WDFREQUEST Request,
                       _In_ WDFDEVICE Device,
                       _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                       _In_ size_t OutputBufferLength,
                       _In_ size_t InputBufferLength)
{
    PSOFPROBE_CAPTURE_DMA_RESULT result;
    WDFCOMMONBUFFER bdl = NULL;
    WDFCOMMONBUFFER payload = NULL;
    BUS_INTERFACE_STANDARD bus;
    UCHAR pciConfig[8];
    PUCHAR hda = NULL;
    PUCHAR dsp = NULL;
    PHYSICAL_ADDRESS bdlAddress;
    PHYSICAL_ADDRESS payloadAddress;
    SOFPROBE_BDL_ENTRY *entry;
    ULONG beforeControl;
    ULONG beforePpctl;
    ULONG vendorDevice;
    UCHAR beforeLtrp;
    ULONG current;
    ULONG i;
    LARGE_INTEGER delay;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    if (Context->CaptureDmaAttempted || Context->UnstoppedCaptureBdl != NULL) {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) goto finish;
    RtlZeroMemory(&bus, sizeof(bus));
    status = WdfFdoQueryForInterface(Device, &GUID_BUS_INTERFACE_STANDARD,
                                     (PINTERFACE)&bus, sizeof(bus), 1, NULL);
    if (!NT_SUCCESS(status)) goto finish;
    RtlZeroMemory(pciConfig, sizeof(pciConfig));
    i = bus.GetBusData(bus.Context, PCI_WHICHSPACE_CONFIG,
                       pciConfig, 0, sizeof(pciConfig));
    bus.InterfaceDereference(bus.Context);
    if (i != sizeof(pciConfig)) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto finish;
    }
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1;
    RtlCopyMemory(&vendorDevice, pciConfig, sizeof(vendorDevice));
    RtlCopyMemory(&result->PciCommand, pciConfig + 4, sizeof(USHORT));
    if (vendorDevice != 0x54c88086 ||
        (result->PciCommand & 6) != 6) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto finish;
    }
    status = WdfCommonBufferCreate(Context->DmaEnabler, 128,
                                   WDF_NO_OBJECT_ATTRIBUTES, &bdl);
    if (!NT_SUCCESS(status)) goto finish;
    status = WdfCommonBufferCreate(Context->DmaEnabler, 4096,
                                   WDF_NO_OBJECT_ATTRIBUTES, &payload);
    if (!NT_SUCCESS(status)) goto finish;
    bdlAddress = WdfCommonBufferGetAlignedLogicalAddress(bdl);
    payloadAddress = WdfCommonBufferGetAlignedLogicalAddress(payload);
    if ((bdlAddress.QuadPart & 127) != 0 ||
        (payloadAddress.QuadPart & 127) != 0) {
        status = STATUS_DATATYPE_MISALIGNMENT;
        goto finish;
    }
    RtlZeroMemory(WdfCommonBufferGetAlignedVirtualAddress(bdl), 128);
    RtlZeroMemory(WdfCommonBufferGetAlignedVirtualAddress(payload), 4096);
    entry = (SOFPROBE_BDL_ENTRY *)WdfCommonBufferGetAlignedVirtualAddress(bdl);
    entry[0].Address = (ULONGLONG)payloadAddress.QuadPart;
    entry[0].Length = 2048;
    entry[1].Address = (ULONGLONG)payloadAddress.QuadPart + 2048;
    entry[1].Length = 2048;

    hda = (PUCHAR)MmMapIoSpaceEx(Context->HdaStart, 0x2000,
                                PAGE_READWRITE | PAGE_NOCACHE);
    dsp = (PUCHAR)MmMapIoSpaceEx(Context->DspStart, 0x100,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (hda == NULL || dsp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto finish;
    }
    beforeControl = READ_REGISTER_ULONG((PULONG)(hda + 0x80));
    beforePpctl = READ_REGISTER_ULONG((PULONG)(hda + 0x804));
    beforeLtrp = READ_REGISTER_UCHAR(hda + 0x1048);
    if (READ_REGISTER_USHORT((PUSHORT)hda) != 0x9701 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x08)) != 1 ||
        beforeControl != 0x00040000 || beforePpctl != 0x40000000 ||
        beforeLtrp != 0xa9 ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0x04)) != 0x00000f0f ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0xd0)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0xd4)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x88)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x92)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x98)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x9c)) != 0) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto finish;
    }
    Context->CaptureDmaAttempted = TRUE;
    result->BeforeControl = beforeControl;
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80), beforeControl | 1);
    for (i = 0; i < 100; ++i) {
        if (READ_REGISTER_ULONG((PULONG)(hda + 0x80)) & 1) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto restore;
    }
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80), beforeControl);
    KeStallExecutionProcessor(3);
    for (i = 0; i < 100; ++i) {
        if ((READ_REGISTER_ULONG((PULONG)(hda + 0x80)) & 1) == 0) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto restore;
    }
    current = READ_REGISTER_ULONG((PULONG)(hda + 0x80)) & 0x00ffffff;
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80),
                         (current & ~0x00f00000) | 0x00100000);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x804), beforePpctl | 1);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x98), bdlAddress.LowPart);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x9c), bdlAddress.HighPart);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x88), 4096);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x8c), 1);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x92), 0x40);
    WRITE_REGISTER_UCHAR(hda + 0x1048, (beforeLtrp & 0xc0) | 0x1f);
    result->ConfiguredControl = READ_REGISTER_ULONG((PULONG)(hda + 0x80)) & 0x00ffffff;
    result->ConfiguredPpctl = READ_REGISTER_ULONG((PULONG)(hda + 0x804));
    if (result->ConfiguredControl != 0x00140000 ||
        result->ConfiguredPpctl != (beforePpctl | 1) ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x98)) != bdlAddress.LowPart ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x9c)) != (ULONG)bdlAddress.HighPart ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x88)) != 4096 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c)) != 1 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x92)) != 0x40 ||
        READ_REGISTER_UCHAR(hda + 0x1048) != 0x9f) {
        result->Result = 2;
        goto restore;
    }

    KeMemoryBarrier();
    WRITE_REGISTER_UCHAR(hda + 0x80, 2);
    for (i = 0; i < 100; ++i) {
        if (READ_REGISTER_UCHAR(hda + 0x80) & 2) break;
        KeStallExecutionProcessor(5);
    }
    result->RunningControl = READ_REGISTER_ULONG((PULONG)(hda + 0x80));
    if (i == 100) result->Result = 3;
    if (result->Result == 0) {
        delay.QuadPart = -20000; // 2 ms, relative 100 ns units
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        result->Position = READ_REGISTER_ULONG((PULONG)(hda + 0x84));
        result->CaptureStatus = READ_REGISTER_UCHAR(hda + 0x83);
        if (result->CaptureStatus & 0x18) result->Result = 6;
    }
    WRITE_REGISTER_UCHAR(hda + 0x80, 0);
    delay.QuadPart = -10000; // 1 ms per stop poll, up to 300 ms
    for (i = 0; i < 300; ++i) {
        if ((READ_REGISTER_UCHAR(hda + 0x80) & 2) == 0) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    result->StopPolls = i;
    result->StoppedControl = READ_REGISTER_ULONG((PULONG)(hda + 0x80));
    if (i == 300) {
        result->Result = 4;
        // Preserve DMA buffers if the hardware still owns them.
        Context->UnstoppedCaptureBdl = bdl;
        Context->UnstoppedCapturePayload = payload;
        bdl = NULL;
        payload = NULL;
        goto complete;
    }

restore:
    WRITE_REGISTER_UCHAR(hda + 0x83, 0x1c);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x804), beforePpctl);
    WRITE_REGISTER_UCHAR(hda + 0x1048, beforeLtrp);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x98), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x9c), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x88), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x8c), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x92), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80), beforeControl);
    result->RestoredControl = READ_REGISTER_ULONG((PULONG)(hda + 0x80));
    result->RestoredPpctl = READ_REGISTER_ULONG((PULONG)(hda + 0x804));
    result->RestoredCbl = READ_REGISTER_ULONG((PULONG)(hda + 0x88));
    result->RestoredLvi = READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c));
    result->RestoredFormat = READ_REGISTER_USHORT((PUSHORT)(hda + 0x92));
    result->RestoredBdlLow = READ_REGISTER_ULONG((PULONG)(hda + 0x98));
    result->RestoredBdlHigh = READ_REGISTER_ULONG((PULONG)(hda + 0x9c));
    result->RestoredLtrp = READ_REGISTER_UCHAR(hda + 0x1048);
    result->Adspcs = READ_REGISTER_ULONG((PULONG)(dsp + 0x04));
    if (result->RestoredControl != beforeControl ||
        result->RestoredPpctl != beforePpctl ||
        result->RestoredCbl != 0 || result->RestoredLvi != 0 ||
        result->RestoredFormat != 0 || result->RestoredBdlLow != 0 ||
        result->RestoredBdlHigh != 0 || result->RestoredLtrp != beforeLtrp ||
        result->Adspcs != 0x00000f0f) {
        result->Result = 5;
    }

complete:
    WdfRequestSetInformation(Request, sizeof(*result));
    status = STATUS_SUCCESS;

finish:
    if (dsp != NULL) MmUnmapIoSpace(dsp, 0x100);
    if (hda != NULL) MmUnmapIoSpace(hda, 0x2000);
    if (payload != NULL) WdfObjectDelete(payload);
    if (bdl != NULL) WdfObjectDelete(bdl);
    WdfRequestComplete(Request, status);
}

static VOID
SofProbeReadPciConfig(_In_ WDFREQUEST Request,
                      _In_ WDFDEVICE Device,
                      _In_ size_t OutputBufferLength,
                      _In_ size_t InputBufferLength)
{
    PSOFPROBE_PCI_SNAPSHOT result;
    BUS_INTERFACE_STANDARD bus;
    UCHAR config[16];
    ULONG bytesRead;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    RtlZeroMemory(&bus, sizeof(bus));
    status = WdfFdoQueryForInterface(Device, &GUID_BUS_INTERFACE_STANDARD,
                                     (PINTERFACE)&bus, sizeof(bus), 1, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    RtlZeroMemory(config, sizeof(config));
    bytesRead = bus.GetBusData(bus.Context, PCI_WHICHSPACE_CONFIG,
                               config, 0, sizeof(config));
    bus.InterfaceDereference(bus.Context);
    if (bytesRead != sizeof(config)) {
        WdfRequestComplete(Request, STATUS_DEVICE_DATA_ERROR);
        return;
    }
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1;
    RtlCopyMemory(&result->VendorDevice, config, sizeof(ULONG));
    RtlCopyMemory(&result->CommandStatus, config + 4, sizeof(ULONG));
    RtlCopyMemory(&result->RevisionClass, config + 8, sizeof(ULONG));
    RtlCopyMemory(&result->HeaderInfo, config + 12, sizeof(ULONG));
    WdfRequestSetInformation(Request, sizeof(*result));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

static VOID
SofProbeTestCaptureSetup(_In_ WDFREQUEST Request,
                         _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                         _In_ size_t OutputBufferLength,
                         _In_ size_t InputBufferLength)
{
    PSOFPROBE_STREAM_SETUP_RESULT result;
    WDFCOMMONBUFFER bdl = NULL;
    WDFCOMMONBUFFER payload = NULL;
    PHYSICAL_ADDRESS bdlAddress;
    PHYSICAL_ADDRESS payloadAddress;
    SOFPROBE_BDL_ENTRY *entry;
    PUCHAR hda = NULL;
    PUCHAR dsp = NULL;
    PULONG control;
    PULONG ppctl;
    ULONG beforeControl;
    ULONG beforePpctl;
    ULONG current;
    ULONG i;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    if (Context->CaptureSetupAttempted) {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) goto finish;
    status = WdfCommonBufferCreate(Context->DmaEnabler, 128,
                                   WDF_NO_OBJECT_ATTRIBUTES, &bdl);
    if (!NT_SUCCESS(status)) goto finish;
    status = WdfCommonBufferCreate(Context->DmaEnabler, 4096,
                                   WDF_NO_OBJECT_ATTRIBUTES, &payload);
    if (!NT_SUCCESS(status)) goto finish;
    bdlAddress = WdfCommonBufferGetAlignedLogicalAddress(bdl);
    payloadAddress = WdfCommonBufferGetAlignedLogicalAddress(payload);
    if ((bdlAddress.QuadPart & 127) != 0 ||
        (payloadAddress.QuadPart & 127) != 0) {
        status = STATUS_DATATYPE_MISALIGNMENT;
        goto finish;
    }
    RtlZeroMemory(WdfCommonBufferGetAlignedVirtualAddress(bdl), 128);
    RtlZeroMemory(WdfCommonBufferGetAlignedVirtualAddress(payload), 4096);
    entry = (SOFPROBE_BDL_ENTRY *)WdfCommonBufferGetAlignedVirtualAddress(bdl);
    entry[0].Address = (ULONGLONG)payloadAddress.QuadPart;
    entry[0].Length = 2048;
    entry[1].Address = (ULONGLONG)payloadAddress.QuadPart + 2048;
    entry[1].Length = 2048;

    hda = (PUCHAR)MmMapIoSpaceEx(Context->HdaStart, 0x1000,
                                PAGE_READWRITE | PAGE_NOCACHE);
    dsp = (PUCHAR)MmMapIoSpaceEx(Context->DspStart, 0x100,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (hda == NULL || dsp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto finish;
    }
    control = (PULONG)(hda + 0x80);
    ppctl = (PULONG)(hda + 0x804);
    beforeControl = READ_REGISTER_ULONG(control);
    beforePpctl = READ_REGISTER_ULONG(ppctl);
    if (READ_REGISTER_USHORT((PUSHORT)hda) != 0x9701 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x08)) != 1 ||
        beforeControl != 0x00040000 || beforePpctl != 0x40000000 ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0x04)) != 0x00000f0f ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0xd0)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(dsp + 0xd4)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x88)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x92)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x98)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x9c)) != 0) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto finish;
    }
    MmUnmapIoSpace(dsp, 0x100);
    dsp = NULL;
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1;
    result->BeforeControl = beforeControl;
    Context->CaptureSetupAttempted = TRUE;

    // Prepare the ICCMAX input stream but do not set RUN or fetch DMA data.
    WRITE_REGISTER_ULONG(control, beforeControl | 1);
    for (i = 0; i < 100; ++i) {
        if (READ_REGISTER_ULONG(control) & 1) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto restore;
    }
    WRITE_REGISTER_ULONG(control, beforeControl);
    KeStallExecutionProcessor(3);
    for (i = 0; i < 100; ++i) {
        if ((READ_REGISTER_ULONG(control) & 1) == 0) break;
        KeStallExecutionProcessor(5);
    }
    if (i == 100) {
        result->Result = 1;
        goto restore;
    }
    result->ResetAcknowledged = 1;
    current = READ_REGISTER_ULONG(control) & 0x00ffffff;
    WRITE_REGISTER_ULONG(control, (current & ~0x00f00000) | 0x00100000);
    WRITE_REGISTER_ULONG(ppctl, beforePpctl | 1);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x88), 4096);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x92), 0x40);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x8c), 1);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x98), bdlAddress.LowPart);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x9c), bdlAddress.HighPart);
    result->ConfiguredControl = READ_REGISTER_ULONG(control) & 0x00ffffff;
    result->ConfiguredCbl = READ_REGISTER_ULONG((PULONG)(hda + 0x88));
    result->ConfiguredLvi = READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c));
    result->ConfiguredFormat = READ_REGISTER_USHORT((PUSHORT)(hda + 0x92));
    result->ConfiguredBdlLow = READ_REGISTER_ULONG((PULONG)(hda + 0x98));
    result->ConfiguredBdlHigh = READ_REGISTER_ULONG((PULONG)(hda + 0x9c));
    result->ConfiguredPpctl = READ_REGISTER_ULONG(ppctl);
    if (result->ConfiguredControl != 0x00140000 ||
        result->ConfiguredCbl != 4096 || result->ConfiguredLvi != 1 ||
        result->ConfiguredFormat != 0x40 ||
        result->ConfiguredBdlLow != bdlAddress.LowPart ||
        result->ConfiguredBdlHigh != (ULONG)bdlAddress.HighPart ||
        result->ConfiguredPpctl != (beforePpctl | 1)) {
        result->Result = 2;
    }

restore:
    WRITE_REGISTER_ULONG(ppctl, beforePpctl);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x98), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x9c), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x8c), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x92), 0);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x88), 0);
    WRITE_REGISTER_ULONG(control, beforeControl);
    result->RestoredControl = READ_REGISTER_ULONG(control) & 0x00ffffff;
    result->RestoredCbl = READ_REGISTER_ULONG((PULONG)(hda + 0x88));
    result->RestoredLvi = READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c));
    result->RestoredFormat = READ_REGISTER_USHORT((PUSHORT)(hda + 0x92));
    result->RestoredBdlLow = READ_REGISTER_ULONG((PULONG)(hda + 0x98));
    if (result->RestoredControl != beforeControl ||
        result->RestoredCbl != 0 || result->RestoredLvi != 0 ||
        result->RestoredFormat != 0 || result->RestoredBdlLow != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x9c)) != 0 ||
        READ_REGISTER_ULONG(ppctl) != beforePpctl) {
        result->Result = 3;
    }
    WdfRequestSetInformation(Request, sizeof(*result));
    status = STATUS_SUCCESS;

finish:
    if (dsp != NULL) MmUnmapIoSpace(dsp, 0x100);
    if (hda != NULL) MmUnmapIoSpace(hda, 0x1000);
    if (payload != NULL) WdfObjectDelete(payload);
    if (bdl != NULL) WdfObjectDelete(bdl);
    WdfRequestComplete(Request, status);
}

static VOID
SofProbeReadBootPreflight(_In_ WDFREQUEST Request,
                          _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                          _In_ size_t OutputBufferLength,
                          _In_ size_t InputBufferLength)
{
    PSOFPROBE_BOOT_PREFLIGHT result;
    PUCHAR hda;
    PUCHAR dsp;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    hda = (PUCHAR)MmMapIoSpaceEx(Context->HdaStart, 0x2000,
                                PAGE_READONLY | PAGE_NOCACHE);
    dsp = (PUCHAR)MmMapIoSpaceEx(Context->DspStart, 0x100,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (hda == NULL || dsp == NULL) {
        if (dsp != NULL) MmUnmapIoSpace(dsp, 0x100);
        if (hda != NULL) MmUnmapIoSpace(hda, 0x2000);
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1;
    result->Gcap = READ_REGISTER_USHORT((PUSHORT)hda);
    result->CaptureControlAndStatus = READ_REGISTER_ULONG((PULONG)(hda + 0x80));
    result->CaptureCbl = READ_REGISTER_ULONG((PULONG)(hda + 0x88));
    result->CaptureLvi = READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c));
    result->CaptureFormat = READ_REGISTER_USHORT((PUSHORT)(hda + 0x92));
    result->CaptureBdlLow = READ_REGISTER_ULONG((PULONG)(hda + 0x98));
    result->CaptureBdlHigh = READ_REGISTER_ULONG((PULONG)(hda + 0x9c));
    result->ProcessingPipeControl = READ_REGISTER_ULONG((PULONG)(hda + 0x804));
    result->Ltrp = READ_REGISTER_UCHAR((PUCHAR)(hda + 0x1048));
    result->Adspcs = READ_REGISTER_ULONG((PULONG)(dsp + 0x04));
    result->Hipcidr = READ_REGISTER_ULONG((PULONG)(dsp + 0xd0));
    result->Hipcida = READ_REGISTER_ULONG((PULONG)(dsp + 0xd4));
    result->Hipcctl = READ_REGISTER_ULONG((PULONG)(dsp + 0xe8));
    result->Adspic = READ_REGISTER_ULONG((PULONG)(dsp + 0x08));
    result->Adspis = READ_REGISTER_ULONG((PULONG)(dsp + 0x0c));
    MmUnmapIoSpace(dsp, 0x100);
    MmUnmapIoSpace(hda, 0x2000);
    WdfRequestSetInformation(Request, sizeof(*result));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

static VOID
SofProbeReadStream0(_In_ WDFREQUEST Request,
                    _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                    _In_ size_t OutputBufferLength,
                    _In_ size_t InputBufferLength)
{
    PSOFPROBE_STREAM_SNAPSHOT result;
    PUCHAR hda;
    ULONG streamControlAndStatus;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    hda = (PUCHAR)MmMapIoSpaceEx(Context->HdaStart, 0x1000,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (hda == NULL) {
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    // GCAP 0x9701 reports seven input streams; output stream 0 therefore
    // starts at 0x80 + 7 * 0x20 = 0x160 on this exact N100 controller.
    if (READ_REGISTER_USHORT((PUSHORT)hda) != 0x9701) {
        MmUnmapIoSpace(hda, 0x1000);
        WdfRequestComplete(Request, STATUS_DEVICE_CONFIGURATION_ERROR);
        return;
    }
    streamControlAndStatus = READ_REGISTER_ULONG((PULONG)(hda + 0x160));
    result->Control = streamControlAndStatus & 0x00ffffff;
    result->Status = streamControlAndStatus >> 24;
    result->CyclicBufferLength = READ_REGISTER_ULONG((PULONG)(hda + 0x168));
    result->LastValidIndex = READ_REGISTER_USHORT((PUSHORT)(hda + 0x16c));
    result->Format = READ_REGISTER_USHORT((PUSHORT)(hda + 0x172));
    result->BdlAddressLow = READ_REGISTER_ULONG((PULONG)(hda + 0x178));
    result->BdlAddressHigh = READ_REGISTER_ULONG((PULONG)(hda + 0x17c));
    result->ProcessingPipeControl = READ_REGISTER_ULONG((PULONG)(hda + 0x804));
    MmUnmapIoSpace(hda, 0x1000);
    WdfRequestSetInformation(Request, sizeof(*result));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

static VOID
SofProbePulseCore0(_In_ WDFREQUEST Request,
                   _In_ PSOFPROBE_DEVICE_CONTEXT Context,
                   _In_ size_t OutputBufferLength,
                   _In_ size_t InputBufferLength)
{
    PSOFPROBE_POWER_RESULT result;
    PUCHAR hda;
    PUCHAR dsp;
    PULONG adspcs;
    ULONG before;
    ULONG ppctl;
    ULONG gctl;
    ULONG i;
    LARGE_INTEGER interval;
    NTSTATUS status;

    if (InputBufferLength != 0 || OutputBufferLength < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    if (Context->PowerPulseAttempted) {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result),
                                            (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    hda = (PUCHAR)MmMapIoSpaceEx(Context->HdaStart, 0x1000,
                                PAGE_READONLY | PAGE_NOCACHE);
    if (hda == NULL) {
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    dsp = (PUCHAR)MmMapIoSpaceEx(Context->DspStart, 0x100,
                                PAGE_READWRITE | PAGE_NOCACHE);
    if (dsp == NULL) {
        MmUnmapIoSpace(hda, 0x1000);
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    gctl = READ_REGISTER_ULONG((PULONG)(hda + 0x08));
    ppctl = READ_REGISTER_ULONG((PULONG)(hda + 0x804));
    adspcs = (PULONG)(dsp + 0x04);
    before = READ_REGISTER_ULONG(adspcs);
    // This exact baseline was observed repeatedly on the N100. Do not
    // disturb a DSP that is active, or any other core's control bits.
    if ((gctl & 1) == 0 || ppctl != 0x40000000 || before != 0x00000f0f) {
        MmUnmapIoSpace(dsp, 0x100);
        MmUnmapIoSpace(hda, 0x1000);
        WdfRequestComplete(Request, STATUS_DEVICE_CONFIGURATION_ERROR);
        return;
    }

    Context->PowerPulseAttempted = TRUE;
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1;
    result->Before = before;
    interval.QuadPart = -10000; // One millisecond, bounded to 100 polls.
    WRITE_REGISTER_ULONG(adspcs, before | 0x00010000);
    result->Requested = READ_REGISTER_ULONG(adspcs);
    for (i = 0; i < 100; ++i) {
        if (READ_REGISTER_ULONG(adspcs) & 0x01000000) break;
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }
    result->UpPolls = i;
    result->Powered = READ_REGISTER_ULONG(adspcs);

    // Keep reset/stall asserted throughout. Always clear the SPA request,
    // including when the power-up acknowledgment times out.
    WRITE_REGISTER_ULONG(adspcs, before);
    for (i = 0; i < 100; ++i) {
        if ((READ_REGISTER_ULONG(adspcs) & 0x01000000) == 0) break;
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }
    result->DownPolls = i;
    result->Restored = READ_REGISTER_ULONG(adspcs);
    if ((result->Powered & 0x01000000) == 0) result->Result = 1;
    if ((result->Restored & 0x01000000) != 0 ||
        (result->Restored & 0x00010000) != 0) result->Result = 2;
    MmUnmapIoSpace(dsp, 0x100);
    MmUnmapIoSpace(hda, 0x1000);
    WdfRequestSetInformation(Request, sizeof(*result));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

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
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDFDEVICE device;
    PSOFPROBE_DEVICE_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = SofProbeEvtPrepareHardware;
    callbacks.EvtDeviceReleaseHardware = SofProbeEvtReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SOFPROBE_DEVICE_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;
    context = SofProbeGetContext(device);
    WdfDeviceSetAlignmentRequirement(device, FILE_128_BYTE_ALIGNMENT);
    WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig, WdfDmaProfileScatterGather64Duplex,
                                524288);
    status = WdfDmaEnablerCreate(device, &dmaConfig, WDF_NO_OBJECT_ATTRIBUTES,
                                 &context->DmaEnabler);
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

    if (IoControlCode != IOCTL_SOFPROBE_READ_SNAPSHOT &&
        IoControlCode != IOCTL_SOFPROBE_PULSE_CORE0 &&
        IoControlCode != IOCTL_SOFPROBE_TEST_DMA_ALLOCATION &&
        IoControlCode != IOCTL_SOFPROBE_STAGE_IPC3_FIRMWARE &&
        IoControlCode != IOCTL_SOFPROBE_READ_STREAM0 &&
        IoControlCode != IOCTL_SOFPROBE_TEST_STREAM_SETUP &&
        IoControlCode != IOCTL_SOFPROBE_READ_BOOT_PREFLIGHT &&
        IoControlCode != IOCTL_SOFPROBE_TEST_CAPTURE_SETUP &&
        IoControlCode != IOCTL_SOFPROBE_READ_PCI_CONFIG &&
        IoControlCode != IOCTL_SOFPROBE_TEST_CAPTURE_DMA &&
        IoControlCode != IOCTL_SOFPROBE_TEST_ROM_ENTRY &&
        IoControlCode != IOCTL_SOFPROBE_TEST_FIRMWARE_DMA &&
        IoControlCode != IOCTL_SOFPROBE_TEST_IPC3_READY &&
        IoControlCode != IOCTL_SOFPROBE_READ_IPC3_INFO &&
        IoControlCode != IOCTL_SOFPROBE_ACK_IPC3_READY &&
        IoControlCode != IOCTL_SOFPROBE_QUERY_IPC3_MEMORY) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
    if (!context->ResourcesReady) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_PULSE_CORE0) {
        SofProbePulseCore0(Request, context, OutputBufferLength,
                           InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_TEST_DMA_ALLOCATION) {
        SofProbeTestDmaAllocation(Request, context, OutputBufferLength,
                                  InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_STAGE_IPC3_FIRMWARE) {
        SofProbeStageFirmware(Request, context, OutputBufferLength,
                              InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_READ_STREAM0) {
        SofProbeReadStream0(Request, context, OutputBufferLength,
                            InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_TEST_STREAM_SETUP) {
        SofProbeTestStreamSetup(Request, context, OutputBufferLength,
                                InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_READ_BOOT_PREFLIGHT) {
        SofProbeReadBootPreflight(Request, context, OutputBufferLength,
                                  InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_TEST_CAPTURE_SETUP) {
        SofProbeTestCaptureSetup(Request, context, OutputBufferLength,
                                 InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_READ_PCI_CONFIG) {
        SofProbeReadPciConfig(Request, device, OutputBufferLength,
                              InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_TEST_CAPTURE_DMA) {
        SofProbeTestCaptureDma(Request, device, context, OutputBufferLength,
                               InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_TEST_ROM_ENTRY) {
        SofProbeTestRomEntry(Request, device, context, OutputBufferLength,
                             InputBufferLength, FALSE, FALSE, FALSE, FALSE,
                             FALSE);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_TEST_FIRMWARE_DMA) {
        SofProbeTestRomEntry(Request, device, context, OutputBufferLength,
                             InputBufferLength, TRUE, FALSE, FALSE, FALSE,
                             FALSE);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_TEST_IPC3_READY) {
        SofProbeTestRomEntry(Request, device, context, OutputBufferLength,
                             InputBufferLength, TRUE, TRUE, FALSE, FALSE,
                             FALSE);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_READ_IPC3_INFO) {
        SofProbeTestRomEntry(Request, device, context, OutputBufferLength,
                             InputBufferLength, TRUE, TRUE, TRUE, FALSE,
                             FALSE);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_ACK_IPC3_READY) {
        SofProbeTestRomEntry(Request, device, context, OutputBufferLength,
                             InputBufferLength, TRUE, TRUE, TRUE, TRUE,
                             FALSE);
        return;
    }
    if (IoControlCode == IOCTL_SOFPROBE_QUERY_IPC3_MEMORY) {
        SofProbeTestRomEntry(Request, device, context, OutputBufferLength,
                             InputBufferLength, TRUE, TRUE, TRUE, TRUE,
                             TRUE);
        return;
    }
    if (InputBufferLength != 0) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
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
    {
        PSOFPROBE_DEVICE_CONTEXT context = SofProbeGetContext(Device);
        context->ResourcesReady = FALSE;
        if (context->FirmwarePayload != NULL) {
            WdfObjectDelete(context->FirmwarePayload);
            context->FirmwarePayload = NULL;
        }
        if (context->FirmwareBdl != NULL) {
            WdfObjectDelete(context->FirmwareBdl);
            context->FirmwareBdl = NULL;
        }
    }
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
        SofProbeGetContext(Device)->ResourcesReady = FALSE;
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
