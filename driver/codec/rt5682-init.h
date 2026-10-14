/* SPDX-License-Identifier: GPL-2.0-only */
/* Adapted from Linux v6.8 rt5682.c / rt5682-i2c.c.
 * Copyright 2018 Realtek Semiconductor Corp.
 * Original author: Bard Liao <bardliao@realtek.com>
 * Windows adaptation: bounded calibration, checked transfers, muted output.
 */
#pragma once
#include "rt5682-registers.h"

typedef struct { USHORT Reg, Value; ULONG DelayMs; } CODEC_SETTING;

static NTSTATUS CodecApply(WDFDEVICE device, const CODEC_SETTING *settings, ULONG count)
{
    ULONG i;
    NTSTATUS status;
    for (i = 0; i < count; ++i) {
        status = WriteRegister(device, settings[i].Reg, settings[i].Value);
        if (!NT_SUCCESS(status)) return status;
        if (settings[i].DelayMs) CodecDelay(settings[i].DelayMs);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS CodecResetMuted(WDFDEVICE device)
{
    // Best effort across ALL writes: do not skip mute/power-down after one
    // failed transfer. Preserve the first failure instead of claiming success.
    static const CODEC_SETTING off[] = {
        {RT5682_HP_CTRL_1, 0x8080, 0},
        {RT5682_HP_CTRL_2, 0x8000, 0},
        {RT5682_RESET, 0, 0},
        {RT5682_I2C_MODE, 1, 10},
        {RT5682_HP_CTRL_1, 0x8080, 0},
        {RT5682_HP_CTRL_2, 0x8000, 0},
        {RT5682_PWR_DIG_1, 0, 0},
        {RT5682_PWR_DIG_2, 0, 0},
        {RT5682_PWR_ANLG_1, 0x002c, 0},
        {RT5682_PWR_ANLG_2, 0, 0},
        {RT5682_PWR_ANLG_3, 0, 0},
        {RT5682_DEPOP_1, 0, 0},
    };
    NTSTATUS first = STATUS_SUCCESS, status;
    ULONG i;
    for (i = 0; i < RTL_NUMBER_OF(off); ++i) {
        status = WriteRegister(device, off[i].Reg, off[i].Value);
        if (!NT_SUCCESS(status) && NT_SUCCESS(first)) first = status;
        if (off[i].DelayMs) CodecDelay(off[i].DelayMs);
    }
    return first;
}

static NTSTATUS CodecInitializeMuted(WDFDEVICE device)
{
    // Calibration uses the codec's internal clock. No SSP clock or stream is
    // needed. Assert BOTH hardware mutes before entering the Linux sequence.
    static const CODEC_SETTING calibration[] = {
        {RT5682_I2C_CTRL, 0x000f, 0},
        {RT5682_PWR_ANLG_1, 0xa2af, 20},
        {RT5682_PWR_ANLG_1, 0xf2af, 0},
        {RT5682_MICBIAS_2, 0x0300, 0},
        {RT5682_GLB_CLK, 0x8000, 0},
        {RT5682_PWR_DIG_1, 0x0100, 0},
        {RT5682_HP_IMP_SENS_CTRL_19, 0x3800, 0},
        {RT5682_CHOP_DAC, 0x3000, 0},
        {RT5682_CALIB_ADC_CTRL, 0x7005, 0},
        {RT5682_STO1_ADC_MIXER, 0x686c, 0},
        {RT5682_CAL_REC, 0x0d0d, 0},
        {RT5682_HP_CALIB_CTRL_2, 0x0321, 0},
        {RT5682_HP_LOGIC_CTRL_2, 0x0004, 0},
        {RT5682_HP_CALIB_CTRL_1, 0x7c00, 0},
        {RT5682_HP_CALIB_CTRL_3, 0x06a1, 0},
        {RT5682_A_DAC1_MUX, 0x0311, 0},
        {RT5682_HP_CALIB_CTRL_1, 0x7c00, 0},
        {RT5682_HP_CALIB_CTRL_1, 0xfc00, 0},
    };
    static const CODEC_SETTING restoreAndPatch[] = {
        {RT5682_PWR_ANLG_1, 0x002f, 0},
        {RT5682_MICBIAS_2, 0x0080, 0},
        {RT5682_GLB_CLK, 0, 0},
        {RT5682_PWR_DIG_1, 0, 0},
        {RT5682_CHOP_DAC, 0x2000, 0},
        {RT5682_CALIB_ADC_CTRL, 0x2005, 0},
        {RT5682_STO1_ADC_MIXER, 0xc0c4, 0},
        {RT5682_CAL_REC, 0x0c0c, 0},
        {RT5682_HP_IMP_SENS_CTRL_19, 0x1000, 0},
        {RT5682_DAC_ADC_DIG_VOL1, 0xa020, 0},
        {RT5682_I2C_CTRL, 0x000f, 0},
        {RT5682_PLL2_INTERNAL, 0x8266, 0},
        {RT5682_SAR_IL_CMD_1, 0x22b7, 0},
        {RT5682_SAR_IL_CMD_3, 0x0365, 0},
        {RT5682_SAR_IL_CMD_6, 0x0110, 0},
        {RT5682_CHARGE_PUMP_1, 0x0210, 0},
        {RT5682_HP_LOGIC_CTRL_2, 0x0007, 0},
        {RT5682_SAR_IL_CMD_2, 0xac00, 0},
        {RT5682_CBJ_CTRL_7, 0x0104, 0},
        {RT5682_DEPOP_1, 0, 0},
        // Quiescent endpoint after calibration. Playback is not implemented.
        {RT5682_HP_CTRL_1, 0x8080, 0},
        {RT5682_HP_CTRL_2, 0x8000, 0},
        {RT5682_PWR_ANLG_1, 0x002c, 0},
        {RT5682_PWR_ANLG_2, 0, 0},
        {RT5682_PWR_ANLG_3, 0, 0},
    };
    ULONG value = 0, count;
    NTSTATUS status = CodecResetMuted(device);
    if (!NT_SUCCESS(status)) goto fail;
    status = ReadRegister(device, RT5682_HP_CTRL_1, &value);
    if (!NT_SUCCESS(status)) goto fail;
    if ((value & 0x8080) != 0x8080) { status = STATUS_DEVICE_DATA_ERROR; goto fail; }
    status = ReadRegister(device, RT5682_HP_CTRL_2, &value);
    if (!NT_SUCCESS(status)) goto fail;
    if ((value & 0xe000) != 0x8000) { status = STATUS_DEVICE_DATA_ERROR; goto fail; }
    status = CodecApply(device, calibration, RTL_NUMBER_OF(calibration));
    if (!NT_SUCCESS(status)) goto fail;
    for (count = 0; count < 60; ++count) {
        status = ReadRegister(device, RT5682_HP_CALIB_STA_1, &value);
        if (!NT_SUCCESS(status)) goto fail;
        if (!(value & 0x8000)) break;
        CodecDelay(10);
    }
    if (count == 60) { status = STATUS_IO_TIMEOUT; goto fail; }
    status = CodecApply(device, restoreAndPatch, RTL_NUMBER_OF(restoreAndPatch));
    if (!NT_SUCCESS(status)) goto fail;
    return STATUS_SUCCESS;
fail:
    // Reset clears partial calibration; never advertise initialized state.
    (VOID)CodecResetMuted(device);
    return status;
}
