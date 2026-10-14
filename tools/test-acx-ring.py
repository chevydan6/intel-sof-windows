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
prepare = source[source.index('NTSTATUS\nSofProbeAcxPrepareBuffer('):
                 source.index('// AcxBufferLock serializes')]
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
#define _Out_
#define FALSE 0
typedef void VOID;
typedef int32_t LONG;
typedef int64_t LONGLONG;
typedef uint32_t ULONG, *PULONG;
typedef uint64_t ULONGLONG;
typedef unsigned char UCHAR;
typedef int NTSTATUS;
#define KSSTREAM_HEADER_OPTIONSF_ENDOFSTREAM 0x200U
#define STATUS_INVALID_DEVICE_STATE -6
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
#define WdfWaitLockAcquire(x,y) ((void)0)
#define WdfWaitLockRelease(x) ((void)0)
#define READ_REGISTER_ULONG(p) (*(p))
#define READ_REGISTER_UCHAR(p) (*(p))
typedef struct {
    int AcxHardwareReady, AcxEosReceived, AcxPcmNeedsReset;
    UCHAR *AcxRing, *ToneHda;
    ULONG AcxLastPosition, AcxMaxAdvance, AcxLatePackets, AcxPacketBytes, AcxLastPacket;
    ULONGLONG AcxLinearPosition;
} CONTEXT, *PSOFPROBE_DEVICE_CONTEXT;
typedef CONTEXT *WDFDEVICE;
#define SofProbeGetContext(d) (d)
#ifdef SOFPLAY_HEADPHONE
#define WdfPowerDeviceD0 1
static NTSTATUS SofPlayStartPlaybackHardware(CONTEXT *c, int previous) {
    (void)c; (void)previous; return STATUS_DEVICE_NOT_READY;
}
#endif
static NTSTATUS SofPlayRearmPcmLocked(CONTEXT *c) {
    (void)c;
    assert(!"Hardware rearm is outside this offline test");
    return STATUS_DEVICE_NOT_READY;
}
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
#ifdef SOFPLAY_HEADPHONE
    int32_t scaled = 0x5a5a5a5a / 4096;
#else
    int32_t scaled = SofPlayAttenuateSpeakerSample(0x5a5a5a5a);
#endif
    UCHAR expected[4]; memcpy(expected,&scaled,4);
    assert(SofPlayAttenuateSpeakerSample(INT32_MIN)==-761921536);
    assert(SofPlayAttenuateSpeakerSample(INT32_MAX)==761921535);
    assert(SofPlayAttenuateSpeakerSample(0)==0);
    assert(SofPlayAttenuateSpeakerSample(32768)==11626);
    assert(SofPlayAttenuateSpeakerSample(-32768)==-11626);
    memset(packet, 0x5a, sizeof(packet));
    CONTEXT c = { .AcxHardwareReady=1, .AcxRing=ring, .ToneHda=registers };
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),0,0)==0);
    assert(SofProbeAcxWritePacket(&c,packet,1,sizeof(packet),0,0)==0);
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
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),0,0)==STATUS_DATA_LATE_ERROR);
    assert(c.AcxLatePackets==1); zero(ring,sizeof(ring));
    assert(SofProbeAcxWritePacket(&c,packet,100,sizeof(packet),0,0)==STATUS_DATA_OVERRUN);
    zero(ring,sizeof(ring));
    // Packet 25 crosses the physical end of the ring at byte 96,000.
    cursor(&c,46464); assert(SofProbeAcxAdvanceLocked(&c)==0);
    assert(SofProbeAcxWritePacket(&c,packet,25,sizeof(packet),0,0)==0);
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
    assert(SofProbeAcxWritePacket(&c,packet,26,sizeof(packet),0,0)==STATUS_DEVICE_NOT_READY);
    assert(SofProbeAcxWritePacket(&c,packet,26,3,0,0)==STATUS_INVALID_PARAMETER);

    // EOS tails must be silent even if both buffer tails contain old samples.
    // Exercise every stereo frame boundary, including zero/full length and
    // both sides of a wrapped packet.
    for (ULONG packetNumber = 0; packetNumber <= 25; packetNumber += 25) {
        for (ULONG valid = 0; valid <= sizeof(packet); valid += 8) {
            ULONGLONG absolute = (ULONGLONG)packetNumber * sizeof(packet);
            ULONG offset = absolute % sizeof(ring);
            memset(registers, 0, sizeof(registers));
            memset(ring, 0xa5, sizeof(ring));
            c = (CONTEXT){ .AcxHardwareReady=1, .AcxRing=ring,
                .ToneHda=registers, .AcxLastPosition=offset,
                .AcxLinearPosition=absolute };
            cursor(&c,offset);
            assert(SofProbeAcxWritePacket(&c,packet,packetNumber,sizeof(packet),
                KSSTREAM_HEADER_OPTIONSF_ENDOFSTREAM,valid)==STATUS_SUCCESS);
            for (ULONG i=0;i<sizeof(packet);++i)
                assert(ring[(offset+i)%sizeof(ring)] ==
                    (i < valid ? expected[i%4] : 0));
            // The preceding queued samples must not be overwritten.
            for (ULONG i=sizeof(packet);i<sizeof(ring);++i)
                assert(ring[(offset+i)%sizeof(ring)]==0xa5);
            assert(c.AcxEosReceived);
            UCHAR saved[sizeof(ring)]; memcpy(saved,ring,sizeof(ring));
            assert(SofProbeAcxWritePacket(&c,packet,packetNumber+1,
                sizeof(packet),0,0)==STATUS_INVALID_DEVICE_STATE);
            assert(memcmp(saved,ring,sizeof(ring))==0);
        }
    }
    memset(registers,0,sizeof(registers));
    memset(ring,0,sizeof(ring));
    c = (CONTEXT){ .AcxHardwareReady=1, .AcxRing=ring, .ToneHda=registers };
    // Reject malformed flags/length without publishing data or latching EOS.
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),1,0)==STATUS_INVALID_PARAMETER);
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),0x201,8)==STATUS_INVALID_PARAMETER);
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),0x200,3848)==STATUS_INVALID_PARAMETER);
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),0x200,7)==STATUS_INVALID_PARAMETER);
    assert(!c.AcxEosReceived); zero(ring,sizeof(ring));
    cursor(&c,8);
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),0x200,8)==STATUS_DATA_LATE_ERROR);
    assert(!c.AcxEosReceived); zero(ring,sizeof(ring));
    // EosPacketLength is ignored on ordinary packets, even if nonsensical.
    assert(SofProbeAcxWritePacket(&c,packet,1,sizeof(packet),0,UINT32_MAX)==STATUS_SUCCESS);
    for (ULONG i=3840;i<7680;++i) assert(ring[i]==expected[i%4]);
    assert(!c.AcxEosReceived);
    // A new successful preparation clears EOS and permits fresh preroll.
    c.AcxEosReceived = 1;
    ULONG bytes = 0;
    assert(SofProbeAcxPrepareBuffer(&c,&bytes)==STATUS_SUCCESS);
    assert(bytes==sizeof(ring) && !c.AcxEosReceived);
    cursor(&c,0);
    assert(SofProbeAcxWritePacket(&c,packet,0,sizeof(packet),0,0)==STATUS_SUCCESS);
    puts("PASS: attenuation, consumption, underrun silence, late/overrun rejection, wrap, EOS tails at every frame boundary, post-EOS rejection, malformed input");
}
'''
with tempfile.TemporaryDirectory(prefix='sofplay-ring-') as directory:
    path = pathlib.Path(directory)
    header = (root / 'driver/acx/sofplay-samples.h').read_text().replace('#pragma once', '')
    (path / 'test.c').write_text(shim + header + prepare + advance + copy + write + tests)
    for mode in ('speaker', 'headphone'):
        flags = ['-DSOFPLAY_HEADPHONE=1'] if mode == 'headphone' else []
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-g', *flags, str(path / 'test.c'),
                        '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
        print(f'Validated production ring in {mode} build')
