#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Keep packet MDLs alive until ACX's packet-free callback, in either order."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'driver/acx/sofplay.c').read_text()
def extract(name):
    start=s.index('VOID\n'+name+'('); end=s.index('{',start)+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[start:end]
c=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define _In_
#define TRUE 1
typedef void VOID;
typedef void *PVOID;
typedef unsigned ULONG;
typedef struct Packet { int mapped; } ACX_RTPACKET, *PACX_RTPACKET;
typedef struct Context {
    void *Timer, *Device;
    PACX_RTPACKET Packets;
    ULONG PacketCount, PacketSize;
} Context, *PSOFPLAY_STREAM_CONTEXT, *ACXSTREAM, *WDFOBJECT;
#define SofPlayGetStreamContext(x) (x)
static unsigned frees, releases;
static void WdfTimerStop(void *t,int wait) {(void)t;assert(wait);}
static void SofProbeAcxSetTimer(void *d,void *t) {(void)d;assert(!t);}
static void SofProbeAcxReleaseStream(void *d) {(void)d;++releases;}
static __inline void *InterlockedExchangePointer(void *volatile *p,void *v) {
    void *r=*p;*p=v;return r;
}
static void *InterlockedCompareExchangePointer(void *volatile *p,void *v,void *old) {
    void *r=*p;if(r==old)*p=v;return r;
}
static void SofPlayReleasePacketMemory(PACX_RTPACKET p,ULONG count) {
    for(unsigned i=0;i<count;i++) assert(!p[i].mapped);
    ++frees;free(p);
}
'''+extract('SofPlayEvtStreamCleanup')+'\n'+extract('SofPlayEvtFreePackets')+r'''
int main(void) {
 for(int cleanupFirst=0;cleanupFirst<2;cleanupFirst++) {
    Context c={0}; c.Device=&c;c.Timer=&c;c.PacketCount=2;c.PacketSize=3840;
    PACX_RTPACKET p=calloc(2,sizeof(*p));c.Packets=p;
    p[0].mapped=p[1].mapped=1;
    unsigned before=frees;
    if(cleanupFirst) {
        SofPlayEvtStreamCleanup(&c);
        assert(frees==before && c.Packets==p);
        // ACX still needs its MDLs while unmapping the client view.
        assert(p[0].mapped && p[1].mapped);
    }
    p[0].mapped=p[1].mapped=0;
    SofPlayEvtFreePackets(&c,p,2);
    assert(frees==before+1 && !c.Packets && !c.PacketCount && !c.PacketSize);
    if(!cleanupFirst) SofPlayEvtStreamCleanup(&c);
    assert(frees==before+1);
 }
 assert(releases==2);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='acx-lifetime-') as td:
    p=Path(td);(p/'test.c').write_text(c)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: ACX retains mapped packet memory through cleanup; packet-free callback releases it once')
