#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fault injection for the production amp state machine and WDF IOCTL/timer bodies."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'driver/amp/sofamp.c').read_text()
def function(signature):
    a=source.index(signature); b=source.index('{',a); n=1; e=b+1
    while n:
        n+=(source[e]=='{')-(source[e]=='}'); e+=1
    return source[a:e]
code=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef int32_t NTSTATUS;
typedef uint8_t UCHAR;
typedef int BOOLEAN;
typedef void VOID;
typedef void *PVOID;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY (-1)
#define STATUS_INVALID_PARAMETER (-2)
#define STATUS_DEVICE_BUSY (-3)
#define STATUS_ACCESS_DENIED (-4)
#define STATUS_IO_TIMEOUT (-5)
#define STATUS_DEVICE_DATA_ERROR (-6)
#define NT_SUCCESS(s) ((s)>=0)
#define FILE_DEVICE_UNKNOWN 0x22
#define FILE_READ_ACCESS 1
#define FILE_WRITE_ACCESS 2
#define METHOD_BUFFERED 0
#define CTL_CODE(d,f,m,a) (((d)<<16)|((a)<<14)|((f)<<2)|(m))
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} GUID;
#include "amp-interface.h"
#include "amp-lifecycle.h"
static ULONGLONG clockMs;
static int writes, highWrites, lowWrites, failHigh, failLow, highDelay;
static NTSTATUS SofAmpDriveRaw(PVOID io, UCHAR value) {
    assert(io && value<=1); ++writes;
    if(value){++highWrites;clockMs+=highDelay;return failHigh ? STATUS_DEVICE_DATA_ERROR : 0;}
    ++lowWrites;return failLow ? STATUS_DEVICE_DATA_ERROR : 0;
}
static ULONGLONG SofAmpNowMs(VOID){return clockMs;}
static void setup(SOFAMP_STATE *s) {
    memset(s,0,sizeof(*s));clockMs=1000;
    writes=highWrites=lowWrites=failHigh=failLow=highDelay=0;
    assert(SofAmpStateOff(s,s)==0); s->Ready=TRUE;
}
// Compile the actual request authorization, timer and file-cleanup callbacks.
typedef struct {SOFAMP_STATE State;int Lock;void *Timer;} SOFAMP_CONTEXT;
typedef SOFAMP_CONTEXT *WDFDEVICE;
typedef void *WDFFILEOBJECT;
typedef SOFAMP_CONTEXT *WDFQUEUE;
typedef SOFAMP_CONTEXT *WDFTIMER;
typedef struct {int mode,completed;NTSTATUS status;size_t information;void *file;SOFAMP_STATUS out;} REQUEST,*WDFREQUEST;
typedef union {LONGLONG QuadPart;} LARGE_INTEGER;
#define KernelMode 0
#define SofAmpContext(d) (d)
#define WdfTimerGetParentObject(t) (t)
#define WdfFileObjectGetDevice(f) (&testDevice)
#define WdfIoQueueGetDevice(q) (q)
#define WdfRequestGetFileObject(r) ((r)->file)
#define WdfRequestGetRequestorMode(r) ((r)->mode)
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define WDF_REL_TIMEOUT_IN_MS(n) (n)
static SOFAMP_CONTEXT testDevice;
static int locked, timerArms, timerDelay;
static void WdfWaitLockAcquire(int l,void *p){(void)l;(void)p;assert(!locked);locked=1;}
static void WdfWaitLockRelease(int l){(void)l;assert(locked);locked=0;}
static int WdfTimerStart(void *t,ULONG delay){(void)t;assert(locked);++timerArms;timerDelay=delay;return 0;}
static void SofAmpReport(WDFDEVICE d,ULONG stage,NTSTATUS status){(void)d;(void)stage;(void)status;}
static void WdfRequestComplete(WDFREQUEST r,NTSTATUS s){assert(!r->completed);r->completed=1;r->status=s;}
static NTSTATUS WdfRequestRetrieveOutputBuffer(WDFREQUEST r,size_t n,PVOID *out,void *p){(void)p;assert(n==sizeof(r->out));*out=&r->out;return 0;}
static void WdfRequestSetInformation(WDFREQUEST r,size_t n){r->information=n;}
static int KeDelayExecutionThread(int mode,int alert,LARGE_INTEGER *delay){(void)mode;(void)alert;clockMs+=-delay->QuadPart/10000;return 0;}
'''
for signature in ('static VOID SofAmpArmTimer(', 'VOID SofAmpTimer(', 'VOID SofAmpFileCleanup(', 'VOID SofAmpControl('):
    code+=function(signature)+'\n'
code+=r'''
static REQUEST request(int mode,void *owner,ULONG command,size_t in,size_t out){
    REQUEST r={.mode=mode,.file=owner};
    SofAmpControl(&testDevice,&r,out,in,command);assert(r.completed && !locked);return r;
}
int main(void){
    SOFAMP_STATE s;int a=1,b=2;
    assert(sizeof(SOFAMP_STATUS)==48);
    setup(&s);s.Ready=FALSE;assert(SofAmpStateStart(&s,&s,&a)==STATUS_DEVICE_NOT_READY);assert(!highWrites);
    s.Ready=TRUE;assert(SofAmpStateStart(&s,&s,NULL)==STATUS_INVALID_PARAMETER);
    assert(SofAmpStateStart(&s,&s,&a)==0 && s.Active && s.OnWrites==1);
    assert(SofAmpStateStart(&s,&s,&a)==STATUS_DEVICE_BUSY && highWrites==1);
    assert(SofAmpStateRenew(&s,&s,&b)==STATUS_ACCESS_DENIED);
    SofAmpStateFileCleanup(&s,&s,&b);assert(s.Active);
    SofAmpStateFileCleanup(&s,&s,&a);assert(!s.Active && s.OutputRequest==SOFAMP_OUTPUT_OFF);
    assert(SofAmpStateStart(&s,&s,&b)==0);assert(SofAmpStateOff(&s,&s)==0);
    setup(&s);assert(SofAmpStateStart(&s,&s,&a)==0);
    clockMs=1099;assert(SofAmpStateRenew(&s,&s,&a)==0 && s.Deadline==1199);
    clockMs=1198;assert(SofAmpStateRenew(&s,&s,&a)==0 && s.Deadline==1250);
    clockMs=1249;assert(SofAmpStateRenew(&s,&s,&a)==0 && s.Deadline==1250);
    clockMs=1250;assert(SofAmpStateRenew(&s,&s,&a)==STATUS_IO_TIMEOUT);
    assert(!s.Active && s.Faulted && s.WatchdogTrips==1 && s.OutputRequest==SOFAMP_OUTPUT_OFF);
    assert(SofAmpStateStart(&s,&s,&a)==STATUS_DEVICE_NOT_READY);
    assert(SofAmpStateOff(&s,&s)==0 && s.Faulted); // Stop cannot clear a fault.
    for(int failedEnable=0;failedEnable<2;++failedEnable){
      setup(&s);failHigh=failedEnable;failLow=1;
      if(failedEnable){assert(SofAmpStateStart(&s,&s,&a)==STATUS_DEVICE_DATA_ERROR);}
      else {assert(SofAmpStateStart(&s,&s,&a)==0);clockMs=1100;assert(SofAmpStateExpired(&s,&s)==STATUS_IO_TIMEOUT);}
      assert(s.Faulted && !s.Active && !s.Owner && s.OutputRequest==SOFAMP_OUTPUT_UNKNOWN);
      assert(s.CleanupStatus==STATUS_DEVICE_DATA_ERROR && highWrites==1 && lowWrites==2);
      failLow=0;assert(SofAmpStateOff(&s,&s)==0);assert(s.OutputRequest==SOFAMP_OUTPUT_OFF && s.Faulted);
    }
    setup(&s);failHigh=1;assert(SofAmpStateStart(&s,&s,&a)==STATUS_DEVICE_DATA_ERROR);
    assert(s.Faulted && s.OutputRequest==SOFAMP_OUTPUT_OFF && s.CleanupStatus==0);
    setup(&s);highDelay=101;assert(SofAmpStateStart(&s,&s,&a)==STATUS_IO_TIMEOUT);
    assert(!s.Active && s.Faulted && lowWrites==2);
    setup(&testDevice.State);REQUEST r;
    r=request(1,&a,IOCTL_SOFAMP_START,0,48);assert(r.status==STATUS_ACCESS_DENIED && highWrites==0);
    r=request(1,&a,IOCTL_SOFAMP_RENEW,0,48);assert(r.status==STATUS_ACCESS_DENIED && highWrites==0);
    r=request(0,&a,IOCTL_SOFAMP_START,1,48);assert(r.status==STATUS_INVALID_PARAMETER && highWrites==0);
    r=request(0,&a,IOCTL_SOFAMP_START,0,47);assert(r.status==STATUS_INVALID_PARAMETER && highWrites==0);
    r=request(1,&a,IOCTL_SOFAMP_QUERY,0,48);assert(r.status==0 && r.out.OutputRequest==1 && r.information==48);
    r=request(0,&a,IOCTL_SOFAMP_START,0,48);assert(r.status==0 && r.out.Active && timerArms==1 && timerDelay==100);
    SofAmpFileCleanup(&b);assert(testDevice.State.Active); // Query close is harmless.
    clockMs+=50;r=request(0,&a,IOCTL_SOFAMP_RENEW,0,48);assert(r.status==0);
    clockMs+=50;SofAmpTimer(&testDevice);assert(testDevice.State.Active && timerDelay==50); // Stale timer callback.
    clockMs+=50;SofAmpTimer(&testDevice);assert(!testDevice.State.Active && testDevice.State.Faulted);
    r=request(1,&a,IOCTL_SOFAMP_QUERY,0,48);assert(r.status==0 && r.out.Faulted && r.out.WatchdogTrips==1);
    setup(&testDevice.State);r=request(0,&a,IOCTL_SOFAMP_START,0,48);assert(r.status==0);
    SofAmpFileCleanup(&a);assert(!testDevice.State.Active && testDevice.State.OutputRequest==1);
    setup(&testDevice.State);r=request(0,&a,IOCTL_SOFAMP_START,0,48);assert(r.status==0);
    r=request(1,&b,IOCTL_SOFAMP_STOP,0,48);assert(r.status==0 && r.out.OutputRequest==1 && !r.out.Active);

    // Listening is a separate kernel-only command, preserving ordinary START.
    setup(&testDevice.State);
    r=request(1,&a,IOCTL_SOFAMP_START_LISTEN,0,48);
    assert(r.status==STATUS_ACCESS_DENIED && highWrites==0);
    assert(SofAmpStateStartBounded(&testDevice.State,&testDevice,&a,2201)==STATUS_INVALID_PARAMETER);
    r=request(0,&a,IOCTL_SOFAMP_START_LISTEN,0,48);
    assert(r.status==0 && r.out.Version==2 && r.out.MaximumMs==2200);
    ULONGLONG begun=clockMs;
    assert(testDevice.State.MaximumDeadline==begun+2200);
    // No active START can reset ownership or the absolute deadline.
    r=request(0,&a,IOCTL_SOFAMP_START_LISTEN,0,48);assert(r.status==STATUS_DEVICE_BUSY);
    for(int step=1;step<=54;step++){
      clockMs=begun+40*step;
      r=request(0,&a,IOCTL_SOFAMP_RENEW,0,48);assert(r.status==0 && r.out.Active);
      assert(testDevice.State.Deadline<=begun+2200);
    }
    clockMs=begun+2200;SofAmpTimer(&testDevice);
    assert(!testDevice.State.Active && testDevice.State.Faulted && testDevice.State.WatchdogTrips==1);
    setup(&testDevice.State);
    r=request(0,&a,IOCTL_SOFAMP_START_LISTEN,0,48);assert(r.status==0);
    clockMs+=100;SofAmpTimer(&testDevice); // Missing heartbeat still shuts off at 100 ms.
    assert(!testDevice.State.Active && testDevice.State.Faulted);
    setup(&testDevice.State);
    r=request(0,&a,IOCTL_SOFAMP_START_LISTEN,0,48);assert(r.status==0);
    SofAmpFileCleanup(&a);assert(!testDevice.State.Active && testDevice.State.OutputRequest==1);
    for(unsigned mode=0;mode<2;mode++) {
    ULONG queryCode=mode ? IOCTL_SOFAMP_QUERY_PREVIEW_30 : IOCTL_SOFAMP_QUERY_PREVIEW;
    ULONG startCode=mode ? IOCTL_SOFAMP_START_PREVIEW_30 : IOCTL_SOFAMP_START_PREVIEW;
    ULONG cap=mode ? 30200 : 8200;
    // Extended preview is a separate command; legacy listening stays 2200 ms.
    setup(&testDevice.State);
    r=request(1,&a,queryCode,0,48);
    assert(r.status==0 && !r.out.Active && r.out.MaximumMs==cap && !highWrites);
    r=request(1,&a,startCode,0,48);
    assert(r.status==STATUS_ACCESS_DENIED && !highWrites);
    r=request(0,&a,startCode,0,48);
    assert(r.status==0 && r.out.Active && r.out.MaximumMs==cap);
    ULONGLONG previewBegun=clockMs;
    assert(testDevice.State.MaximumDeadline==previewBegun+cap);
    r=request(0,&b,IOCTL_SOFAMP_RENEW,0,48);assert(r.status==STATUS_ACCESS_DENIED);
    r=request(0,&a,startCode,0,48);assert(r.status==STATUS_DEVICE_BUSY);
    for(ULONG step=1;40*step<cap;step++) {
      clockMs=previewBegun+40*step;
      r=request(0,&a,IOCTL_SOFAMP_RENEW,0,48);assert(r.status==0 && r.out.Active);
      assert(testDevice.State.Deadline<=previewBegun+cap);
    }
    clockMs=previewBegun+cap;SofAmpTimer(&testDevice);
    assert(!testDevice.State.Active && testDevice.State.Faulted && testDevice.State.WatchdogTrips==1);
    setup(&testDevice.State);r=request(0,&a,startCode,0,48);assert(r.status==0);
    clockMs+=100;SofAmpTimer(&testDevice);
    assert(!testDevice.State.Active && testDevice.State.Faulted);
    setup(&testDevice.State);r=request(0,&a,startCode,0,48);assert(r.status==0);
    SofAmpFileCleanup(&a);assert(!testDevice.State.Active && testDevice.State.OutputRequest==1);
    // Legacy query reports the unchanged cap, including on a new amplifier.
    r=request(1,&a,IOCTL_SOFAMP_QUERY,0,48);assert(r.status==0 && r.out.MaximumMs==2200);
    }
    // Normal stream mode retains ownership/auth/watchdog but has no total cap.
    setup(&testDevice.State);
    r=request(1,&a,IOCTL_SOFAMP_QUERY_STREAM,0,48);
    assert(r.status==0 && r.out.MaximumMs==0 && !r.out.Active);
    r=request(1,&a,IOCTL_SOFAMP_START_STREAM,0,48);
    assert(r.status==STATUS_ACCESS_DENIED && !highWrites);
    r=request(0,&a,IOCTL_SOFAMP_START_STREAM,0,48);
    assert(r.status==0 && r.out.Active && r.out.MaximumMs==0);
    for(unsigned i=0;i<3000;i++) {
      clockMs+=40;r=request(0,&a,IOCTL_SOFAMP_RENEW,0,48);
      assert(r.status==0 && r.out.Active && highWrites==1);
    }
    r=request(0,&b,IOCTL_SOFAMP_RENEW,0,48);assert(r.status==STATUS_ACCESS_DENIED);
    r=request(0,&a,IOCTL_SOFAMP_START_STREAM,0,48);assert(r.status==STATUS_DEVICE_BUSY);
    clockMs+=100;SofAmpTimer(&testDevice);
    assert(!testDevice.State.Active && testDevice.State.Faulted && testDevice.State.WatchdogTrips==1);
    r=request(0,&a,IOCTL_SOFAMP_START_STREAM,0,48);assert(r.status==STATUS_DEVICE_NOT_READY);
    setup(&testDevice.State);r=request(0,&a,IOCTL_SOFAMP_START_STREAM,0,48);assert(r.status==0);
    SofAmpFileCleanup(&a);assert(!testDevice.State.Active && testDevice.State.OutputRequest==1);
    puts("PASS: amp ownership, user-mode enable rejection, leases, watchdog, stale timers, cleanup and GPIO failures");
}
'''
with tempfile.TemporaryDirectory(prefix='sofamp-lifecycle-') as d:
    c=Path(d)/'test.c';binary=Path(d)/'test';c.write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-I',str(root/'driver/amp'),str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
