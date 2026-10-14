// SPDX-License-Identifier: GPL-2.0-only
// One-shot PUJJO user-facing camera DMIC0 diagnostic. Requires the headphone
// build and its amplifier hold. No speakers, codec gain writes or user tuning.
// PDM0 A+B: coreboot brya/variants/baseboard/nissa/gpio.c GPP_R4/R5.
// IPC3 clock limits: sof-adl-max98360a-rt5682 topology, DMIC0 48kHz.

static NTSTATUS
SofMicIpc(PSOFPROBE_DEVICE_CONTEXT Context, const ULONG *Words,
          PSOFMIC_CAPTURE_RESULT Result)
{
    SOFPROBE_IPC3_SESSION_MEMORY_RESULT reply;
    NTSTATUS status;
    RtlZeroMemory(&reply, sizeof(reply));
    status = SofProbeExchangeMessage(Context, Words, Words[0], &reply);
    if (!NT_SUCCESS(status) || reply.Result != 0 || reply.ReplyError != 0) {
        Result->Command = Words[1];
        Result->Component = Words[2];
        Result->ReplyError = (ULONG)reply.ReplyError;
        return NT_SUCCESS(status) ? STATUS_DEVICE_DATA_ERROR : status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
SofMicGraph(PSOFPROBE_DEVICE_CONTEXT Context, PSOFMIC_CAPTURE_RESULT Result)
{
    // Separate IDs and pipeline from headphone playback. Capture flows DAI->host.
    static const ULONG pipeline[] = {48,0x30100000,15,2,14,0,1000,0,5000,0,0,1};
    static const ULONG buffer[] = {44,0x30200000,12,12,2,0,0,768,0x71,0,0};
    static const ULONG host[] = {76,0x30010000,10,1,2,0,0,36,0,2,2,0,2,0,0,0,1,0,0};
    static const ULONG dai[] = {80,0x30010000,14,2,2,0,0,36,0,2,0,0,2,0,0,0,1,0,2,0};
    // sof_ipc_dai_config 52 bytes + DMIC parameters 164 bytes.
    // Only controller 0, mic A+B. Other PDM controllers are disabled.
    static const ULONG config[54] = {
        216,0x80010000,2,0,0,0,0,0,0,0,0,0,0,
        164,1,2400000,4800000,48000,0,32,0x003c0028,
        1,0,0,200,0,0,0,0,0,
        24,0x00010000,1,0,0,0
    };
    static const ULONG daiToBuffer[] = {16,0x30030000,14,12};
    static const ULONG bufferToHost[] = {16,0x30030000,12,10};
    static const ULONG complete[] = {12,0x30130000,15};
    const ULONG *messages[] = {pipeline,buffer,host,dai,config,daiToBuffer,bufferToHost,complete};
    ULONG i;
    NTSTATUS status;
    for (i = 0; i < RTL_NUMBER_OF(messages); ++i) {
        Result->Stage = 10 + i;
        status = SofMicIpc(Context, messages[i], Result);
        if (!NT_SUCCESS(status)) return status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
SofMicRestoreInput(PUCHAR Hda)
{
    LARGE_INTEGER delay;
    ULONG i, pp;
    // Only input stream 0 and its decouple bit; preserve output stream state.
    WRITE_REGISTER_UCHAR(Hda + 0x80, 0);
    delay.QuadPart = -10000;
    for (i = 0; i < 300; ++i) {
        if ((READ_REGISTER_ULONG((PULONG)(Hda + 0x80)) & 3) == 0) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    if (i == 300) return STATUS_IO_TIMEOUT;
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG((PULONG)(Hda + 0x98), 0);
    WRITE_REGISTER_ULONG((PULONG)(Hda + 0x9c), 0);
    WRITE_REGISTER_ULONG((PULONG)(Hda + 0x88), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(Hda + 0x8c), 0);
    WRITE_REGISTER_USHORT((PUSHORT)(Hda + 0x92), 0);
    WRITE_REGISTER_UCHAR(Hda + 0x83, 0x1c);
    WRITE_REGISTER_ULONG((PULONG)(Hda + 0x80), 0x00040000);
    pp = READ_REGISTER_ULONG((PULONG)(Hda + 0x804));
    WRITE_REGISTER_ULONG((PULONG)(Hda + 0x804), pp & ~1UL);
    if ((READ_REGISTER_ULONG((PULONG)(Hda + 0x80)) & 0xffffff) != 0x40000 ||
        READ_REGISTER_ULONG((PULONG)(Hda + 0x98)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(Hda + 0x9c)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(Hda + 0x88)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(Hda + 0x8c)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(Hda + 0x92)) != 0 ||
        (READ_REGISTER_ULONG((PULONG)(Hda + 0x804)) & 1) != 0)
        return STATUS_DEVICE_DATA_ERROR;
    return STATUS_SUCCESS;
}

static VOID
SofMicCapture(WDFDEVICE Device, WDFREQUEST Request, size_t InputBytes, size_t OutputBytes)
{
    PSOFPROBE_DEVICE_CONTEXT context = SofProbeGetContext(Device);
    PSOFMIC_CAPTURE_RESULT result;
    WDFCOMMONBUFFER bdl = NULL, audio = NULL, table = NULL;
    PHYSICAL_ADDRESS ba, aa, ta;
    SOFPROBE_BDL_ENTRY *entries;
    PUCHAR data = NULL, pages, hda = NULL;
    ULONGLONG aligned, started = 0, lastTime = 0, now, gap;
    ULONG offset, i, pfn, index, pos, lastPos = 0, count, part;
    ULONG pcm[27] = {108,0x60010000,10};
    static const ULONG trigger[] = {12,0x60040000,10};
    static const ULONG stop[] = {12,0x60050000,10};
    static const ULONG freePcm[] = {12,0x60030000,10};
    BOOLEAN touched = FALSE, configured = FALSE, triggered = FALSE;
    LARGE_INTEGER delay;
    NTSTATUS status, cleanup = STATUS_SUCCESS, step;

    if (InputBytes != 0 || OutputBytes < sizeof(*result)) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*result), (PVOID *)&result, NULL);
    if (!NT_SUCCESS(status)) { WdfRequestComplete(Request, status); return; }
    RtlZeroMemory(result, sizeof(*result));
    result->Version = 1; result->Rate = 48000; result->Channels = 2; result->Bits = 32;
    result->Stage = 1;
    if (!context->ResourcesReady) { status = STATUS_DEVICE_NOT_READY; goto complete; }
    // Boot the verified muted headphone transport without starting playback.
    status = SofPlayStartPlaybackHardware(Device, WdfPowerDeviceD3);
    if (!NT_SUCCESS(status)) goto complete;
    WdfWaitLockAcquire(context->AcxPowerLock, NULL);
    if (context->MicAttempted || !context->AcxHardwareReady || context->ToneHda == NULL) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto finish;
    }
    context->MicAttempted = TRUE; // Until a verified DSP reset, IDs cannot be reused.
    hda = context->ToneHda;
    result->Stage = 2;
    if (READ_REGISTER_USHORT((PUSHORT)hda) != 0x9701 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x80)) != 0x40000 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x88)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c)) != 0 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x92)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x98)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x9c)) != 0 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x804)) != 0x40000080) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish;
    }
    status = WdfCommonBufferCreate(context->DmaEnabler, 128, WDF_NO_OBJECT_ATTRIBUTES, &bdl);
    if (!NT_SUCCESS(status)) goto finish;
    status = WdfCommonBufferCreate(context->DmaEnabler32, SOFPROBE_TONE_BYTES + PAGE_SIZE, WDF_NO_OBJECT_ATTRIBUTES, &audio);
    if (!NT_SUCCESS(status)) goto finish;
    status = WdfCommonBufferCreate(context->DmaEnabler32, 128, WDF_NO_OBJECT_ATTRIBUTES, &table);
    if (!NT_SUCCESS(status)) goto finish;
    ba = WdfCommonBufferGetAlignedLogicalAddress(bdl);
    aa = WdfCommonBufferGetAlignedLogicalAddress(audio);
    ta = WdfCommonBufferGetAlignedLogicalAddress(table);
    aligned = (aa.QuadPart + PAGE_SIZE - 1) & ~((ULONGLONG)PAGE_SIZE - 1);
    offset = (ULONG)(aligned - aa.QuadPart);
    if ((ba.QuadPart & 127) != 0 || (ULONGLONG)ta.QuadPart > MAXULONG ||
        aligned + SOFPROBE_TONE_BYTES - 1 > MAXULONG ||
        offset + SOFPROBE_TONE_BYTES > WdfCommonBufferGetLength(audio)) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish;
    }
    data = (PUCHAR)WdfCommonBufferGetAlignedVirtualAddress(audio) + offset;
    pages = (PUCHAR)WdfCommonBufferGetAlignedVirtualAddress(table);
    RtlZeroMemory(data, SOFPROBE_TONE_BYTES);
    RtlZeroMemory(pages, 128);
    for (i = 0; i < SOFPROBE_TONE_PAGES; ++i) {
        pfn = (ULONG)(aligned >> PAGE_SHIFT) + i; index = (5 * i) >> 1;
        if (i & 1) {
            pages[index] = (UCHAR)((pages[index] & 15) | ((pfn & 15) << 4));
            pages[index+1] = (UCHAR)(pfn >> 4); pages[index+2] = (UCHAR)(pfn >> 12);
        } else {
            pages[index] = (UCHAR)pfn; pages[index+1] = (UCHAR)(pfn >> 8);
            pages[index+2] = (UCHAR)(pfn >> 16);
        }
    }
    entries = (SOFPROBE_BDL_ENTRY *)WdfCommonBufferGetAlignedVirtualAddress(bdl);
    RtlZeroMemory(entries, 128);
    for (i = 0; i < 4; ++i) {
        entries[i].Address = aligned + i * SOFPROBE_TONE_PERIOD_BYTES;
        entries[i].Length = SOFPROBE_TONE_PERIOD_BYTES;
    }
    result->Stage = 3; touched = TRUE; delay.QuadPart = -10000;
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80), 0x40001);
    for (i = 0; i < 50; ++i) {
        if (READ_REGISTER_ULONG((PULONG)(hda + 0x80)) & 1) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    if (i == 50) { status = STATUS_IO_TIMEOUT; goto finish; }
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80), 0x40000);
    for (i = 0; i < 50; ++i) {
        if (!(READ_REGISTER_ULONG((PULONG)(hda + 0x80)) & 1)) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    if (i == 50) { status = STATUS_IO_TIMEOUT; goto finish; }
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80), 0x00140000); // Input stream 0 uses tag 1, independently of output tags.
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x804), 0x40000081);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x88), SOFPROBE_TONE_BYTES);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x8c), 3);
    WRITE_REGISTER_USHORT((PUSHORT)(hda + 0x92), 0x41); // Decoupled host DMA, like playback.
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x98), ba.LowPart);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x9c), ba.HighPart);
    if (READ_REGISTER_ULONG((PULONG)(hda + 0x80)) != 0x00140000 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x804)) != 0x40000081 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x88)) != SOFPROBE_TONE_BYTES ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x8c)) != 3 ||
        READ_REGISTER_USHORT((PUSHORT)(hda + 0x92)) != 0x41 ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x98)) != ba.LowPart ||
        READ_REGISTER_ULONG((PULONG)(hda + 0x9c)) != (ULONG)ba.HighPart) {
        status = STATUS_DEVICE_DATA_ERROR; goto finish;
    }
    status = SofMicGraph(context, result);
    if (!NT_SUCCESS(status)) goto finish;
    result->Stage = 20;
    pcm[6] = 84; pcm[8] = ta.LowPart; pcm[9] = 12; pcm[10] = SOFPROBE_TONE_BYTES;
    pcm[14] = 1; pcm[15] = 2; pcm[17] = 48000; pcm[18] = 0x00020001;
    pcm[19] = 0x00040004; pcm[20] = SOFPROBE_TONE_PERIOD_BYTES; pcm[21] = 1;
    // Retain buffers on an ambiguous firmware reply until device reset.
    configured = TRUE;
    status = SofMicIpc(context, pcm, result);
    if (!NT_SUCCESS(status)) goto finish;
    result->Stage = 21;
    WRITE_REGISTER_UCHAR(hda + 0x83, 0x1c);
    WRITE_REGISTER_ULONG((PULONG)(hda + 0x80), 0x00140002);
    for (i = 0; i < 50; ++i) {
        if (READ_REGISTER_ULONG((PULONG)(hda + 0x80)) & 2) break;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    if (i == 50) { status = STATUS_IO_TIMEOUT; goto finish; }
    triggered = TRUE;
    status = SofMicIpc(context, trigger, result);
    if (!NT_SUCCESS(status)) goto finish;
    result->Stage = 22;
    started = lastTime = KeQueryInterruptTime();
    while (result->Bytes < SOFMIC_CAPTURE_BYTES) {
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        now = KeQueryInterruptTime(); gap = now - lastTime; lastTime = now;
        if (gap / 10 > result->MaxPollUs) result->MaxPollUs = (ULONG)(gap / 10);
        result->ElapsedMs = (ULONG)((now - started) / 10000);
        // Ring wraps in 128ms: reject pauses well before ambiguity/overwrite.
        if (gap >= 500000 || now - started > 60000000) { status = STATUS_IO_TIMEOUT; goto finish; }
        if (WdfRequestIsCanceled(Request)) { status = STATUS_CANCELLED; goto finish; }
        pos = READ_REGISTER_ULONG((PULONG)(hda + 0x84));
        result->Position = pos; result->HdaStatus = READ_REGISTER_UCHAR(hda + 0x83);
        if (pos >= SOFPROBE_TONE_BYTES || (result->HdaStatus & 0x18)) {
            status = STATUS_DEVICE_DATA_ERROR; goto finish;
        }
        pos &= ~7UL;
        count = (pos + SOFPROBE_TONE_BYTES - lastPos) % SOFPROBE_TONE_BYTES;
        if (count > SOFMIC_CAPTURE_BYTES - result->Bytes) count = SOFMIC_CAPTURE_BYTES - result->Bytes;
        part = SOFPROBE_TONE_BYTES - lastPos;
        if (part > count) part = count;
        KeMemoryBarrier();
        RtlCopyMemory(result->Samples + result->Bytes, data + lastPos, part);
        if (count > part) RtlCopyMemory(result->Samples + result->Bytes + part, data, count - part);
        result->Bytes += count; lastPos = (lastPos + count) % SOFPROBE_TONE_BYTES;
    }
    result->Stage = 23;
finish:
    // Always stop firmware and host DMA. Preserve the original failure details.
    if (triggered) {
        SOFMIC_CAPTURE_RESULT *scratch = result;
        ULONG command = result->Command, component = result->Component, error = result->ReplyError;
        step = SofMicIpc(context, stop, scratch);
        if (!NT_SUCCESS(step)) cleanup = step;
        if (!NT_SUCCESS(status)) { result->Command = command; result->Component = component; result->ReplyError = error; }
    }
    if (touched) {
        step = SofMicRestoreInput(hda);
        if (!NT_SUCCESS(step)) cleanup = step;
    }
    if (configured && NT_SUCCESS(cleanup)) {
        step = SofMicIpc(context, freePcm, result);
        if (!NT_SUCCESS(step)) cleanup = step;
    }
    if (!NT_SUCCESS(cleanup)) {
        context->MicRetainedBdl = bdl; context->MicRetainedAudio = audio; context->MicRetainedTable = table;
        bdl = audio = table = NULL;
    }
    if (table != NULL) WdfObjectDelete(table);
    if (audio != NULL) WdfObjectDelete(audio);
    if (bdl != NULL) WdfObjectDelete(bdl);
    result->CleanupStatus = (ULONG)cleanup;
    WdfWaitLockRelease(context->AcxPowerLock);
complete:
    result->Status = (ULONG)status;
    // Return bounded diagnostics even on failure; caller must check both statuses.
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
        FIELD_OFFSET(SOFMIC_CAPTURE_RESULT, Samples) + result->Bytes);
}
