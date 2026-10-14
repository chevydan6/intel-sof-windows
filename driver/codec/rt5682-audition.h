/* SPDX-License-Identifier: GPL-2.0-only */
/* Fixed, three-second headphone audition derived from Realtek Linux v6.8
 * rt5682 headset detection, I2C probe defaults, DAPM supplies and hp_event.
 * Copyright 2018 Realtek Semiconductor Corp. Original author: Bard Liao.
 * No register, gain, duration or output-selection input is accepted.
 */
#pragma once
static NTSTATUS CodecAudition(WDFDEVICE device, ULONG *stage)
{
    ULONG value, count;
    NTSTATUS status, stopped;
#define UPDATE(reg, mask, val) do { status = CodecUpdate(device, reg, mask, val); if (!NT_SUCCESS(status)) goto finish; } while (0)
#define READ(reg) do { status = ReadRegister(device, reg, &value); if (!NT_SUCCESS(status)) goto finish; } while (0)
    *stage = 1;
    READ(RT5682_HP_CTRL_1);
    if ((value & 0x8080) != 0x8080) { status = STATUS_DEVICE_DATA_ERROR; goto finish; }
    READ(RT5682_HP_CTRL_2);
    if ((value & 0xe000) != 0x8000) { status = STATUS_DEVICE_DATA_ERROR; goto finish; }
    READ(RT5682_DAC1_DIG_VOL);
    if (value != 0x4646) { status = STATUS_DEVICE_DATA_ERROR; goto finish; }
    // Probe defaults required by the analog headphone path.
    UPDATE(RT5682_BIAS_CUR_CTRL_8, RT5682_HPA_CP_BIAS_CTRL_MASK, RT5682_HPA_CP_BIAS_3UA);
    UPDATE(RT5682_CHARGE_PUMP_1, RT5682_CP_CLK_HP_MASK, RT5682_CP_CLK_HP_300KHZ);
    UPDATE(RT5682_HP_CHARGE_PUMP_1, RT5682_PM_HP_MASK, RT5682_PM_HP_HV);
    // Configure JD1, but do not enable GPIO or button interrupts: this bounded
    // diagnostic polls jack presence while normal hotplug support is pending.
    *stage = 2;
    UPDATE(RT5682_CBJ_CTRL_5, 0x0700, 0x0600);
    UPDATE(RT5682_CBJ_CTRL_2, RT5682_EXT_JD_SRC, RT5682_EXT_JD_SRC_MANUAL);
    status = WriteRegister(device, RT5682_CBJ_CTRL_1, 0xd142);
    if (!NT_SUCCESS(status)) goto finish;
    UPDATE(RT5682_CBJ_CTRL_3, RT5682_CBJ_IN_BUF_EN, RT5682_CBJ_IN_BUF_EN);
    UPDATE(RT5682_SAR_IL_CMD_1, RT5682_SAR_POW_MASK, RT5682_SAR_POW_EN);
    UPDATE(RT5682_RC_CLK_CTRL, RT5682_POW_IRQ | RT5682_POW_JDH | RT5682_POW_ANA,
        RT5682_POW_IRQ | RT5682_POW_JDH | RT5682_POW_ANA);
    UPDATE(RT5682_PWR_ANLG_2, RT5682_PWR_JDH, RT5682_PWR_JDH);
    UPDATE(RT5682_IRQ_CTRL_2, RT5682_JD1_EN_MASK | RT5682_JD1_POL_MASK,
        RT5682_JD1_EN | RT5682_JD1_POL_NOR);
    // Leave GP1 unmapped to IRQ and allow the Linux jack debounce interval.
    CodecDelay(250);
    READ(RT5682_AJD1_CTRL);
    if (value & RT5682_JDH_RS_MASK) { status = STATUS_DEVICE_NOT_READY; goto finish; }
    *stage = 3;
    UPDATE(RT5682_PWR_ANLG_1, RT5682_PWR_VREF2 | RT5682_PWR_MB | RT5682_PWR_FV2,
        RT5682_PWR_VREF2 | RT5682_PWR_MB);
    CodecDelay(20);
    UPDATE(RT5682_PWR_ANLG_1, RT5682_PWR_FV2, RT5682_PWR_FV2);
    UPDATE(RT5682_PWR_ANLG_3, RT5682_PWR_CBJ, RT5682_PWR_CBJ);
    UPDATE(RT5682_HP_CHARGE_PUMP_1, RT5682_OSW_L_MASK | RT5682_OSW_R_MASK, 0);
    UPDATE(RT5682_IRQ_CTRL_3, RT5682_IL_IRQ_MASK, RT5682_IL_IRQ_DIS);
    UPDATE(RT5682_SAR_IL_CMD_1, RT5682_SAR_BUTT_DET_MASK, RT5682_SAR_BUTT_DET_DIS);
    UPDATE(RT5682_4BTN_IL_CMD_2, RT5682_4BTN_IL_MASK | RT5682_4BTN_IL_RST_MASK,
        RT5682_4BTN_IL_DIS | RT5682_4BTN_IL_RST);
    UPDATE(RT5682_SAR_IL_CMD_13, RT5682_SAR_SOUR_MASK, RT5682_SAR_SOUR_TYPE);
    UPDATE(RT5682_CBJ_CTRL_1, RT5682_TRIG_JD_MASK, RT5682_TRIG_JD_LOW);
    CodecDelay(55);
    UPDATE(RT5682_CBJ_CTRL_1, RT5682_TRIG_JD_MASK, RT5682_TRIG_JD_HIGH);
    for (count = 0; count < 50; ++count) {
        READ(RT5682_CBJ_CTRL_2);
        if (value & RT5682_JACK_TYPE_MASK) break;
        CodecDelay(10);
    }
    if (count == 50) { status = STATUS_IO_TIMEOUT; goto finish; }
    UPDATE(RT5682_HP_CHARGE_PUMP_1, RT5682_OSW_L_MASK | RT5682_OSW_R_MASK,
        RT5682_OSW_L_EN | RT5682_OSW_R_EN);
    UPDATE(RT5682_MICBIAS_2, RT5682_PWR_CLK25M_MASK | RT5682_PWR_CLK1M_MASK,
        RT5682_PWR_CLK25M_PU | RT5682_PWR_CLK1M_PU);
    *stage = 4;
    UPDATE(RT5682_PWR_ANLG_1, RT5682_PWR_HA_L | RT5682_PWR_HA_R,
        RT5682_PWR_HA_L | RT5682_PWR_HA_R);
    UPDATE(RT5682_DEPOP_1, RT5682_PUMP_EN | RT5682_CAPLESS_EN,
        RT5682_PUMP_EN | RT5682_CAPLESS_EN);
    // Keep the physical mute asserted until supplies and depop have settled.
    UPDATE(RT5682_HP_LOGIC_CTRL_2, RT5682_HP_LC2_SIG_SOUR2_MASK, RT5682_HP_LC2_SIG_SOUR2_REG);
    UPDATE(RT5682_DEPOP_1, 0x60, 0x60);
    UPDATE(RT5682_DAC_ADC_DIG_VOL1, 0x00c0, 0x0080);
    UPDATE(RT5682_HP_CTRL_2, RT5682_HP_C2_DAC_L_EN | RT5682_HP_C2_DAC_R_EN,
        RT5682_HP_C2_DAC_L_EN | RT5682_HP_C2_DAC_R_EN);
    CodecDelay(10);
    UPDATE(RT5682_CHARGE_PUMP_1, RT5682_CP_SW_SIZE_MASK, RT5682_CP_SW_SIZE_L);
    READ(RT5682_AJD1_CTRL);
    if (value & RT5682_JDH_RS_MASK) { status = STATUS_DEVICE_NOT_READY; goto finish; }
    READ(RT5682_DAC1_DIG_VOL);
    if (value != 0x4646) { status = STATUS_DEVICE_DATA_ERROR; goto finish; }
    *stage = 5;
    UPDATE(RT5682_HP_CTRL_1, 0x8080, 0);
    UPDATE(RT5682_HP_CTRL_2, RT5682_HP_C2_DAC_AMP_MUTE, 0);
    READ(RT5682_HP_CTRL_2);
    if ((value & 0xe000) != 0x6000) { status = STATUS_DEVICE_DATA_ERROR; goto finish; }
    *stage = 6;
    for (count = 0; count < 30; ++count) {
        CodecDelay(100);
        READ(RT5682_AJD1_CTRL);
        if (value & RT5682_JDH_RS_MASK) { status = STATUS_DEVICE_NOT_READY; goto finish; }
    }
    status = STATUS_SUCCESS;
finish:
    // Always mute and power down before returning, even after a failed I2C op.
    stopped = CodecResetMuted(device);
    if (NT_SUCCESS(status)) status = stopped;
    if (NT_SUCCESS(status)) *stage = 7;
    return status;
#undef READ
#undef UPDATE
}
