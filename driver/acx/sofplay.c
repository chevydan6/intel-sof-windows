// SPDX-License-Identifier: GPL-2.0-only
// Initial ACX render circuit for the N100 SOF speaker path.

#include <wdm.h>
#include <windef.h>
#include <initguid.h>
#define NOBITMAP
#include <mmsystem.h>
#include <ks.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <wdf.h>
#include <acx.h>

#ifndef SOFPLAY_INTEGRATED
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofPlayEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofPlayEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE SofPlayEvtReleaseHardware;
#endif
EVT_ACX_CIRCUIT_CREATE_STREAM SofPlayEvtCreateStream;

#ifdef SOFPLAY_INTEGRATED
NTSTATUS SofProbeAcxClaimStream(_In_ WDFDEVICE Device);
VOID SofProbeAcxReleaseStream(_In_ WDFDEVICE Device);
VOID SofProbeAcxSetTimer(_In_ WDFDEVICE Device, _In_opt_ WDFTIMER Timer);
VOID SofProbeAcxRecord(_In_ WDFDEVICE Device, _In_ ULONG Event);
VOID SofProbeAcxRecordFailure(_In_ WDFDEVICE Device, _In_ ULONG Stage,
                             _In_ NTSTATUS Status);
NTSTATUS SofProbeAcxPrepareBuffer(_In_ WDFDEVICE Device, _Out_ PULONG Bytes);
NTSTATUS SofProbeAcxWritePacket(_In_ WDFDEVICE Device,
                                _In_reads_bytes_(Bytes) const UCHAR *Source,
                                _In_ ULONG Packet, _In_ ULONG Bytes);
NTSTATUS SofProbeAcxGetPosition(_In_ WDFDEVICE Device,
                               _Out_ PULONG Position,
                               _Out_ PUCHAR Status);
NTSTATUS SofProbeAcxStart(_In_ WDFDEVICE Device);
NTSTATUS SofProbeAcxPause(_In_ WDFDEVICE Device);

EVT_ACX_STREAM_PREPARE_HARDWARE SofPlayEvtStreamPrepare;
EVT_ACX_STREAM_RELEASE_HARDWARE SofPlayEvtStreamRelease;
EVT_ACX_STREAM_RUN SofPlayEvtStreamRun;
EVT_ACX_STREAM_PAUSE SofPlayEvtStreamPause;
EVT_ACX_STREAM_GET_HW_LATENCY SofPlayEvtGetLatency;
EVT_ACX_STREAM_ALLOCATE_RTPACKETS SofPlayEvtAllocatePackets;
EVT_ACX_STREAM_FREE_RTPACKETS SofPlayEvtFreePackets;
EVT_ACX_STREAM_SET_RENDER_PACKET SofPlayEvtSetPacket;
EVT_ACX_STREAM_GET_CURRENT_PACKET SofPlayEvtGetPacket;
EVT_ACX_STREAM_GET_PRESENTATION_POSITION SofPlayEvtGetPosition;
EVT_WDF_TIMER SofPlayEvtTimer;
EVT_WDF_OBJECT_CONTEXT_CLEANUP SofPlayEvtStreamCleanup;

typedef struct _SOFPLAY_STREAM_CONTEXT {
    WDFDEVICE Device;
    ACXSTREAM Stream;
    WDFTIMER Timer;
    PACX_RTPACKET Packets;
    ULONG PacketCount;
    ULONG PacketSize;
    ULONG RingBytes;
    ULONG LastPosition;
    ULONGLONG LinearPosition;
    ULONG NextComplete;
    BOOLEAN Running;
} SOFPLAY_STREAM_CONTEXT, *PSOFPLAY_STREAM_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOFPLAY_STREAM_CONTEXT,
                                   SofPlayGetStreamContext)
#define SOFPLAY_PACKET_TAG 'pFoS'
#endif

#ifndef SOFPLAY_INTEGRATED
typedef struct _SOFPLAY_DEVICE_CONTEXT {
    ACXCIRCUIT RenderCircuit;
    BOOLEAN CircuitAdded;
} SOFPLAY_DEVICE_CONTEXT, *PSOFPLAY_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOFPLAY_DEVICE_CONTEXT, SofPlayGetContext)
#endif

static const GUID SofPlaySpeakerComponent =
    { 0x39edc32d, 0xd57e, 0x47a9,
      { 0x84, 0x20, 0xbb, 0xa1, 0x08, 0x78, 0x5c, 0xf0 } };
static const GUID SofPlayAudioCategory =
    { 0x6994ad04, 0x93ef, 0x11d0,
      { 0xa3, 0xcc, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
static const GUID SofPlaySpeakerNode =
    { 0xdff21ce1, 0xf70f, 0x11d0,
      { 0xb9, 0x17, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
static const GUID SofPlayDefaultMode =
    { 0xc18e2f7e, 0x933d, 0x4965,
      { 0xb7, 0xd1, 0x1e, 0xef, 0x22, 0x8d, 0x2a, 0xf3 } };

static KSDATAFORMAT_WAVEFORMATEXTENSIBLE SofPlayPcm48000S32 = {
    {
        sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE), 0, 0, 0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    {
        { WAVE_FORMAT_EXTENSIBLE, 2, 48000, 384000, 8, 32,
          sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) },
        24, KSAUDIO_SPEAKER_STEREO,
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)
    }
};

NTSTATUS
SofPlayCreateRenderCircuit(_In_ WDFDEVICE Device,
                           _Out_ ACXCIRCUIT *Circuit)
{
    PACXCIRCUIT_INIT circuitInit;
    ACXCIRCUIT circuit = NULL;
    ACXPIN pins[2] = { NULL, NULL };
    ACX_PIN_CONFIG pinConfig;
    ACX_DATAFORMAT_CONFIG formatConfig;
    ACX_DATAFORMAT_LIST_CONFIG formatListConfig;
    ACX_JACK_CONFIG jackConfig;
    ACXDATAFORMAT format = NULL;
    ACXDATAFORMATLIST formats;
    ACXDATAFORMATLIST defaultFormats = NULL;
    ACXJACK jack = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    // Match the reference string in sofplay.inx AddInterface directives.
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"Speaker0");
    NTSTATUS status;

    *Circuit = NULL;
    circuitInit = AcxCircuitInitAllocate(Device);
    if (circuitInit == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    AcxCircuitInitSetComponentId(circuitInit, &SofPlaySpeakerComponent);
    status = AcxCircuitInitAssignName(circuitInit, &name);
    if (!NT_SUCCESS(status)) goto finish;
    AcxCircuitInitSetCircuitType(circuitInit, AcxCircuitTypeRender);
    status = AcxCircuitInitAssignAcxCreateStreamCallback(
        circuitInit, SofPlayEvtCreateStream);
    if (!NT_SUCCESS(status)) goto finish;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = AcxCircuitCreate(Device, &attributes, &circuitInit, &circuit);
    if (!NT_SUCCESS(status)) goto finish;

    ACX_PIN_CONFIG_INIT(&pinConfig);
    pinConfig.Type = AcxPinTypeSink;
    pinConfig.Communication = AcxPinCommunicationSink;
    pinConfig.Category = &SofPlayAudioCategory;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = circuit;
    status = AcxPinCreate(circuit, &attributes, &pinConfig, &pins[0]);
    if (!NT_SUCCESS(status)) goto finish;

    ACX_PIN_CONFIG_INIT(&pinConfig);
    pinConfig.Type = AcxPinTypeSource;
    pinConfig.Communication = AcxPinCommunicationNone;
    pinConfig.Category = &SofPlaySpeakerNode;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = circuit;
    status = AcxPinCreate(circuit, &attributes, &pinConfig, &pins[1]);
    if (!NT_SUCCESS(status)) goto finish;

    ACX_JACK_CONFIG_INIT(&jackConfig);
    jackConfig.Description.ChannelMapping = KSAUDIO_SPEAKER_STEREO;
    jackConfig.Description.Color = 0;
    jackConfig.Description.ConnectionType = AcxConnTypeAtapiInternal;
    jackConfig.Description.GeoLocation = AcxGeoLocFront;
    jackConfig.Description.GenLocation = AcxGenLocPrimaryBox;
    jackConfig.Description.PortConnection = AcxPortConnIntegratedDevice;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pins[1];
    status = AcxJackCreate(pins[1], &attributes, &jackConfig, &jack);
    if (!NT_SUCCESS(status)) goto finish;
    status = AcxPinAddJacks(pins[1], &jack, 1);
    if (!NT_SUCCESS(status)) goto finish;

    ACX_DATAFORMAT_CONFIG_INIT_KS(&formatConfig, &SofPlayPcm48000S32);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = circuit;
    status = AcxDataFormatCreate(Device, &attributes, &formatConfig, &format);
    if (!NT_SUCCESS(status)) goto finish;
    formats = AcxPinGetRawDataFormatList(pins[0]);
    if (formats == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto finish;
    }
    // Windows needs an active default format to initialize the endpoint.
    status = AcxDataFormatListAssignDefaultDataFormat(formats, format);
    if (!NT_SUCCESS(status)) goto finish;
    // Shared-mode clients request the default processing mode, not raw mode.
    // The hardware format is identical in both modes; software processing is
    // supplied by the Windows audio engine.
    ACX_DATAFORMAT_LIST_CONFIG_INIT(&formatListConfig);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = pins[0];
    status = AcxDataFormatListCreate(Device, &attributes,
                                     &formatListConfig, &defaultFormats);
    if (!NT_SUCCESS(status)) goto finish;
    status = AcxDataFormatListAssignDefaultDataFormat(defaultFormats, format);
    if (!NT_SUCCESS(status)) goto finish;
    status = AcxPinAssignModeDataFormatList(pins[0],
                                           &SofPlayDefaultMode,
                                           defaultFormats);
    if (!NT_SUCCESS(status)) goto finish;
    status = AcxCircuitAddPins(circuit, pins, RTL_NUMBER_OF(pins));
    if (!NT_SUCCESS(status)) goto finish;

    *Circuit = circuit;
    return STATUS_SUCCESS;

finish:
    if (circuitInit != NULL) AcxCircuitInitFree(circuitInit);
    if (circuit != NULL) WdfObjectDelete(circuit);
    return status;
}

NTSTATUS
SofPlayEvtCreateStream(_In_ WDFDEVICE Device,
                       _In_ ACXCIRCUIT Circuit,
                       _In_ ACXPIN Pin,
                       _In_ PACXSTREAM_INIT StreamInit,
                       _In_ ACXDATAFORMAT StreamFormat,
                       _In_ const GUID *SignalProcessingMode,
                       _In_ ACXOBJECTBAG VarArguments)
{
#ifdef SOFPLAY_INTEGRATED
    ACX_STREAM_CALLBACKS streamCallbacks;
    ACX_RT_STREAM_CALLBACKS rtCallbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_TIMER_CONFIG timerConfig;
    ACXSTREAM stream;
    PSOFPLAY_STREAM_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Pin);
    UNREFERENCED_PARAMETER(SignalProcessingMode);
    UNREFERENCED_PARAMETER(VarArguments);
    SofProbeAcxRecord(Device, 5);
    if (AcxDataFormatGetSampleRate(StreamFormat) != 48000 ||
        AcxDataFormatGetChannelsCount(StreamFormat) != 2 ||
        AcxDataFormatGetBlockAlign(StreamFormat) != 8) {
        SofProbeAcxRecordFailure(Device, 1, STATUS_NOT_SUPPORTED);
        return STATUS_NOT_SUPPORTED;
    }
    status = SofProbeAcxClaimStream(Device);
    if (!NT_SUCCESS(status)) {
        SofProbeAcxRecordFailure(Device, 2, status);
        return status;
    }
    ACX_STREAM_CALLBACKS_INIT(&streamCallbacks);
    streamCallbacks.EvtAcxStreamPrepareHardware = SofPlayEvtStreamPrepare;
    streamCallbacks.EvtAcxStreamReleaseHardware = SofPlayEvtStreamRelease;
    streamCallbacks.EvtAcxStreamRun = SofPlayEvtStreamRun;
    streamCallbacks.EvtAcxStreamPause = SofPlayEvtStreamPause;
    status = AcxStreamInitAssignAcxStreamCallbacks(StreamInit,
                                                   &streamCallbacks);
    if (!NT_SUCCESS(status)) {
        SofProbeAcxRecordFailure(Device, 3, status);
        goto fail;
    }
    ACX_RT_STREAM_CALLBACKS_INIT(&rtCallbacks);
    rtCallbacks.EvtAcxStreamGetHwLatency = SofPlayEvtGetLatency;
    rtCallbacks.EvtAcxStreamAllocateRtPackets = SofPlayEvtAllocatePackets;
    rtCallbacks.EvtAcxStreamFreeRtPackets = SofPlayEvtFreePackets;
    rtCallbacks.EvtAcxStreamSetRenderPacket = SofPlayEvtSetPacket;
    rtCallbacks.EvtAcxStreamGetCurrentPacket = SofPlayEvtGetPacket;
    rtCallbacks.EvtAcxStreamGetPresentationPosition = SofPlayEvtGetPosition;
    status = AcxStreamInitAssignAcxRtStreamCallbacks(StreamInit,
                                                     &rtCallbacks);
    if (!NT_SUCCESS(status)) {
        SofProbeAcxRecordFailure(Device, 4, status);
        goto fail;
    }
    AcxStreamInitSetAcxRtStreamSupportsNotifications(StreamInit);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
                                            SOFPLAY_STREAM_CONTEXT);
    attributes.EvtCleanupCallback = SofPlayEvtStreamCleanup;
    status = AcxRtStreamCreate(Device, Circuit, &attributes, &StreamInit,
                               &stream);
    if (!NT_SUCCESS(status)) {
        SofProbeAcxRecordFailure(Device, 5, status);
        goto fail;
    }
    context = SofPlayGetStreamContext(stream);
    context->Device = Device;
    context->Stream = stream;
    WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, SofPlayEvtTimer, 1);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = stream;
    status = WdfTimerCreate(&timerConfig, &attributes, &context->Timer);
    if (!NT_SUCCESS(status)) {
        SofProbeAcxRecordFailure(Device, 6, status);
        WdfObjectDelete(stream);
        return status;
    }
    SofProbeAcxSetTimer(Device, context->Timer);
    SofProbeAcxRecord(Device, 1);
    return STATUS_SUCCESS;
fail:
    SofProbeAcxReleaseStream(Device);
    return status;
#else
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Circuit);
    UNREFERENCED_PARAMETER(Pin);
    UNREFERENCED_PARAMETER(StreamInit);
    UNREFERENCED_PARAMETER(StreamFormat);
    UNREFERENCED_PARAMETER(SignalProcessingMode);
    UNREFERENCED_PARAMETER(VarArguments);
    // Hardware stream creation is added after circuit enumeration is verified.
    return STATUS_NOT_SUPPORTED;
#endif
}

#ifdef SOFPLAY_INTEGRATED
static VOID
SofPlayReleasePacketMemory(_In_ PACX_RTPACKET Packets,
                           _In_ ULONG PacketCount)
{
    ULONG i;
    for (i = 0; i < PacketCount; ++i) {
        PMDL mdl = Packets[i].RtPacketBuffer.u.MdlType.Mdl;
        if (mdl != NULL) {
            PVOID buffer = MmGetMdlVirtualAddress(mdl);
            IoFreeMdl(mdl);
            ExFreePoolWithTag(buffer, SOFPLAY_PACKET_TAG);
        }
    }
    ExFreePoolWithTag(Packets, SOFPLAY_PACKET_TAG);
}

VOID
SofPlayEvtStreamCleanup(_In_ WDFOBJECT Object)
{
    PSOFPLAY_STREAM_CONTEXT context =
        SofPlayGetStreamContext((ACXSTREAM)Object);
    PACX_RTPACKET packets;
    ULONG packetCount;
    if (context->Timer != NULL)
        WdfTimerStop(context->Timer, TRUE);
    if (context->Device != NULL)
        SofProbeAcxSetTimer(context->Device, NULL);
    packets = (PACX_RTPACKET)InterlockedExchangePointer(
        (PVOID volatile *)&context->Packets, NULL);
    if (packets != NULL) {
        packetCount = context->PacketCount;
        context->PacketCount = 0;
        context->PacketSize = 0;
        SofPlayReleasePacketMemory(packets, packetCount);
    }
    if (context->Device != NULL) SofProbeAcxReleaseStream(context->Device);
}

NTSTATUS
SofPlayEvtAllocatePackets(_In_ ACXSTREAM Stream,
                          _In_ ULONG PacketCount,
                          _In_ ULONG PacketSize,
                          _Out_ PACX_RTPACKET *Packets)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    PACX_RTPACKET packets;
    ULONG allocationBytes;
    ULONG i;
    if (PacketCount < 1 || PacketCount > 2 || PacketSize == 0 ||
        PacketSize > 12288 || (PacketSize & 7) != 0 ||
        PacketCount * PacketSize > 49152 || context->Packets != NULL)
        return STATUS_INVALID_PARAMETER;
    allocationBytes = (PacketSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    packets = (PACX_RTPACKET)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        sizeof(ACX_RTPACKET) * PacketCount, SOFPLAY_PACKET_TAG);
    if (packets == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(packets, sizeof(ACX_RTPACKET) * PacketCount);
    for (i = 0; i < PacketCount; ++i) {
        PVOID buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                       allocationBytes, SOFPLAY_PACKET_TAG);
        PMDL mdl;
        if (buffer == NULL) goto allocation_failed;
        mdl = IoAllocateMdl(buffer, allocationBytes, FALSE, TRUE, NULL);
        if (mdl == NULL) {
            ExFreePoolWithTag(buffer, SOFPLAY_PACKET_TAG);
            goto allocation_failed;
        }
        MmBuildMdlForNonPagedPool(mdl);
        ACX_RTPACKET_INIT(&packets[i]);
        WDF_MEMORY_DESCRIPTOR_INIT_MDL(&packets[i].RtPacketBuffer,
                                       mdl, allocationBytes);
        packets[i].RtPacketSize = PacketSize;
        // ACX maps the separate MDLs into one contiguous client buffer.
        // Packet 0 must end at its allocation's page boundary so packet 1
        // can begin immediately after it, without page-rounding padding.
        packets[i].RtPacketOffset = i == 0 ? allocationBytes - PacketSize : 0;
    }
    context->PacketCount = PacketCount;
    context->PacketSize = PacketSize;
    InterlockedExchangePointer((PVOID volatile *)&context->Packets, packets);
    *Packets = packets;
    return STATUS_SUCCESS;
allocation_failed:
    SofPlayReleasePacketMemory(packets, PacketCount);
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID
SofPlayEvtFreePackets(_In_ ACXSTREAM Stream,
                       _In_ PACX_RTPACKET Packets,
                       _In_ ULONG PacketCount)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    if (Packets == NULL || Packets != context->Packets ||
        PacketCount != context->PacketCount)
        return;
    if (InterlockedCompareExchangePointer(
        (PVOID volatile *)&context->Packets, NULL, Packets) != Packets)
        return;
    context->PacketCount = 0;
    context->PacketSize = 0;
    SofPlayReleasePacketMemory(Packets, PacketCount);
}

NTSTATUS
SofPlayEvtStreamPrepare(_In_ ACXSTREAM Stream)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    NTSTATUS status = SofProbeAcxPrepareBuffer(context->Device, &context->RingBytes);
    if (!NT_SUCCESS(status))
        SofProbeAcxRecordFailure(context->Device, 7, status);
    if (NT_SUCCESS(status)) {
        context->LastPosition = 0;
        context->LinearPosition = 0;
        context->NextComplete = 0;
    }
    return status;
}

NTSTATUS
SofPlayEvtStreamRelease(_In_ ACXSTREAM Stream)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    if (context->Running) {
        WdfTimerStop(context->Timer, TRUE);
        (VOID)SofProbeAcxPause(context->Device);
        context->Running = FALSE;
    }
    context->RingBytes = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
SofPlayEvtStreamRun(_In_ ACXSTREAM Stream)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    NTSTATUS status;
    ULONG position;
    UCHAR streamStatus;
    if (context->RingBytes == 0 || context->Packets == NULL)
        return STATUS_DEVICE_NOT_READY;
    status = SofProbeAcxStart(context->Device);
    if (!NT_SUCCESS(status)) {
        SofProbeAcxRecordFailure(context->Device, 8, status);
        return status;
    }
    status = SofProbeAcxGetPosition(context->Device, &position,
                                    &streamStatus);
    if (!NT_SUCCESS(status)) {
        (VOID)SofProbeAcxPause(context->Device);
        return status;
    }
    context->LastPosition = position;
    context->Running = TRUE;
    SofProbeAcxRecord(context->Device, 2);
    WdfTimerStart(context->Timer, WDF_REL_TIMEOUT_IN_MS(1));
    return STATUS_SUCCESS;
}

NTSTATUS
SofPlayEvtStreamPause(_In_ ACXSTREAM Stream)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    if (!context->Running) return STATUS_SUCCESS;
    WdfTimerStop(context->Timer, TRUE);
    context->Running = FALSE;
    return SofProbeAcxPause(context->Device);
}

NTSTATUS
SofPlayEvtGetLatency(_In_ ACXSTREAM Stream,
                      _Out_ ULONG *FifoSize,
                      _Out_ ULONG *Delay)
{
    UNREFERENCED_PARAMETER(Stream);
    *FifoSize = 128;
    *Delay = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
SofPlayEvtSetPacket(_In_ ACXSTREAM Stream, _In_ ULONG Packet,
                     _In_ ULONG Flags, _In_ ULONG EosPacketLength)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    PUCHAR source;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(EosPacketLength);
    if (context->Packets == NULL || context->PacketCount == 0 || context->RingBytes == 0)
        return STATUS_DEVICE_NOT_READY;
    source = (PUCHAR)MmGetMdlVirtualAddress(
        context->Packets[Packet % context->PacketCount].RtPacketBuffer.u.MdlType.Mdl);
    source += context->Packets[Packet % context->PacketCount].RtPacketOffset;
    status = SofProbeAcxWritePacket(context->Device, source, Packet, context->PacketSize);
    if (!NT_SUCCESS(status)) return status;
    SofProbeAcxRecord(context->Device, 3);
    return STATUS_SUCCESS;
}

NTSTATUS
SofPlayEvtGetPacket(_In_ ACXSTREAM Stream, _Out_ PULONG CurrentPacket)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    *CurrentPacket = context->NextComplete;
    return STATUS_SUCCESS;
}

NTSTATUS
SofPlayEvtGetPosition(_In_ ACXSTREAM Stream,
                       _Out_ PULONGLONG PositionInBlocks,
                       _Out_ PULONGLONG QPCPosition)
{
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(Stream);
    *PositionInBlocks = context->LinearPosition / 8;
    *QPCPosition = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
    return STATUS_SUCCESS;
}

VOID
SofPlayEvtTimer(_In_ WDFTIMER Timer)
{
    ACXSTREAM stream = (ACXSTREAM)WdfTimerGetParentObject(Timer);
    PSOFPLAY_STREAM_CONTEXT context = SofPlayGetStreamContext(stream);
    ULONG position;
    UCHAR status;
    if (!context->Running || context->PacketSize == 0 ||
        !NT_SUCCESS(SofProbeAcxGetPosition(context->Device, &position,
                                            &status)) ||
        (status & 0x1c) != 0 || position >= context->RingBytes)
        return;
    context->LinearPosition +=
        (position + context->RingBytes - context->LastPosition) %
        context->RingBytes;
    context->LastPosition = position;
    while (context->LinearPosition >=
           ((ULONGLONG)context->NextComplete + 1) * context->PacketSize) {
        ULONGLONG qpc = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
        (VOID)AcxRtStreamNotifyPacketComplete(stream,
                                               context->NextComplete, qpc);
        SofProbeAcxRecord(context->Device, 4);
        ++context->NextComplete;
    }
}
#endif

#ifndef SOFPLAY_INTEGRATED
NTSTATUS
SofPlayEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    ACX_DEVICEINIT_CONFIG acxInit;
    ACX_DEVICE_CONFIG acxConfig;
    WDF_PNPPOWER_EVENT_CALLBACKS callbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    PSOFPLAY_DEVICE_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);
    ACX_DEVICEINIT_CONFIG_INIT(&acxInit);
    status = AcxDeviceInitInitialize(DeviceInit, &acxInit);
    if (!NT_SUCCESS(status)) return status;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = SofPlayEvtPrepareHardware;
    callbacks.EvtDeviceReleaseHardware = SofPlayEvtReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SOFPLAY_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;
    context = SofPlayGetContext(device);
    ACX_DEVICE_CONFIG_INIT(&acxConfig);
    status = AcxDeviceInitialize(device, &acxConfig);
    if (!NT_SUCCESS(status)) return status;
    return SofPlayCreateRenderCircuit(device, &context->RenderCircuit);
}

NTSTATUS
SofPlayEvtPrepareHardware(_In_ WDFDEVICE Device,
                          _In_ WDFCMRESLIST ResourcesRaw,
                          _In_ WDFCMRESLIST ResourcesTranslated)
{
    PSOFPLAY_DEVICE_CONTEXT context = SofPlayGetContext(Device);
    NTSTATUS status;
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    status = AcxDeviceAddCircuit(Device, context->RenderCircuit);
    if (NT_SUCCESS(status)) context->CircuitAdded = TRUE;
    return status;
}

NTSTATUS
SofPlayEvtReleaseHardware(_In_ WDFDEVICE Device,
                          _In_ WDFCMRESLIST ResourcesTranslated)
{
    PSOFPLAY_DEVICE_CONTEXT context = SofPlayGetContext(Device);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    if (context->CircuitAdded) {
        NTSTATUS status = AcxDeviceRemoveCircuit(Device, context->RenderCircuit);
        if (!NT_SUCCESS(status)) return status;
        context->CircuitAdded = FALSE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG driverConfig;
    ACX_DRIVER_CONFIG acxConfig;
    WDFDRIVER driver;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&driverConfig, SofPlayEvtDeviceAdd);
    status = WdfDriverCreate(DriverObject, RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES, &driverConfig, &driver);
    if (!NT_SUCCESS(status)) return status;
    ACX_DRIVER_CONFIG_INIT(&acxConfig);
    return AcxDriverInitialize(driver, &acxConfig);
}
#endif
