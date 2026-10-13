#!/usr/bin/env python3
"""Exercise the production ring routines with a mock DMA cursor on Linux.

This checks copying/clearing and late-packet behavior, not WDF synchronization,
real DMA timing, or audible quality. The routines are extracted unchanged.
"""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'driver/sofprobe.c').read_text()
advance = source[source.index('static NTSTATUS\nSofProbeAcxAdvanceLocked('):
                 source.index('NTSTATUS\nSofProbeAcxGetProgress(')]
write = source[source.index('NTSTATUS\nSofProbeAcxWritePacket('):
               source.index('NTSTATUS\nSofProbeAcxGetPosition(')]
copy = source[source.index('static VOID\nSofProbeAcxCopySpeakerSamples('):
              source.index('NTSTATUS\nSofProbeAcxWritePacket(')]
shim = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define _In_
#define _In_reads_bytes_(x)
#define _Out_writes_bytes_(x)
typedef void VOID;
typedef uint32_t ULONG, *PULONG;
typedef uint64_t ULONGLONG;
typedef unsigned char UCHAR;
typedef int NTSTATUS;
#define SOFPROBE_TONE_BYTES 49152U
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY -1
#define STATUS_DEVICE_DATA_ERROR -2
#define STATUS_INVALID_PARAMETER -3
#define STATUS_DATA_LATE_ERROR -4
#define STATUS_DATA_OVERRUN -5
#define NT_SUCCESS(x) ((x) >= 0)
#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(p,s,n) memcpy(p,s,n)
#define KeMemoryBarrier() ((void)0)
#define WdfSpinLockAcquire(x) ((void)0)
#define WdfSpinLockRelease(x) ((void)0)
#define READ_REGISTER_ULONG(p) (*(p))
#define READ_REGISTER_UCHAR(p) (*(p))
typedef struct {
    int AcxHardwareReady;
    UCHAR *AcxRing, *ToneHda;
    ULONG AcxLastPosition, AcxMaxAdvance, AcxLatePackets, AcxPacketBytes;
    ULONGLONG AcxLinearPosition;
} CONTEXT, *PSOFPROBE_DEVICE_CONTEXT;
typedef CONTEXT *WDFDEVICE;
#define SofProbeGetContext(d) (d)
'''
tests = r'''
static void cursor(CONTEXT *c, ULONG p) {
    memcpy(c->ToneHda + 0x164, &p, sizeof(p));
}
static void zero(const UCHAR *p, ULONG n) {
    for (ULONG i=0;i<n;++i) assert(p[i]==0);
}
int main(void) {
    _Alignas(8) UCHAR registers[0x200] = {0};
    UCHAR ring[SOFPROBE_TONE_BYTES] = {0};
    UCHAR packet[3840];
    int32_t scaled = SofPlayAttenuateSpeakerSample(0x5a5a5a5a);
    UCHAR expected[4]; memcpy(expected,&scaled,4);
    assert(SofPlayAttenuateSpeakerSample(INT32_MIN)==-761921536);
    assert(SofPlayAttenuateSpeakerSample(INT32_MAX)==761921535);
    assert(SofPlayAttenuateSpeakerSample(0)==0);
    assert(SofPlayAttenuateSpeakerSample(32768)==11626);
    assert(SofPlayAttenuateSpeakerSample(-32768)==-11626);
    memset(packet, 0x5a, sizeof(packet));
    CONTEXT c = { .AcxHardwareReady=1, .AcxRing=ring, .ToneHda=registers };
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet))==0);
    assert(SofProbeAcxWritePacket(&c,packet,1,sizeof(packet))==0);
    // Consume the two packets, including irregular position increments.
    for (ULONG p=384;p<=7680;p+=384) {
        cursor(&c,p);
        assert(SofProbeAcxAdvanceLocked(&c)==0);
        zero(ring,p);
        for (ULONG i=p;i<7680;++i) assert(ring[i]==expected[i%4]);
    }
    // An underrun must stay silent on the next complete DMA revolution.
    for (ULONG p=8064;p<SOFPROBE_TONE_BYTES;p+=384) {
        cursor(&c,p); assert(SofProbeAcxAdvanceLocked(&c)==0);
    }
    cursor(&c,0); assert(SofProbeAcxAdvanceLocked(&c)==0);
    zero(ring,sizeof(ring));
    assert(c.AcxLinearPosition==SOFPROBE_TONE_BYTES);
    // A late packet must not leave a delayed copy waiting in the ring.
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet))==STATUS_DATA_LATE_ERROR);
    assert(c.AcxLatePackets==1); zero(ring,sizeof(ring));
    assert(SofProbeAcxWritePacket(&c,packet,100,sizeof(packet))==STATUS_DATA_OVERRUN);
    zero(ring,sizeof(ring));
    // Packet 25 crosses the physical end of the ring at byte 96,000.
    cursor(&c,46464); assert(SofProbeAcxAdvanceLocked(&c)==0);
    assert(SofProbeAcxWritePacket(&c,packet,25,sizeof(packet))==0);
    for (ULONG i=46848;i<49152;++i) assert(ring[i]==expected[i%4]);
    for (ULONG i=0;i<1536;++i) assert(ring[i]==expected[i%4]);
    for (ULONG i=0;i<sizeof(packet);++i) assert(packet[i]==0x5a);
    cursor(&c,1536); assert(SofProbeAcxAdvanceLocked(&c)==0);
    zero(ring,sizeof(ring));
    assert(c.AcxLinearPosition==99840);
    cursor(&c,SOFPROBE_TONE_BYTES);
    assert(SofProbeAcxAdvanceLocked(&c)==STATUS_DEVICE_DATA_ERROR);
    assert(c.AcxLinearPosition==99840);
    cursor(&c,1536); registers[0x163]=0x10;
    assert(SofProbeAcxAdvanceLocked(&c)==STATUS_DEVICE_DATA_ERROR);
    c.AcxHardwareReady=0;
    assert(SofProbeAcxWritePacket(&c,packet,26,sizeof(packet))==STATUS_DEVICE_NOT_READY);
    assert(SofProbeAcxWritePacket(&c,packet,26,3)==STATUS_INVALID_PARAMETER);
    puts("PASS: sample attenuation/extremes, unchanged client data, consumption, underrun silence, late/overrun rejection, wrap, invalid input/status");
}
'''
with tempfile.TemporaryDirectory(prefix='sofplay-ring-') as directory:
    path = pathlib.Path(directory)
    header = (root / 'driver/acx/sofplay-samples.h').read_text().replace('#pragma once', '')
    (path / 'test.c').write_text(shim + header + advance + copy + write + tests)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
