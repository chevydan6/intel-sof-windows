// SPDX-License-Identifier: GPL-2.0-only
// Initial ACX render circuit for the N100 SOF speaker path.

#include <ntddk.h>
#include <wdf.h>
#include <acx.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmreg.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SofPlayEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SofPlayEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE SofPlayEvtReleaseHardware;
EVT_ACX_CIRCUIT_CREATE_STREAM SofPlayEvtCreateStream;

typedef struct _SOFPLAY_DEVICE_CONTEXT {
    ACXCIRCUIT RenderCircuit;
    BOOLEAN CircuitAdded;
} SOFPLAY_DEVICE_CONTEXT, *PSOFPLAY_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SOFPLAY_DEVICE_CONTEXT, SofPlayGetContext)

static const GUID SofPlaySpeakerComponent =
    { 0x39edc32d, 0xd57e, 0x47a9,
      { 0x84, 0x20, 0xbb, 0xa1, 0x08, 0x78, 0x5c, 0xf0 } };

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
        32, KSAUDIO_SPEAKER_STEREO,
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)
    }
};

static NTSTATUS
SofPlayCreateRenderCircuit(_In_ WDFDEVICE Device,
                           _Out_ ACXCIRCUIT *Circuit)
{
    PACXCIRCUIT_INIT circuitInit;
    ACXCIRCUIT circuit = NULL;
    ACXPIN pins[2] = { NULL, NULL };
    ACX_PIN_CONFIG pinConfig;
    ACX_DATAFORMAT_CONFIG formatConfig;
    ACXDATAFORMAT format = NULL;
    ACXDATAFORMATLIST formats;
    WDF_OBJECT_ATTRIBUTES attributes;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"N100 SOF Speakers");
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
    pinConfig.Category = &KSCATEGORY_AUDIO;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = circuit;
    status = AcxPinCreate(circuit, &attributes, &pinConfig, &pins[0]);
    if (!NT_SUCCESS(status)) goto finish;

    ACX_PIN_CONFIG_INIT(&pinConfig);
    pinConfig.Type = AcxPinTypeSource;
    pinConfig.Communication = AcxPinCommunicationNone;
    pinConfig.Category = &KSNODETYPE_SPEAKER;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = circuit;
    status = AcxPinCreate(circuit, &attributes, &pinConfig, &pins[1]);
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
    status = AcxDataFormatListAddDataFormat(formats, format);
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
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Circuit);
    UNREFERENCED_PARAMETER(Pin);
    UNREFERENCED_PARAMETER(StreamInit);
    UNREFERENCED_PARAMETER(StreamFormat);
    UNREFERENCED_PARAMETER(SignalProcessingMode);
    UNREFERENCED_PARAMETER(VarArguments);
    // Hardware stream creation is added after circuit enumeration is verified.
    return STATUS_NOT_SUPPORTED;
}

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
