// SPDX-License-Identifier: GPL-2.0-only
// Included by microphone.inc after SofMicIpc. One fixed speaker diagnostic per
// DSP session; replaces the idle headphone PCM until a verified device reset.
#include "../amp/amp-interface.h"
#include "speaker-test-samples.h"

typedef struct {
    WDFIOTARGET Amp;
    BOOLEAN Configured, Triggered, Checked, Pulsed, Stopped;
    ULONG Mode, InitialOnWrites, InitialPosition;
    ULONGLONG PulseStart;
    const LONG *Samples;
} SOFSPEAKER_CHECK;

static VOID
SofSpeakerSnapshot(PSOFPROBE_DEVICE_CONTEXT c, SOFSPEAKER_CHECK *test, ULONG position, ULONG bytes)
{
    SOFSPEAKER_SNAPSHOT *s = &c->SpeakerSnapshot;
    // The existing session maps the complete 1 MiB DSP BAR. SSP1 is at 0x11000;
    // use only control/status registers, never SSDR (the FIFO data port).
    s->Mode = test->Mode; s->HostPosition = position; s->CapturedBytes = bytes;
    if (c->SessionDsp == NULL) return;
    s->Sscr0 = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x11000));
    s->Sscr1 = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x11004));
    s->Sssr = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x11008));
    s->Sspsp = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x1102c));
    s->Sstsa = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x11030));
    s->Sscr2 = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x11040));
    s->Sspsp2 = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x11044));
    s->Sscr3 = READ_REGISTER_ULONG((PULONG)(c->SessionDsp + 0x11048));
    s->Version = 1;
}

static NTSTATUS
SofSpeakerAmpCommand(SOFSPEAKER_CHECK *test, ULONG code, SOFAMP_STATUS *out)
{
    WDF_MEMORY_DESCRIPTOR buffer;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR returned = 0;
    NTSTATUS status;
    RtlZeroMemory(out, sizeof(*out));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&buffer, out, sizeof(*out));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_MS(200));
    status = WdfIoTargetSendIoctlSynchronously(test->Amp, NULL, code,
        NULL, &buffer, &options, &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned != sizeof(*out) || out->Version != 1 || !out->Ready ||
        out->Faulted || out->FaultStatus || out->CleanupStatus ||
        out->LeaseMs != 100 || out->MaximumMs != 250)
        return STATUS_DEVICE_DATA_ERROR;
    if (code == IOCTL_SOFAMP_START) {
        if (!out->Active || out->OutputRequest != 2 ||
            out->OnWrites != test->InitialOnWrites + 1)
            return STATUS_DEVICE_DATA_ERROR;
    } else if (out->Active || out->OutputRequest != 1) return STATUS_DEVICE_DATA_ERROR;
    return STATUS_SUCCESS;
}

static NTSTATUS
SofSpeakerOpenAmp(WDFDEVICE device, SOFSPEAKER_CHECK *test)
{
    PWSTR links = NULL;
    UNICODE_STRING name;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS open;
    SOFAMP_STATUS state;
    NTSTATUS status = IoGetDeviceInterfaces(&SofAmpInterfaceGuid, NULL, 0, &links);
    if (!NT_SUCCESS(status)) return status;
    if (links == NULL || !links[0]) { status = STATUS_DEVICE_NOT_READY; goto done; }
    RtlInitUnicodeString(&name, links);
    if (links[name.Length / sizeof(WCHAR) + 1]) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto done; }
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ParentObject = device;
    status = WdfIoTargetCreate(device, &attributes, &test->Amp);
    if (!NT_SUCCESS(status)) goto done;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &name, GENERIC_READ | GENERIC_WRITE);
    open.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
    status = WdfIoTargetOpen(test->Amp, &open);
    if (!NT_SUCCESS(status)) { WdfObjectDelete(test->Amp); test->Amp = NULL; goto done; }
    status = SofSpeakerAmpCommand(test, IOCTL_SOFAMP_QUERY, &state);
    if (NT_SUCCESS(status)) test->InitialOnWrites = state.OnWrites;
done:
    if (links != NULL) ExFreePool(links);
    return status;
}

static NTSTATUS
SofSpeakerPrepare(WDFDEVICE device, SOFSPEAKER_CHECK *test, PSOFMIC_CAPTURE_RESULT result)
{
    PSOFPROBE_DEVICE_CONTEXT c = SofProbeGetContext(device);
    static const ULONG freeHeadphone[] = {12,0x60030000,0};
    static const ULONG pipeline[] = {48,0x30100000,25,3,20,0,1000,0,5000,0,0,1};
    static const ULONG buffer[] = {44,0x30200000,22,12,3,0,0,768,0x71,0,0};
    static const ULONG host[] = {76,0x30010000,20,1,3,0,0,36,0,2,2,0,2,0,0,0,0,0,0};
    static const ULONG dai[] = {80,0x30010000,24,2,3,0,0,36,0,0,2,0,2,0,0,0,0,1,1,0};
    static const ULONG config[] = {112,0x80010000,1,1,0x4001,0,0,0,0,0,0,0,0,60,
        0,19200000,48000,3072000,2,3,3,32,32,1,0,0,0,0};
    static const ULONG connect1[] = {16,0x30030000,20,22};
    static const ULONG connect2[] = {16,0x30030000,22,24};
    static const ULONG complete[] = {12,0x30130000,25};
    static const ULONG trigger[] = {12,0x60040000,20};
    const ULONG *messages[] = {pipeline,buffer,host,dai,config,connect1,connect2,complete};
    ULONG pcm[27] = {108,0x60010000,20};
    SOFPROBE_TONE_RESULT tone;
    PHYSICAL_ADDRESS table;
    PUCHAR ring;
    ULONG i;
    NTSTATUS status;
    if (test->Mode < 1 || test->Mode > 4 || c->SpeakerTestAttempted ||
        c->ToneDmaRunning || c->ToneTriggerActive || !c->TonePcmConfigured ||
        c->AcxRunCalls || InterlockedCompareExchange(&c->AcxStreamClaimed,0,0))
        return STATUS_INVALID_DEVICE_STATE;
    c->SpeakerTestAttempted = TRUE;
    RtlZeroMemory(&c->SpeakerSnapshot, sizeof(c->SpeakerSnapshot));
    status = SofSpeakerOpenAmp(device, test);
    if (!NT_SUCCESS(status)) return status;
    status = HeadphoneCodecCommand(c->HeadphoneCodec, IOCTL_CODEC_STOP);
    if (!NT_SUCCESS(status)) return status;
    WdfSpinLockAcquire(c->AcxBufferLock);
    ring = c->AcxRing;
    c->AcxHardwareReady = FALSE; c->AcxRing = NULL;
    WdfSpinLockRelease(c->AcxBufferLock);
    if (ring == NULL) return STATUS_DEVICE_NOT_READY;
    status = SofMicIpc(c, freeHeadphone, result);
    if (!NT_SUCCESS(status)) return status;
    c->TonePcmConfigured = FALSE;
    for (i = 0; i < RTL_NUMBER_OF(messages); ++i) {
        status = SofMicIpc(c, messages[i], result);
        if (!NT_SUCCESS(status)) return status;
    }
    SofSpeakerFillTestSamples((LONG *)ring, SOFPROBE_TONE_BYTES / 8,
                             (test->Mode - 1) & 1);
    test->Samples = (const LONG *)ring;
    KeMemoryBarrier();
    table = WdfCommonBufferGetAlignedLogicalAddress(c->TonePageTable);
    pcm[6]=84; pcm[8]=table.LowPart; pcm[9]=12; pcm[10]=SOFPROBE_TONE_BYTES;
    pcm[15]=2; pcm[17]=48000; pcm[18]=0x00020001;
    pcm[19]=0x00040004; pcm[20]=SOFPROBE_TONE_PERIOD_BYTES; pcm[21]=1;
    test->Configured = TRUE; // Ambiguous reply still needs cleanup.
    status = SofMicIpc(c, pcm, result);
    if (!NT_SUCCESS(status)) return status;
    KeMemoryBarrier();
    if (!SofSpeakerTestSamplesValid(test->Samples, SOFPROBE_TONE_BYTES / 8,
                                   (test->Mode - 1) & 1))
        return STATUS_DEVICE_DATA_ERROR;
    c->TonePcmConfigured = TRUE;
    status = SofProbeToneHardware(c, &tone, 1);
    if (!NT_SUCCESS(status) || tone.Result) return NT_SUCCESS(status) ? STATUS_DEVICE_DATA_ERROR : status;
    test->Triggered = TRUE;
    status = SofMicIpc(c, trigger, result);
    if (!NT_SUCCESS(status)) return status;
    test->InitialPosition = READ_REGISTER_ULONG((PULONG)(c->ToneHda + 0x164));
    return STATUS_SUCCESS;
}

static NTSTATUS
SofSpeakerTick(PSOFPROBE_DEVICE_CONTEXT c, SOFSPEAKER_CHECK *test, PSOFMIC_CAPTURE_RESULT result)
{
    SOFAMP_STATUS state;
    NTSTATUS status;
    ULONGLONG now;
    if (!test->Mode) return STATUS_SUCCESS;
    if (READ_REGISTER_UCHAR(c->ToneHda + 0x163) & 0x18) return STATUS_DEVICE_DATA_ERROR;
    // Wait for a full second of captured microphone data before the pulse.
    if (!test->Checked && result->Bytes >= 48000UL * 8) {
        ULONG pos = READ_REGISTER_ULONG((PULONG)(c->ToneHda + 0x164));
        if (pos >= SOFPROBE_TONE_BYTES || pos == test->InitialPosition)
            return STATUS_DEVICE_DATA_ERROR;
        KeMemoryBarrier();
        if (!SofSpeakerTestSamplesValid(test->Samples, SOFPROBE_TONE_BYTES / 8,
                                       (test->Mode - 1) & 1))
            return STATUS_DEVICE_DATA_ERROR;
        test->Checked = TRUE; result->Reserved |= 0xC00;
        SofSpeakerSnapshot(c, test, pos, result->Bytes);
        if (test->Mode >= 3) {
            test->PulseStart = KeQueryInterruptTime();
            status = SofSpeakerAmpCommand(test, IOCTL_SOFAMP_START, &state);
            if (!NT_SUCCESS(status)) { result->Command = IOCTL_SOFAMP_START; return status; }
            test->Pulsed = TRUE; result->Reserved |= 0x100;
        }
    }
    now = KeQueryInterruptTime();
    if (test->Pulsed && !test->Stopped && now - test->PulseStart >= 400000) {
        status = SofSpeakerAmpCommand(test, IOCTL_SOFAMP_STOP, &state);
        if (!NT_SUCCESS(status)) { result->Command = IOCTL_SOFAMP_STOP; return status; }
        test->Stopped = TRUE; result->Reserved |= 0x200;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
SofSpeakerStopOutput(PSOFPROBE_DEVICE_CONTEXT c, SOFSPEAKER_CHECK *test, PSOFMIC_CAPTURE_RESULT result)
{
    SOFAMP_STATUS state;
    static const ULONG stop[] = {12,0x60050000,20};
    LARGE_INTEGER delay;
    NTSTATUS status = STATUS_SUCCESS, step;
    ULONG i;
    // GPIO shutdown is first, before IPC waits or DMA cleanup.
    if (test->Amp != NULL) {
        status = SofSpeakerAmpCommand(test, IOCTL_SOFAMP_STOP, &state);
        if (NT_SUCCESS(status) && state.OnWrites != test->InitialOnWrites + (test->Pulsed ? 1UL : 0UL))
            status = STATUS_DEVICE_DATA_ERROR;
        WdfIoTargetClose(test->Amp); WdfObjectDelete(test->Amp); test->Amp = NULL;
    }
    if (test->Triggered) {
        step = SofMicIpc(c, stop, result);
        if (!NT_SUCCESS(step)) status = step;
        test->Triggered = FALSE;
    }
    if (test->Configured && c->ToneHda != NULL) {
        WRITE_REGISTER_UCHAR(c->ToneHda + 0x160, 0);
        delay.QuadPart = -10000;
        for (i=0;i<300;++i) {
            if (!(READ_REGISTER_UCHAR(c->ToneHda + 0x160) & 2)) break;
            (VOID)KeDelayExecutionThread(KernelMode,FALSE,&delay);
        }
        if (i==300) status = STATUS_IO_TIMEOUT;
        else c->ToneDmaRunning = FALSE;
    }
    return status;
}
