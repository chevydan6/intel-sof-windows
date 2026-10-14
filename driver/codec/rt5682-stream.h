/* SPDX-License-Identifier: GPL-2.0-only */
/* All entry points hold CodecLock. Poll only while a stream owns the output. */
#pragma once
static NTSTATUS CodecStreamOff(WDFDEVICE device)
{
    CODEC_CONTEXT *c = CodecContext(device);
    NTSTATUS status;
    c->StreamRequested = FALSE;
    c->StreamOwner = NULL;
    c->Playing = FALSE;
    c->JackPresent = FALSE;
    if (c->JackTimer != NULL) WdfTimerStop(c->JackTimer, FALSE);
    status = CodecResetMuted(device);
    c->Initialized = c->Prepared = FALSE;
    return status;
}

static NTSTATUS CodecWaitForJack(WDFDEVICE device)
{
    CODEC_CONTEXT *c = CodecContext(device);
    NTSTATUS status = CodecResetMuted(device);
    c->Initialized = c->Prepared = c->Playing = c->JackPresent = FALSE;
    if (NT_SUCCESS(status)) status = CodecConfigureJack(device);
    return status;
}

static NTSTATUS CodecStreamOn(WDFDEVICE device)
{
    CODEC_CONTEXT *c = CodecContext(device);
    NTSTATUS status = STATUS_SUCCESS;
    if (!c->Initialized) {
        status = CodecInitializeMuted(device);
        c->Initialized = NT_SUCCESS(status);
    }
    if (NT_SUCCESS(status) && !c->Prepared) {
        status = CodecPrepareHeadphoneMuted(device);
        c->Prepared = NT_SUCCESS(status);
    }
    if (NT_SUCCESS(status)) status = CodecEnableHeadphones(device, &c->AuditionStage, 0xaeae);
    if (status == STATUS_DEVICE_NOT_READY) {
        // No plug is a normal state: keep the stream running silently until insertion.
        status = CodecWaitForJack(device);
    } else if (NT_SUCCESS(status)) {
        c->Playing = c->JackPresent = TRUE;
    }
    if (!NT_SUCCESS(status)) (VOID)CodecStreamOff(device);
    return status;
}

static NTSTATUS CodecPollJack(WDFDEVICE device)
{
    CODEC_CONTEXT *c = CodecContext(device);
    ULONG value;
    NTSTATUS status = ReadRegister(device, RT5682_AJD1_CTRL, &value);
    if (!NT_SUCCESS(status)) goto fail;
    if (value & RT5682_JDH_RS_MASK) {
        if (c->Playing) status = CodecWaitForJack(device);
        c->JackPresent = FALSE;
    } else if (!c->Playing) {
        status = CodecStreamOn(device);
    }
    if (NT_SUCCESS(status)) return status;
fail:
    (VOID)CodecStreamOff(device);
    return status;
}
