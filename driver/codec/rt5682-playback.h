/* SPDX-License-Identifier: GPL-2.0-only */
/* RT5682 fixed 48 kHz stereo headphone path, adapted from Realtek's Linux v6.8
 * codec initialization, hw_params, set_pll, set_filter_clk and DAPM controls.
 * Copyright 2018 Realtek Semiconductor Corp. Original author: Bard Liao.
 * Preparation deliberately leaves both physical headphone mutes asserted.
 */
#pragma once

static NTSTATUS CodecUpdate(WDFDEVICE device, USHORT reg, USHORT mask, USHORT value)
{
    ULONG old;
    NTSTATUS status = ReadRegister(device, reg, &old);
    if (!NT_SUCCESS(status)) return status;
    return WriteRegister(device, reg, (USHORT)((old & ~mask) | (value & mask)));
}

static NTSTATUS CodecPrepareHeadphoneMuted(WDFDEVICE device)
{
    // 19.2 MHz MCLK -> 24.576 MHz PLL1: exact rl6231.c preset K=3 N=30 M=3.
    // AIF1: I2S clock consumer, stereo 24-bit. SSP0 supplies the bit clock.
    static const CODEC_SETTING fixed[] = {
        {RT5682_HP_CTRL_1, 0x8080, 0},
        {RT5682_HP_CTRL_2, 0x8000, 0},
        {RT5682_MICBIAS_2, 0x0080, 0},
        {RT5682_TEST_MODE_CTRL_1, 0, 0},
        {RT5682_PLL_CTRL_1, (30 << RT5682_PLL_N_SFT) | 3, 0},
        {RT5682_PLL_CTRL_2, (3 << RT5682_PLL_M_SFT) | RT5682_PLL_RST, 0},
        // -39 dB DAC gain (35 steps above -65.25 dB), no app can exceed it.
        {RT5682_DAC1_DIG_VOL, 0x4646, 0},
        // Route IF1 directly through DAC1. Mute all ADC mix inputs.
        {RT5682_AD_DA_MIXER, 0x8080, 0},
        {RT5682_A_DAC1_MUX, 0x0311, 0},
    };
    NTSTATUS status;
    ULONG value;
#define UPDATE(reg, mask, val) do { status = CodecUpdate(device, reg, mask, val); if (!NT_SUCCESS(status)) goto fail; } while (0)
    status = CodecApply(device, fixed, RTL_NUMBER_OF(fixed));
    if (!NT_SUCCESS(status)) goto fail;
    UPDATE(RT5682_GLB_CLK, RT5682_SCLK_SRC_MASK | RT5682_PLL1_SRC_MASK,
        RT5682_SCLK_SRC_PLL1 | RT5682_PLL1_SRC_MCLK);
    UPDATE(RT5682_GPIO_CTRL_1, RT5682_GP4_PIN_MASK | RT5682_GP5_PIN_MASK,
        RT5682_GP4_PIN_ADCDAT1 | RT5682_GP5_PIN_DACDAT1);
    UPDATE(RT5682_I2S1_SDP, RT5682_I2S_DF_MASK | RT5682_I2S1_DL_MASK |
        RT5682_I2S1_MONO_MASK | RT5682_I2S1_TX_CHL_MASK | RT5682_I2S1_RX_CHL_MASK,
        RT5682_I2S1_DL_24 | RT5682_I2S1_TX_CHL_24 | RT5682_I2S1_RX_CHL_24);
    UPDATE(RT5682_TDM_ADDA_CTRL_2, RT5682_TDM_EN, 0);
    UPDATE(RT5682_TDM_CTRL, RT5682_TDM_TX_CH_MASK | RT5682_TDM_RX_CH_MASK, 0);
    UPDATE(RT5682_TDM_TCON_CTRL, RT5682_TDM_MS_MASK | RT5682_TDM_S_BP_MASK |
        RT5682_TDM_DF_MASK | RT5682_TDM_M_BP_MASK | RT5682_TDM_M_LP_MASK |
        RT5682_TDM_S_LP_MASK | RT5682_TDM_CL_MASK, RT5682_TDM_CL_24);
    UPDATE(RT5682_PLL_TRACK_2, RT5682_FILTER_CLK_DIV_MASK, 1 << RT5682_FILTER_CLK_DIV_SFT);
    UPDATE(RT5682_ADDA_CLK_1, RT5682_ADC_OSR_MASK | RT5682_DAC_OSR_MASK,
        (1 << RT5682_ADC_OSR_SFT) | (1 << RT5682_DAC_OSR_SFT));
    UPDATE(RT5682_PWR_ANLG_1, RT5682_LDO1_DVO_MASK | RT5682_HP_DRIVER_MASK |
        RT5682_PWR_BG | RT5682_PWR_VREF1 | RT5682_PWR_FV1,
        RT5682_LDO1_DVO_12 | RT5682_HP_DRIVER_5X | RT5682_PWR_BG | RT5682_PWR_VREF1);
    CodecDelay(20);
    UPDATE(RT5682_PWR_ANLG_1, RT5682_PWR_FV1, RT5682_PWR_FV1);
    UPDATE(RT5682_PWR_ANLG_3, RT5682_PWR_PLL | RT5682_PWR_LDO2,
        RT5682_PWR_PLL | RT5682_PWR_LDO2);
    UPDATE(RT5682_PWR_DIG_1, RT5682_PWR_I2S1 | RT5682_PWR_DAC_L1 | RT5682_PWR_DAC_R1 |
        RT5682_PWR_LDO | RT5682_DIG_GATE_CTRL,
        RT5682_PWR_I2S1 | RT5682_PWR_DAC_L1 | RT5682_PWR_DAC_R1 |
        RT5682_PWR_LDO | RT5682_DIG_GATE_CTRL);
    UPDATE(RT5682_PWR_DIG_2, RT5682_PWR_DAC_S1F, RT5682_PWR_DAC_S1F);
    UPDATE(RT5682_CHOP_DAC, 1 << RT5682_CKGEN_DAC1_SFT, 1 << RT5682_CKGEN_DAC1_SFT);
    UPDATE(RT5682_CLK_DET, RT5682_SYS_CLK_DET | (1 << RT5682_POW_CLK_DET_SFT),
        RT5682_SYS_CLK_DET | (1 << RT5682_POW_CLK_DET_SFT));
    // Keep headphone analog stages OFF during digital transport bring-up.
    // A subsequent, separately reviewed output sequence is required to listen.
    status = ReadRegister(device, RT5682_HP_CTRL_1, &value);
    if (!NT_SUCCESS(status)) goto fail;
    if ((value & 0x8080) != 0x8080) { status = STATUS_DEVICE_DATA_ERROR; goto fail; }
    status = ReadRegister(device, RT5682_HP_CTRL_2, &value);
    if (!NT_SUCCESS(status)) goto fail;
    if ((value & 0xe000) != 0x8000) { status = STATUS_DEVICE_DATA_ERROR; goto fail; }
    return STATUS_SUCCESS;
fail:
    (VOID)CodecResetMuted(device);
    return status;
#undef UPDATE
}
