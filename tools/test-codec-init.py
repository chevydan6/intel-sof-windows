#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the production muted initializer, including each I2C failure point."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "rt5682-registers.h"
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef int NTSTATUS, WDFDEVICE;
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_DATA_ERROR (-2)
#define STATUS_IO_TIMEOUT (-3)
#define NT_SUCCESS(s) ((s)>=0)
#define VOID void
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof((a)[0]))
static USHORT regs[65536];
static int operations, failAt, alwaysFail, busy, badMute, calReads, calStarts;
static unsigned delays;
static int transfer(void) { return ++operations==failAt || alwaysFail ? -99 : 0; }
static NTSTATUS WriteRegister(WDFDEVICE device, USHORT reg, USHORT value) {
    (void)device;
    int status=transfer();
    if (status<0) return status;
    // No operation may clear either output mute or enable audio DACs.
    if (reg==RT5682_HP_CTRL_1) assert(value==0x8080);
    if (reg==RT5682_HP_CTRL_2) assert(value==0x8000);
    if (reg==RT5682_PWR_DIG_1) assert((value&0x8c00)==0);
    if (reg==RT5682_HP_CALIB_CTRL_1 && value==0xfc00) {
        assert(regs[RT5682_HP_CTRL_1]==0x8080);
        assert(regs[RT5682_HP_CTRL_2]==0x8000);
        ++calStarts;
    }
    regs[reg]=value;
    return 0;
}
static NTSTATUS ReadRegister(WDFDEVICE device, USHORT reg, ULONG *value) {
    (void)device;
    int status=transfer();
    if (status<0) return status;
    *value=regs[reg];
    if (reg==RT5682_HP_CALIB_STA_1) { ++calReads; *value=busy ? 0x8000 : 0; }
    if (badMute && reg==RT5682_HP_CTRL_1) *value=0;
    return 0;
}
static void CodecDelay(ULONG ms) { assert(ms<=20); delays+=ms; }
#include "rt5682-init.h"
static void reset(void) {
    memset(regs,0,sizeof(regs));
    operations=failAt=alwaysFail=busy=badMute=calReads=calStarts=0; delays=0;
}
static void off(void) {
    assert(regs[RT5682_HP_CTRL_1]==0x8080 && regs[RT5682_HP_CTRL_2]==0x8000);
    assert(!regs[RT5682_PWR_DIG_1] && !regs[RT5682_PWR_DIG_2]);
    assert(!(regs[RT5682_PWR_ANLG_1]&3));
    assert(!regs[RT5682_PWR_ANLG_2] && !regs[RT5682_PWR_ANLG_3]);
}
int main(void) {
    reset(); assert(CodecInitializeMuted(0)==0); off();
    assert(calStarts==1 && calReads==1 && delays==30);
    int total=operations;
    for (int fault=1;fault<=total;++fault) {
        reset(); failAt=fault;
        assert(CodecInitializeMuted(0)==-99); off();
    }
    reset(); busy=1;
    assert(CodecInitializeMuted(0)==STATUS_IO_TIMEOUT); off();
    assert(calReads==60 && delays==640);
    reset(); badMute=1;
    assert(CodecInitializeMuted(0)==STATUS_DEVICE_DATA_ERROR); off();
    assert(calStarts==0);
    reset(); alwaysFail=1;
    assert(CodecInitializeMuted(0)==-99);
    assert(operations==24 && calStarts==0); // Both complete shutdown attempts.
    printf("PASS: muted calibration, %d injected transfer failures, bounded timeout, mute readback rejection, persistent I2C failure\n",total);
}
'''
with tempfile.TemporaryDirectory(prefix='sof-codec-init-') as directory:
    p = pathlib.Path(directory)
    (p / 'test.c').write_text(program)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', '-I', str(root / 'driver/codec'),
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
