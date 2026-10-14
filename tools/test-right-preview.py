#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run production conversion/timer code with injected DMA, clock and amp faults.

This exercises policy and shutdown ordering, not WDF scheduling or hardware.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'driver/speaker/right-preview.inc').read_text()
probe = (root / 'driver/sofprobe.c').read_text()

def function(text, name):
    start = text.index(name + '(')
    start = text.rfind('static ', 0, start)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

c = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int32_t LONG, NTSTATUS;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint32_t ULONG;
typedef unsigned char UCHAR;
typedef int BOOLEAN;
typedef void VOID;
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_DEVICE_STATE (-1)
#define STATUS_IO_TIMEOUT (-2)
#define STATUS_DEVICE_DATA_ERROR (-3)
#define STATUS_DATA_ERROR (-4)
#define NT_SUCCESS(x) ((x)>=0)
#define SOFPROBE_TONE_BYTES 49152
#define IOCTL_SOFAMP_START_LISTEN 1
#define IOCTL_SOFAMP_RENEW 2
#define IOCTL_SOFAMP_STOP 3
#define IOCTL_SOFAMP_START_PREVIEW 4
#define IOCTL_SOFAMP_QUERY_PREVIEW 5
#define IOCTL_SOFAMP_QUERY 6
#define IOCTL_SOFAMP_START_PREVIEW_30 7
#define IOCTL_SOFAMP_QUERY_PREVIEW_30 8
/* PREVIEW_AMP_MODES */
#define WDF_REL_TIMEOUT_IN_MS(x) (x)
#define RtlCopyMemory memcpy
#define RtlZeroMemory(p,n) memset(p,0,n)
#include "right-preview-policy.h"
#include "right-preview-status.h"
typedef struct Context {
    SOF_RIGHT_PREVIEW Preview;
    SOF_RIGHT_PREVIEW_DIAGNOSTICS PreviewDiagnostics;
    ULONG AcxObservedPosition, AcxObservedStatus, AcxBoundaryReads, AcxCompletionReads;
    SOF_RIGHT_FILTER PreviewFilter;
    int AcxPowerLock, AcxBufferLock;
    struct Context *PreviewTimer;
    void *PreviewAmp;
    UCHAR *AcxRing;
    ULONGLONG AcxLinearPosition;
    BOOLEAN ToneTriggerActive, ToneDmaRunning;
} Context, *PSOFPROBE_DEVICE_CONTEXT, *WDFTIMER;
static Context *current;
static ULONGLONG clockMs;
static int powerLock, spinLock, starts, renews, stops, timerStarts, timerStops;
static int ampOn, failCommand, badDma;
static ULONGLONG SofRightNow(void) { return clockMs; }
static Context *WdfTimerGetParentObject(WDFTIMER t) { return t; }
#define SofProbeGetContext(x) (x)
static void WdfWaitLockAcquire(int lock, void *p) {
    (void)lock; (void)p; assert(!powerLock); powerLock=1;
}
static void WdfWaitLockRelease(int lock) {
    (void)lock; assert(powerLock && !spinLock); powerLock=0;
}
static void WdfSpinLockAcquire(int lock) {
    (void)lock; assert(!spinLock); spinLock=1;
}
static void WdfSpinLockRelease(int lock) {
    (void)lock; assert(spinLock); spinLock=0;
}
static void WdfTimerStart(WDFTIMER t, int delay) {
    (void)t; assert(powerLock && delay==20); ++timerStarts;
}
static void WdfTimerStop(WDFTIMER t, int wait) {
    (void)t; assert(!wait && powerLock); ++timerStops;
}
static NTSTATUS SofProbeAcxAdvanceLocked(Context *c) {
    (void)c; assert(spinLock); return badDma ? STATUS_DATA_ERROR : 0;
}
static NTSTATUS SofRightAmp(Context *c, ULONG command) {
    (void)c; assert(powerLock && !spinLock);
    if (command==SOF_RIGHT_AMP_START) { assert(!ampOn); ampOn=1; ++starts; }
    if (command==IOCTL_SOFAMP_RENEW) { assert(ampOn); ++renews; }
    if (command==IOCTL_SOFAMP_STOP) { ampOn=0; ++stops; }
    return command==(ULONG)failCommand ? STATUS_IO_TIMEOUT : STATUS_SUCCESS;
}
'''
c += function(probe, 'SofRightPreviewCopy') + '\n'
c += function(source, 'SofRightPreviewStop') + '\n'
c += function(source, 'SofRightPreviewBegin') + '\n'
c += function(source, 'SofRightPreviewTick') + '\n'
c += r'''
static LONG ring[SOFPROBE_TONE_BYTES/4];
static Context reset(int powered) {
    memset(ring,0,sizeof(ring));
    clockMs=100; starts=renews=stops=timerStarts=timerStops=0;
    powerLock=spinLock=ampOn=failCommand=badDma=0;
    Context c={0}; c.Preview.Phase=1; c.Preview.Powered=powered;
    c.PreviewAmp=(void*)1; c.PreviewTimer=(WDFTIMER)1; c.AcxRing=(UCHAR*)ring;
    c.ToneTriggerActive=c.ToneDmaRunning=1;
    return c;
}
static void begin(Context *c) {
    current=c; powerLock=1; assert(SofRightPreviewBegin(c)==0); powerLock=0;
}
static void tick(Context *c, unsigned elapsed, unsigned advance) {
    clockMs+=elapsed; c->AcxLinearPosition+=advance;
    SofRightPreviewTick(c); assert(!powerLock && !spinLock);
}
int main(void) {
    Context c=reset(1); begin(&c);
    tick(&c,20,7680); assert(starts==1 && ampOn && c.Preview.Phase==3);
    tick(&c,20,7680); assert(renews==1);
    for(unsigned i=0;i<SOF_RIGHT_PREVIEW_MS/20-2;i++) tick(&c,20,7680);
    assert(c.Preview.Phase==4 && !ampOn && stops==1 && !c.Preview.Error);
    assert(sizeof(c.PreviewDiagnostics)==64 && c.PreviewDiagnostics.Version==2);
    assert(c.PreviewDiagnostics.Reason==SofRightDeadline && c.PreviewDiagnostics.ElapsedMs==SOF_RIGHT_PREVIEW_MS);
    int before=starts; tick(&c,20,7680); assert(starts==before);
    powerLock=1; assert(SofRightPreviewBegin(&c)==STATUS_INVALID_DEVICE_STATE); powerLock=0;
    c=reset(0); begin(&c); for(unsigned i=0;i<SOF_RIGHT_PREVIEW_MS/20;i++) tick(&c,20,7680);
    assert(!starts && !renews && !ampOn && c.Preview.Phase==4);
    // Late first callback must never enable the amp.
    c=reset(1); begin(&c); tick(&c,80,7680); assert(!starts && stops==1 && c.Preview.Error);
    assert(c.PreviewDiagnostics.Reason==SofRightLateTick && c.PreviewDiagnostics.GapMs==80);
    c=reset(1); begin(&c); tick(&c,SOF_RIGHT_PREVIEW_MS,7680); assert(!starts && stops==1);
    // A DMA stall, error, channel corruption, or excess amplitude shuts off.
    for(int fault=0;fault<6;fault++) {
        c=reset(1); begin(&c); tick(&c,20,7680); assert(ampOn);
        if(fault==1) badDma=1;
        if(fault==2) ring[0]=1;
        if(fault==3) ring[1]=SOF_RIGHT_PREVIEW_PEAK+1;
        if(fault==4) c.ToneTriggerActive=0;
        if(fault==5) ring[1]=-SOF_RIGHT_PREVIEW_PEAK-1;
        tick(&c,20,fault==0 ? 0 : 7680);
        assert(c.Preview.Phase==4 && c.Preview.Error && !ampOn && stops==1);
        ULONG reasons[]={SofRightNoProgress,SofRightAdvanceError,SofRightInvalidPcm,
                         SofRightInvalidPcm,SofRightInactive,SofRightInvalidPcm};
        assert(c.PreviewDiagnostics.Reason==reasons[fault]);
        assert(c.PreviewDiagnostics.PhaseAtStop==3 && c.PreviewDiagnostics.Ticks==2);
        SOF_RIGHT_PREVIEW_DIAGNOSTICS saved=c.PreviewDiagnostics;
        powerLock=1; SofRightPreviewStop(&c,STATUS_SUCCESS); powerLock=0;
        assert(!memcmp(&saved,&c.PreviewDiagnostics,sizeof(saved)));
    }
    c=reset(1); begin(&c); tick(&c,20,7680); tick(&c,80,7680);
    assert(c.Preview.Phase==4 && !ampOn && renews==0);
    for(int command=1;command<=2;command++) {
        c=reset(1); begin(&c); failCommand=command==1 ? SOF_RIGHT_AMP_START : IOCTL_SOFAMP_RENEW;
        tick(&c,20,7680); if(command==2) tick(&c,20,7680);
        assert(c.Preview.Phase==4 && !ampOn && stops==1 && c.Preview.Error);
        assert(c.PreviewDiagnostics.Reason==(command==1 ? SofRightAmpStartError : SofRightAmpRenewError));
    }
    c=reset(1); begin(&c); tick(&c,20,7680);
    powerLock=1; assert(SofRightPreviewStop(&c,STATUS_SUCCESS)==0); powerLock=0;
    tick(&c,20,7680); assert(!ampOn && starts==1 && c.Preview.Phase==4);
    assert(c.PreviewDiagnostics.Reason==SofRightPause);
    // Sample conversion must preserve channel isolation and bounded arithmetic.
    Context a={0}, b={0}; LONG input[4096], whole[4096], split[4096];
    uint32_t random=1;
    for(unsigned i=0;i<4096;i++) { random=random*1664525U+1013904223U; input[i]=(LONG)random; }
    SofRightPreviewCopy(&a,(UCHAR*)whole,(UCHAR*)input,sizeof(input));
    for(unsigned i=0;i<4096;i+=2)
        SofRightPreviewCopy(&b,(UCHAR*)(split+i),(UCHAR*)(input+i),8);
    assert(!memcmp(whole,split,sizeof(whole)));
    for(unsigned i=0;i<4096;i+=2) {
        assert(whole[i]==0 && whole[i+1]<=SOF_RIGHT_PREVIEW_PEAK && whole[i+1]>=-SOF_RIGHT_PREVIEW_PEAK);
    }
    SOF_RIGHT_FILTER f={0}; LONG v=0;
    for(unsigned i=0;i<48000;i++) v=SofRightPreviewSample(&f,INT32_MAX,INT32_MAX);
    assert(v==0); // Constant input must decay to silence.
    for(unsigned i=0;i<100000;i++) {
        v=SofRightPreviewSample(&f,i&1 ? INT32_MIN : INT32_MAX,INT32_MIN);
        assert(v<=SOF_RIGHT_PREVIEW_PEAK && v>=-SOF_RIGHT_PREVIEW_PEAK);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='sof-right-preview-') as td:
    p = Path(td)
    modes=source[source.index('#if defined(SOFPLAY_RIGHT_PREVIEW_30)'):source.index('#endif')+6]
    (p/'test.c').write_text(c.replace('/* PREVIEW_AMP_MODES */',modes))
    for flags in ([], ['-DSOFPLAY_RIGHT_PREVIEW_LONG=1'], ['-DSOFPLAY_RIGHT_PREVIEW_30=1']):
      subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',*flags,
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I',str(root/'driver/speaker'),str(p/'test.c'),'-o',str(p/'test')],check=True)
      subprocess.run([str(p/'test')],check=True)
print('PASS: right-only PCM, DC rejection, peak ceiling, bounded streaming, stalled DMA, late callbacks, failed amp I/O, and cleanup')
