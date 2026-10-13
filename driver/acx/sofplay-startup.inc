// SPDX-License-Identifier: GPL-2.0-only
// Included by sofprobe.c after the bounded firmware/DMA/IPC hardware helpers.
// The ACX driver owns startup; the standalone diagnostic probe remains opt-in.

#define SOFPLAY_FIRMWARE_BYTES 525056UL
#define SOFPLAY_FIRMWARE_TAG 'fFoS'

static VOID
SofPlaySaveStartupResult(_In_ WDFDEVICE Device, _In_ NTSTATUS Status)
{
    PSOFPROBE_DEVICE_CONTEXT context = SofProbeGetContext(Device);
    WDFKEY key;
    context->AcxStartupStatus = Status;
    if (NT_SUCCESS(WdfDeviceOpenRegistryKey(Device, PLUGPLAY_REGKEY_DEVICE,
            KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        (VOID)SofProbeSaveValue(key, L"SofPlayStartupStage", context->AcxStartupStage);
        (VOID)SofProbeSaveValue(key, L"SofPlayStartupStatus", (ULONG)Status);
        WdfRegistryClose(key);
    }
}

static NTSTATUS
SofPlayLoadFirmware(_In_ PSOFPROBE_DEVICE_CONTEXT Context)
{
    // Pinned to the IPC3 image used by the successful N100 hardware tests.
    static const UCHAR expectedHash[32] = {
        0x84,0x33,0x39,0x0e,0x4a,0xbf,0xe4,0x0b,
        0x25,0x1f,0xf3,0xde,0xc4,0x16,0x04,0xe7,
        0xb8,0x17,0x50,0x1d,0x10,0x97,0xd5,0x1a,
        0xa2,0xf5,0x1c,0x11,0x44,0x4d,0x3b,0xf0
    };
    UNICODE_STRING path = RTL_CONSTANT_STRING(
        L"\\SystemRoot\\System32\\drivers\\sof-adl-n-ipc3.ri");
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK io;
    FILE_STANDARD_INFORMATION info;
    HANDLE file = NULL;
    BCRYPT_ALG_HANDLE algorithm = NULL;
    PUCHAR image = NULL;
    UCHAR hash[32];
    SOFPROBE_DMA_ALLOCATION_RESULT staged;
    NTSTATUS status;

    if (Context->FirmwarePayload != NULL) return STATUS_SUCCESS;
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwCreateFile(&file, FILE_READ_DATA | SYNCHRONIZE, &attributes,
        &io, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status)) goto finish;
    status = ZwQueryInformationFile(file, &io, &info, sizeof(info), FileStandardInformation);
    if (!NT_SUCCESS(status)) goto finish;
    if (info.EndOfFile.QuadPart != SOFPLAY_FIRMWARE_BYTES) {
        status = STATUS_INVALID_IMAGE_FORMAT;
        goto finish;
    }
    image = ExAllocatePool2(POOL_FLAG_NON_PAGED, SOFPLAY_FIRMWARE_BYTES,
                            SOFPLAY_FIRMWARE_TAG);
    if (image == NULL) { status = STATUS_INSUFFICIENT_RESOURCES; goto finish; }
    status = ZwReadFile(file, NULL, NULL, NULL, &io, image,
                        SOFPLAY_FIRMWARE_BYTES, NULL, NULL);
    if (!NT_SUCCESS(status)) goto finish;
    if (io.Information != SOFPLAY_FIRMWARE_BYTES) {
        status = STATUS_END_OF_FILE;
        goto finish;
    }
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(status)) goto finish;
    status = BCryptHash(algorithm, NULL, 0, image, SOFPLAY_FIRMWARE_BYTES,
                         hash, sizeof(hash));
    if (!NT_SUCCESS(status)) goto finish;
    if (RtlCompareMemory(hash, expectedHash, sizeof(hash)) != sizeof(hash)) {
        status = STATUS_INVALID_IMAGE_HASH;
        goto finish;
    }
    status = SofProbeStageFirmwareImage(Context, image, SOFPLAY_FIRMWARE_BYTES, &staged);
finish:
    if (algorithm != NULL) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (image != NULL) ExFreePoolWithTag(image, SOFPLAY_FIRMWARE_TAG);
    if (file != NULL) ZwClose(file);
    return status;
}

static NTSTATUS
SofPlaySendStartupIpc(_In_ PSOFPROBE_DEVICE_CONTEXT Context,
                       _In_reads_bytes_(Bytes) const ULONG *Words,
                       _In_ ULONG Bytes)
{
    SOFPROBE_IPC3_EXCHANGE_REQUEST request;
    SOFPROBE_IPC3_SESSION_MEMORY_RESULT reply;
    NTSTATUS status;
    if (Bytes < 8 || Bytes > sizeof(request.RequestWords) ||
        (Bytes & 3) != 0 || Words[0] != Bytes)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&request, sizeof(request));
    request.Version = 1;
    request.RequestSize = Bytes;
    RtlCopyMemory(request.RequestWords, Words, Bytes);
    status = SofProbeExchangeHardware(Context, &request,
        FIELD_OFFSET(SOFPROBE_IPC3_EXCHANGE_REQUEST, RequestWords) + Bytes, &reply);
    if (!NT_SUCCESS(status)) return status;
    return reply.Result == 0 && reply.ReplyError == 0
        ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
}

static NTSTATUS
SofPlayPreparePcm(_In_ PSOFPROBE_DEVICE_CONTEXT Context)
{
    ULONG pcm[27] = {108,0x60010000};
    SOFPROBE_TONE_RESULT tone;
    PHYSICAL_ADDRESS address;
    ULONG offset;
    NTSTATUS status;
    Context->AcxStartupStage = 4;
    status = SofProbeToneHardware(Context, &tone, 0);
    if (!NT_SUCCESS(status)) return status;
    if (tone.Result != 0) return STATUS_DEVICE_DATA_ERROR;
    address = WdfCommonBufferGetAlignedLogicalAddress(Context->ToneAudio);
    offset = (ULONG)((((ULONGLONG)address.QuadPart + PAGE_SIZE - 1) &
                      ~((ULONGLONG)PAGE_SIZE - 1)) - (ULONGLONG)address.QuadPart);
    Context->AcxRing = (PUCHAR)WdfCommonBufferGetAlignedVirtualAddress(Context->ToneAudio) + offset;
    // The diagnostic allocator seeds a tone. Ordinary app playback starts silent.
    RtlZeroMemory(Context->AcxRing, SOFPROBE_TONE_BYTES);
    Context->AcxStartupStage = 5;
    pcm[6] = 84; pcm[8] = tone.PageTableAddressLow;
    pcm[9] = SOFPROBE_TONE_PAGES; pcm[10] = SOFPROBE_TONE_BYTES;
    pcm[15] = 2; pcm[17] = 48000; pcm[18] = 0x00020001;
    pcm[19] = 0x00040004; pcm[20] = SOFPROBE_TONE_PERIOD_BYTES; pcm[21] = 1;
    return SofPlaySendStartupIpc(Context, pcm, sizeof(pcm));
}

static NTSTATUS
SofPlayPrepareSpeaker(_In_ PSOFPROBE_DEVICE_CONTEXT Context)
{
    // Same fixed IPC3 speaker graph verified by test-speaker-tone.ps1.
    static const ULONG pipeline[] = {48,0x30100000,5,1,0,0,1000,0,5000,0,0,1};
    static const ULONG buffer[] = {44,0x30200000,2,12,1,0,0,768,0x71,0,0};
    static const ULONG host[] = {76,0x30010000,0,1,1,0,0,36,0,2,2,0,2,0,0,0,0,0,0};
    static const ULONG dai[] = {80,0x30010000,4,2,1,0,0,36,0,0,2,0,2,0,0,0,0,1,1,0};
    static const ULONG config[] = {112,0x80010000,1,1,0x4001,0,0,0,0,0,0,0,0,60,
                                   0,24000000,48000,3072000,2,3,3,32,32,1,0,0,0,0};
    static const ULONG hostToBuffer[] = {16,0x30030000,0,2};
    static const ULONG bufferToDai[] = {16,0x30030000,2,4};
    static const ULONG complete[] = {12,0x30130000,5};
    const ULONG *messages[] = {pipeline,buffer,host,dai,config,hostToBuffer,bufferToDai,complete};
    ULONG i;
    NTSTATUS status;
    Context->AcxStartupStage = 3;
    for (i = 0; i < RTL_NUMBER_OF(messages); ++i) {
        status = SofPlaySendStartupIpc(Context, messages[i], messages[i][0]);
        if (!NT_SUCCESS(status)) return status;
    }
    return SofPlayPreparePcm(Context);
}

static NTSTATUS
SofPlayRearmPcmLocked(_In_ PSOFPROBE_DEVICE_CONTEXT Context)
{
    static const ULONG freePcm[] = {12,0x60030000,0};
    NTSTATUS status;
    if (Context->ToneDmaRunning || Context->ToneTriggerActive)
        return STATUS_DEVICE_BUSY;
    // Serialize new-buffer publication with packet copies and position reads.
    WdfSpinLockAcquire(Context->AcxBufferLock);
    Context->AcxHardwareReady = FALSE;
    Context->AcxRing = NULL;
    WdfSpinLockRelease(Context->AcxBufferLock);
    status = SofPlaySendStartupIpc(Context, freePcm, sizeof(freePcm));
    if (!NT_SUCCESS(status)) return status;
    if (SofProbeStopToneHardware(Context) != 0) return STATUS_DEVICE_DATA_ERROR;
    SofProbeFreeToneBuffers(Context);
    status = SofPlayPreparePcm(Context);
    if (!NT_SUCCESS(status)) return status;
    Context->AcxPcmNeedsReset = FALSE;
    ++Context->AcxPcmResets;
    Context->AcxStartupStage = 6;
    WdfSpinLockAcquire(Context->AcxBufferLock);
    Context->AcxHardwareReady = TRUE;
    WdfSpinLockRelease(Context->AcxBufferLock);
    return STATUS_SUCCESS;
}

NTSTATUS
SofPlayEvtD0Entry(_In_ WDFDEVICE Device, _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PSOFPROBE_DEVICE_CONTEXT context = SofProbeGetContext(Device);
    SOFPROBE_IPC3_SESSION_START_RESULT boot;
    SOFPROBE_IPC3_SESSION_STOP_RESULT stopped;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(PreviousState);
    WdfWaitLockAcquire(context->AcxPowerLock, NULL);
    ++context->AcxStartupCount;
    context->AcxStartupStage = 1;
    status = SofPlayLoadFirmware(context);
    if (!NT_SUCCESS(status)) goto finish;
    context->AcxStartupStage = 2;
    RtlZeroMemory(&boot, sizeof(boot));
    status = SofProbeBootHardware(Device, context, sizeof(boot), &boot,
                                   TRUE, TRUE, TRUE, TRUE, TRUE, TRUE);
    if (!NT_SUCCESS(status)) goto finish;
    if (((PSOFPROBE_ROM_ENTRY_RESULT)&boot)->Result != 0 || !boot.SessionActive) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto finish;
    }
    status = SofPlayPrepareSpeaker(context);
    if (!NT_SUCCESS(status)) goto finish;
    WdfSpinLockAcquire(context->AcxBufferLock);
    context->AcxHardwareReady = TRUE;
    WdfSpinLockRelease(context->AcxBufferLock);
    context->AcxPcmNeedsReset = FALSE;
    context->AcxStartupStage = 6;
finish:
    // WDF does not call D0Exit when D0Entry fails: unwind here as well.
    if (!NT_SUCCESS(status)) {
        context->AcxRing = NULL;
        if (context->SessionActive) SofProbeStopSessionHardware(context, &stopped);
    }
    SofPlaySaveStartupResult(Device, status);
    WdfWaitLockRelease(context->AcxPowerLock);
    return status;
}
