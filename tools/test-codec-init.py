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
#define STATUS_INVALID_PARAMETER (-5)
#define TRUE 1
#define FALSE 0
#define STATUS_DEVICE_DATA_ERROR (-2)
#define STATUS_IO_TIMEOUT (-3)
#define STATUS_DEVICE_NOT_READY (-4)
#define NT_SUCCESS(s) ((s)>=0)
#define VOID void
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof((a)[0]))
static USHORT regs[65536];
static int operations, failAt, alwaysFail, busy, badMute, calReads, calStarts, preparing, audition, unplugged, typeTimeout, unmuted, badGain;
static unsigned delays;
static int transfer(void) { return ++operations==failAt || alwaysFail ? -99 : 0; }
static NTSTATUS WriteRegister(WDFDEVICE device, USHORT reg, USHORT value) {
    (void)device;
    int status=transfer();
    if (status<0) return status;
    // No operation may clear either output mute or enable audio DACs.
    if (reg==RT5682_HP_CTRL_1 && !audition) assert(value==0x8080);
    if (reg==RT5682_HP_CTRL_2 && !audition) assert(value==0x8000);
    if (reg==RT5682_HP_CTRL_2 && !(value&0x8000)) {
        assert(audition && (regs[RT5682_DAC1_DIG_VOL]==0x4646 || regs[RT5682_DAC1_DIG_VOL]==0xaeae));
        assert((regs[RT5682_PWR_ANLG_1]&3)==3);
        assert((regs[RT5682_DEPOP_1]&9)==9);
        ++unmuted;
    }
    if (reg==RT5682_PWR_DIG_1 && !preparing) assert((value&0x8c00)==0);
    if (reg==RT5682_PWR_ANLG_1 && preparing && !audition) assert(!(value&3));
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
    if (audition && reg==RT5682_CBJ_CTRL_2) *value=(*value&~3u)|(typeTimeout?0:3);
    if (audition && reg==RT5682_AJD1_CTRL) *value=unplugged?RT5682_JDH_RS_MASK:0;
    if (audition && badGain && reg==RT5682_DAC1_DIG_VOL) *value=0xffff;
    return 0;
}
static void CodecDelay(ULONG ms) { assert(ms<=250); delays+=ms; }
#include "rt5682-init.h"
#include "rt5682-playback.h"
#include "rt5682-audition.h"
typedef struct {
    int Initialized,Prepared,StreamRequested,Playing,JackPresent;
    void *StreamOwner,*JackTimer;
    ULONG AuditionStage;
} CODEC_CONTEXT;
static CODEC_CONTEXT context;
static CODEC_CONTEXT *CodecContext(WDFDEVICE d) {(void)d;return &context;}
static void WdfTimerStop(void *t,int wait) {(void)t;(void)wait;}
#include "rt5682-stream.h"
static void reset(void) {
    memset(regs,0,sizeof(regs));
    memset(&context,0,sizeof(context));
    operations=failAt=alwaysFail=busy=badMute=calReads=calStarts=preparing=audition=unplugged=typeTimeout=unmuted=badGain=0; delays=0;
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
    reset(); assert(CodecInitializeMuted(0)==0); preparing=1; operations=0;
    assert(CodecPrepareHeadphoneMuted(0)==0);
    assert(regs[RT5682_HP_CTRL_1]==0x8080 && regs[RT5682_HP_CTRL_2]==0x8000);
    assert(regs[RT5682_DAC1_DIG_VOL]==0x4646 && !(regs[RT5682_PWR_ANLG_1]&3));
    int prepareOperations=operations;
    for (int fault=1;fault<=prepareOperations;++fault) {
        reset(); assert(CodecInitializeMuted(0)==0); preparing=1; operations=0; failAt=fault;
        assert(CodecPrepareHeadphoneMuted(0)==-99); off();
    }
    ULONG stage;
    reset(); assert(CodecInitializeMuted(0)==0); preparing=1; assert(CodecPrepareHeadphoneMuted(0)==0);
    audition=1; operations=0; delays=0;
    assert(CodecAudition(0,&stage)==0); off(); assert(stage==7 && unmuted==1 && delays>=3000 && delays<4000);
    int auditionOperations=operations;
    for (int fault=1;fault<=auditionOperations;++fault) {
        reset(); assert(CodecInitializeMuted(0)==0); preparing=1; assert(CodecPrepareHeadphoneMuted(0)==0);
        audition=1;operations=0;failAt=fault;
        assert(CodecAudition(0,&stage)==-99);
        // Production IOCTL adds a second complete shutdown attempt on error.
        assert(CodecResetMuted(0)==0);off(); assert(stage!=7);
    }
    for(int condition=0;condition<3;condition++) {
        reset(); assert(CodecInitializeMuted(0)==0); preparing=1; assert(CodecPrepareHeadphoneMuted(0)==0);
        audition=1; unplugged=condition==0; typeTimeout=condition==1; badGain=condition==2;
        assert(CodecAudition(0,&stage)<0);off();assert(unmuted==0);
    }
    // Stream lifecycle uses the same production analog sequence without the
    // diagnostic time limit, and must fail closed at every failed transfer.
    reset(); preparing=audition=1;context.StreamRequested=1;
    assert(CodecStreamOn(0)==0 && context.Playing && context.Prepared);
    assert(regs[RT5682_DAC1_DIG_VOL]==0xaeae);
    int streamOperations=operations;
    assert(CodecPollJack(0)==0 && context.Playing);
    unplugged=1;assert(CodecPollJack(0)==0 && !context.Playing && context.StreamRequested);
    assert(regs[RT5682_HP_CTRL_1]==0x8080 && !(regs[RT5682_PWR_ANLG_1]&3));
    unplugged=0;assert(CodecPollJack(0)==0 && context.Playing);
    assert(CodecStreamOff(0)==0 && !context.StreamRequested);off();
    context.StreamRequested=1;assert(CodecStreamOn(0)==0 && context.Playing);
    failAt=operations+1;assert(CodecPollJack(0)==-99 && !context.StreamRequested);off();
    for(int fault=1;fault<=streamOperations;fault++) {
        reset();preparing=audition=1;context.StreamRequested=1;failAt=fault;
        assert(CodecStreamOn(0)==-99 && !context.StreamRequested && !context.Playing);off();
    }
    reset();preparing=audition=unplugged=1;context.StreamRequested=1;
    assert(CodecStreamOn(0)==0 && context.StreamRequested && !context.Playing);
    unplugged=0;assert(CodecPollJack(0)==0 && context.Playing);
    assert(CodecStreamOff(0)==0);off();
    printf("PASS: normal stream start/stop/restart, unplug/replug, missing jack, poll failure and %d injected start faults\n",streamOperations);
    printf("PASS: bounded headphone audition, %d injected faults, jack/gain checks, automatic shutdown\n",auditionOperations);
    printf("PASS: muted calibration, %d injected transfer failures, bounded timeout, mute readback rejection, persistent I2C failure, muted clock/DAC preparation\n",total);
}
'''
with tempfile.TemporaryDirectory(prefix='sof-codec-init-') as directory:
    p = pathlib.Path(directory)
    (p / 'test.c').write_text(program)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', '-I', str(root / 'driver/codec'),
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
