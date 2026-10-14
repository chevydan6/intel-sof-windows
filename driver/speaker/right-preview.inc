// SPDX-License-Identifier: GPL-2.0-only
// AcxPowerLock serializes all amp calls and preview state. The separate
// passive, non-periodic timer never performs GPIO I/O from the packet DPC.
static ULONGLONG SofRightNow(VOID) { return KeQueryInterruptTime() / 10000; }

static NTSTATUS SofRightAmp(PSOFPROBE_DEVICE_CONTEXT c, ULONG code)
{
    SOFAMP_STATUS out = {0};
    WDF_MEMORY_DESCRIPTOR buffer;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR returned = 0;
    NTSTATUS status;
    if (c->PreviewAmp == NULL) return STATUS_DEVICE_NOT_READY;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&buffer, &out, sizeof(out));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_MS(50));
    status = WdfIoTargetSendIoctlSynchronously(c->PreviewAmp, NULL, code,
        NULL, &buffer, &options, &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned != sizeof(out) || out.Version != 2 || !out.Ready ||
        out.Faulted || out.FaultStatus || out.CleanupStatus ||
        out.LeaseMs != 100 || out.MaximumMs != 2200)
        return STATUS_DEVICE_DATA_ERROR;
    if (code == IOCTL_SOFAMP_START_LISTEN || code == IOCTL_SOFAMP_RENEW) {
        if (!out.Active || out.OutputRequest != 2 || out.OnWrites != c->PreviewOnWrites + 1)
            return STATUS_DEVICE_DATA_ERROR;
    } else if (out.Active || out.OutputRequest != 1) return STATUS_DEVICE_DATA_ERROR;
    c->PreviewAmpWrites = out.OnWrites;
    if (code == IOCTL_SOFAMP_QUERY) c->PreviewOnWrites = out.OnWrites;
    return STATUS_SUCCESS;
}

static NTSTATUS SofRightPreviewStop(PSOFPROBE_DEVICE_CONTEXT c, NTSTATUS reason)
{
    NTSTATUS status = STATUS_SUCCESS;
    if (c->Preview.Phase != 0) c->Preview.Phase = 4;
    if (!NT_SUCCESS(reason)) c->Preview.Error = reason;
    // No waiting under AcxPowerLock: the callback acquires that same lock.
    if (c->PreviewTimer != NULL) WdfTimerStop(c->PreviewTimer, FALSE);
    if (c->PreviewAmp != NULL) status = SofRightAmp(c, IOCTL_SOFAMP_STOP);
    if (!NT_SUCCESS(status)) c->Preview.Error = status;
    WdfSpinLockAcquire(c->AcxBufferLock);
    if (c->AcxRing != NULL) RtlZeroMemory(c->AcxRing, SOFPROBE_TONE_BYTES);
    WdfSpinLockRelease(c->AcxBufferLock);
    return status;
}

static NTSTATUS SofRightPreviewBegin(PSOFPROBE_DEVICE_CONTEXT c)
{
    if (c->Preview.Phase != 1) return STATUS_INVALID_DEVICE_STATE;
    c->Preview.Phase = 2;
    c->Preview.Started = c->Preview.LastCheck = SofRightNow();
    c->Preview.Position = 0;
    WdfTimerStart(c->PreviewTimer, WDF_REL_TIMEOUT_IN_MS(20));
    return STATUS_SUCCESS;
}

static EVT_WDF_TIMER SofRightPreviewTick;
static VOID SofRightPreviewTick(WDFTIMER timer)
{
    PSOFPROBE_DEVICE_CONTEXT c = SofProbeGetContext(WdfTimerGetParentObject(timer));
    ULONGLONG now, position = 0;
    ULONG i;
    NTSTATUS status = STATUS_SUCCESS;
    WdfWaitLockAcquire(c->AcxPowerLock, NULL);
    if (c->Preview.Phase != 2 && c->Preview.Phase != 3) goto done;
    now = SofRightNow();
    // Check time before any possible enable/renew. Late callbacks cannot
    // extend a lease or start a fresh burst after the absolute deadline.
    if (now - c->Preview.Started >= SOF_RIGHT_PREVIEW_MS) {
        (VOID)SofRightPreviewStop(c, STATUS_SUCCESS);
        goto done;
    }
    if (now < c->Preview.LastCheck || now - c->Preview.LastCheck >= SOF_RIGHT_PREVIEW_GAP_MS) {
        status = STATUS_IO_TIMEOUT; goto stop;
    }
    WdfSpinLockAcquire(c->AcxBufferLock);
    status = SofProbeAcxAdvanceLocked(c);
    if (NT_SUCCESS(status)) {
        const LONG *samples = (const LONG *)c->AcxRing;
        position = c->AcxLinearPosition;
        for (i = 0; i < SOFPROBE_TONE_BYTES / sizeof(LONG); i += 2) {
            if (samples[i] != 0 || samples[i+1] > SOF_RIGHT_PREVIEW_PEAK ||
                samples[i+1] < -SOF_RIGHT_PREVIEW_PEAK) {
                status = STATUS_DATA_ERROR; break;
            }
        }
    }
    WdfSpinLockRelease(c->AcxBufferLock);
    if (!NT_SUCCESS(status)) goto stop;
    if (position <= c->Preview.Position || !c->ToneTriggerActive || !c->ToneDmaRunning) {
        status = STATUS_DEVICE_DATA_ERROR; goto stop;
    }
    if (c->Preview.Phase == 2) {
        if (c->Preview.Powered) {
            status = SofRightAmp(c, IOCTL_SOFAMP_START_LISTEN);
            if (!NT_SUCCESS(status)) goto stop;
        }
        c->Preview.Phase = 3;
    } else {
        if (!SofRightPreviewCanContinue(&c->Preview, now, position)) {
            status = STATUS_IO_TIMEOUT; goto stop;
        }
        if (c->Preview.Powered) {
            status = SofRightAmp(c, IOCTL_SOFAMP_RENEW);
            if (!NT_SUCCESS(status)) goto stop;
        }
    }
    c->Preview.LastCheck = now;
    c->Preview.Position = position;
    WdfTimerStart(timer, WDF_REL_TIMEOUT_IN_MS(20));
    goto done;
stop:
    (VOID)SofRightPreviewStop(c, status);
done:
    WdfWaitLockRelease(c->AcxPowerLock);
}

static NTSTATUS SofRightPreviewCreate(WDFDEVICE device)
{
    WDF_TIMER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_TIMER_CONFIG_INIT(&config, SofRightPreviewTick);
    config.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    return WdfTimerCreate(&config, &attributes, &SofProbeGetContext(device)->PreviewTimer);
}

static NTSTATUS SofRightPreviewOpen(WDFDEVICE device)
{
    PSOFPROBE_DEVICE_CONTEXT c = SofProbeGetContext(device);
    PWSTR links = NULL;
    UNICODE_STRING name;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS open;
    NTSTATUS status = IoGetDeviceInterfaces(&SofAmpInterfaceGuid, NULL, 0, &links);
    if (!NT_SUCCESS(status)) return status;
    if (links == NULL || !links[0]) { status = STATUS_DEVICE_NOT_READY; goto finish; }
    RtlInitUnicodeString(&name, links);
    if (links[name.Length / sizeof(WCHAR) + 1]) { status = STATUS_DEVICE_CONFIGURATION_ERROR; goto finish; }
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ParentObject = device;
    status = WdfIoTargetCreate(device, &attributes, &c->PreviewAmp);
    if (!NT_SUCCESS(status)) goto finish;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &name, GENERIC_READ | GENERIC_WRITE);
    open.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
    status = WdfIoTargetOpen(c->PreviewAmp, &open);
    if (NT_SUCCESS(status)) status = SofRightAmp(c, IOCTL_SOFAMP_QUERY);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(c->PreviewAmp); c->PreviewAmp = NULL;
    }
finish:
    if (links != NULL) ExFreePool(links);
    return status;
}

static VOID SofRightPreviewControl(WDFDEVICE device, WDFREQUEST request,
                                   ULONG code, size_t inputBytes, size_t outputBytes)
{
    PSOFPROBE_DEVICE_CONTEXT c = SofProbeGetContext(device);
    SOF_RIGHT_PREVIEW_STATUS *out;
    ULONG *powered = NULL;
    NTSTATUS status;
    if (outputBytes < sizeof(*out) || inputBytes !=
        (code == IOCTL_SOFPROBE_RIGHT_PREVIEW_ARM ? sizeof(ULONG) : 0)) {
        WdfRequestComplete(request, STATUS_INVALID_PARAMETER); return;
    }
    status = WdfRequestRetrieveOutputBuffer(request, sizeof(*out), (PVOID *)&out, NULL);
    if (!NT_SUCCESS(status)) { WdfRequestComplete(request, status); return; }
    if (code == IOCTL_SOFPROBE_RIGHT_PREVIEW_ARM) {
        status = WdfRequestRetrieveInputBuffer(request, sizeof(ULONG), (PVOID *)&powered, NULL);
        if (!NT_SUCCESS(status)) { WdfRequestComplete(request, status); return; }
        if (*powered > 1) { WdfRequestComplete(request, STATUS_INVALID_PARAMETER); return; }
    }
    WdfWaitLockAcquire(c->AcxPowerLock, NULL);
    if (powered != NULL) {
        if (c->Preview.Phase != 0 || c->SessionActive || c->AcxStreamClaimed || !c->ResourcesReady) {
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            status = SofRightPreviewOpen(device);
            if (NT_SUCCESS(status)) {
                c->Preview.Powered = *powered != 0;
                c->Preview.Phase = 1;
            }
        }
    }
    out->Version = 1; out->Phase = c->Preview.Phase;
    out->Powered = c->Preview.Powered; out->Error = (ULONG)c->Preview.Error;
    out->OnWrites = c->PreviewAmpWrites; out->MaximumMs = (ULONG)SOF_RIGHT_PREVIEW_MS;
    WdfWaitLockRelease(c->AcxPowerLock);
    WdfRequestCompleteWithInformation(request, status, NT_SUCCESS(status) ? sizeof(*out) : 0);
}
