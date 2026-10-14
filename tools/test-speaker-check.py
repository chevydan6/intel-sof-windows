#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check the isolated speaker graph and execute fixed samples/pulse/cleanup code."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'driver/speaker/speaker-check.inc').read_text()
def words(name):
 m=re.search(r'const ULONG '+name+r'\[\] = \{([^}]+)\}',s);assert m,name
 a=[int(v.strip(),0) for v in m[1].split(',')]
 assert a[0]==len(a)*4
 return a
pipe,buf,host,dai,cfg=map(words,['pipeline','buffer','host','dai','config'])
assert {pipe[2],buf[2],host[2],dai[2]}=={20,22,24,25}
assert pipe[4]==host[2] and pipe[3]==buf[4]==host[4]==dai[4]==3
assert host[12]==dai[12]==2 and host[16]==dai[16]==0
assert dai[17]==cfg[3]==1 and dai[18]==cfg[2]==1
assert cfg[4]==0x4001 and cfg[15:23]==[19200000,48000,3072000,2,3,3,32,32]
assert cfg[17]==cfg[16]*cfg[18]*cfg[22]
assert words('connect1')[2:]==[20,22] and words('connect2')[2:]==[22,24]
def function(signature):
 a=s.index(signature);b=s.index('{',a);e=b+1;depth=1
 while depth:
  depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
struct=s[s.index('typedef struct {'):s.index('} SOFSPEAKER_CHECK;')+len('} SOFSPEAKER_CHECK;')]
header=(root/'driver/sofprobe-ioctl.h').read_text()
snapshot=re.search(r'typedef struct _SOFSPEAKER_SNAPSHOT \{.*?\} SOFSPEAKER_SNAPSHOT;',header,re.S)[0]
c=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef int32_t LONG;typedef uint64_t ULONGLONG;
typedef int NTSTATUS,BOOLEAN;typedef void VOID;typedef void *WDFIOTARGET;
typedef uint32_t *PULONG;typedef struct {int64_t QuadPart;} LARGE_INTEGER;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_DATA_ERROR -1
#define STATUS_IO_TIMEOUT -2
#define NT_SUCCESS(s) ((s)>=0)
#define KernelMode 0
#define SOFPROBE_TONE_BYTES 49152
#define IOCTL_SOFAMP_START 1
#define IOCTL_SOFAMP_STOP 2
#define READ_REGISTER_UCHAR(p) (*(p))
#define READ_REGISTER_ULONG(p) (*(p))
#define WRITE_REGISTER_UCHAR(p,v) (*(p)=(v))
#define KeMemoryBarrier() ((void)0)
#define RtlCompareMemory(a,b,n) (memcmp(a,b,n)==0 ? (n) : 0)
'''+snapshot+r'''
typedef struct {uint8_t *ToneHda;BOOLEAN ToneDmaRunning;uint8_t *SessionDsp;SOFSPEAKER_SNAPSHOT SpeakerSnapshot;} CONTEXT,*PSOFPROBE_DEVICE_CONTEXT;
typedef struct {ULONG Bytes,Reserved,Command;} CAPTURE,*PSOFMIC_CAPTURE_RESULT;
typedef struct {ULONG OnWrites;} SOFAMP_STATUS;
#include "speaker-test-samples.h"
'''+struct+r'''
static ULONGLONG now;
static int starts,stops,closed,ipcs,failStart,failStop,sequence,offOrder,ipcOrder;
static ULONGLONG KeQueryInterruptTime(void){return now;}
static NTSTATUS SofSpeakerAmpCommand(SOFSPEAKER_CHECK *t,ULONG code,SOFAMP_STATUS *out){
 memset(out,0,sizeof(*out));
 if(code==IOCTL_SOFAMP_START){starts++;out->OnWrites=t->InitialOnWrites+1;return failStart ? -1 : 0;}
 stops++;offOrder=++sequence;out->OnWrites=t->InitialOnWrites+(t->Pulsed?1:0);return failStop ? -1 : 0;
}
static NTSTATUS SofMicIpc(CONTEXT *c,const ULONG *words,CAPTURE *r){(void)c;(void)r;assert(words[2]==20);ipcs++;ipcOrder=++sequence;return 0;}
static void WdfIoTargetClose(void *p){assert(p);closed++;}
static void WdfObjectDelete(void *p){assert(p);}
static int KeDelayExecutionThread(int a,int b,LARGE_INTEGER *d){(void)a;(void)b;(void)d;return 0;}
'''+function('static BOOLEAN\nSofSpeakerReadPosition(')+'\n'+function('static VOID\nSofSpeakerSnapshot(')+'\n'+function('static NTSTATUS\nSofSpeakerTick(')+'\n'+function('static NTSTATUS\nSofSpeakerStopOutput(')+r'''
int main(void){
 LONG pcm[6144*2];
 for(ULONG ch=0;ch<2;ch++){
  int64_t sum=0;
  SofSpeakerFillTestSamples(pcm,6144,ch);
  for(ULONG i=0;i<6144;i++){
   assert(pcm[2*i+1-ch]==0);assert(llabs(pcm[2*i+ch])<=377487360);
   assert(pcm[2*i+ch]==pcm[2*(i%48)+ch]);sum+=pcm[2*i+ch];
  }
  assert(sum==0);
  assert(pcm[24+ch]==377487360 && pcm[72+ch]==-377487360);
  assert(SofSpeakerTestSamplesValid(pcm,6144,ch));
  pcm[6143*2+1-ch]=1;
  assert(!SofSpeakerTestSamplesValid(pcm,6144,ch));
  SofSpeakerFillTestSamples(pcm,6144,ch);pcm[22]=0x7fffffff;
  assert(!SofSpeakerTestSamplesValid(pcm,6144,ch));
 }
 _Alignas(8) uint8_t registers[0x200]={0};
 _Alignas(8) static uint8_t dsp[0xC2000]={0};
 CONTEXT ctx={.ToneHda=registers,.ToneDmaRunning=1,.SessionDsp=dsp};
 ULONG offsets[8]={0x00,0x04,0x08,0x2c,0x30,0x40,0x44,0x48};
 for(ULONG i=0;i<8;i++) *(PULONG)(dsp+0x11000+offsets[i])=0x100+i;
 *(PULONG)(registers+0x164)=384;
 for(ULONG mode=0;mode<=4;mode++){
  SofSpeakerFillTestSamples(pcm,6144,(mode-1)&1);
  SOFSPEAKER_CHECK t={.Mode=mode,.Amp=&ctx,.InitialOnWrites=3,.Samples=pcm};CAPTURE r={0};
  starts=stops=0;now=10000000;
  r.Bytes=48000*8-8;assert(SofSpeakerTick(&ctx,&t,&r)==0 && !starts);
  r.Bytes=48000*8;assert(SofSpeakerTick(&ctx,&t,&r)==0);
  if(mode>=3){
   assert(starts==1 && t.Pulsed && (r.Reserved&0x500)==0x500);
   now+=399999;assert(SofSpeakerTick(&ctx,&t,&r)==0 && stops==0);
   now++;assert(SofSpeakerTick(&ctx,&t,&r)==0 && stops==1 && (r.Reserved&0x200));
   now+=1000000;assert(SofSpeakerTick(&ctx,&t,&r)==0 && starts==1 && stops==1);
  }else assert(starts==0 && stops==0);
  if(mode){
   assert(ctx.SpeakerSnapshot.Version==2 && ctx.SpeakerSnapshot.Mode==mode);
   assert(ctx.SpeakerSnapshot.HostPosition==384 && ctx.SpeakerSnapshot.CapturedBytes==48000*8);
   ULONG values[8];memcpy(values,&ctx.SpeakerSnapshot.Sscr0,sizeof(values));
   for(ULONG i=0;i<8;i++){
    assert(values[i]==0x100+i);
    assert(*(PULONG)(dsp+0x11000+offsets[i])==0x100+i);
   }
  }
 }
 for(int fault=0;fault<3;fault++){
  SofSpeakerFillTestSamples(pcm,6144,0);
  SOFSPEAKER_CHECK t={.Mode=3,.Amp=&ctx,.Configured=1,.Triggered=1,.Samples=pcm};CAPTURE r={.Bytes=48000*8};
  starts=stops=closed=ipcs=sequence=0;ctx.ToneDmaRunning=1;
  failStart=fault==1;failStop=fault==2;now=10000000;
  NTSTATUS start=SofSpeakerTick(&ctx,&t,&r);assert((start==0)==!failStart);
  NTSTATUS stop=SofSpeakerStopOutput(&ctx,&t,&r);
  assert((stop==0)==!failStop && stops==1 && closed==1 && ipcs==1);
  assert(offOrder<ipcOrder && !t.Amp && !ctx.ToneDmaRunning);
 }
 // A corrupted/cleared ring must never reach amplifier enable.
 failStart=failStop=0;starts=0;memset(pcm,0,sizeof(pcm));
 SOFSPEAKER_CHECK bad={.Mode=3,.Amp=&ctx,.Samples=pcm};CAPTURE badResult={.Bytes=48000*8};
 assert(SofSpeakerTick(&ctx,&bad,&badResult)==STATUS_DEVICE_DATA_ERROR && starts==0);
 // Firmware position records are bounded, identified, and copied without writes.
 SOFSPEAKER_CHECK positionTest={.PositionReady=1,.PositionOffset=16};
 ULONG *record=(PULONG)(dsp+0xC1010),copy[19];
 for(ULONG i=0;i<19;i++)record[i]=i;
 record[0]=76;record[1]=0x600A0014;record[2]=0;record[3]=20;
 assert(SofSpeakerReadPosition(&ctx,&positionTest,copy));
 assert(memcmp(copy,record,sizeof(copy))==0);
 record[3]=10;assert(!SofSpeakerReadPosition(&ctx,&positionTest,copy));record[3]=20;
 positionTest.PositionOffset=4096-72;assert(!SofSpeakerReadPosition(&ctx,&positionTest,copy));
 positionTest.PositionOffset=3;assert(!SofSpeakerReadPosition(&ctx,&positionTest,copy));
 positionTest.PositionOffset=16;positionTest.PositionReady=0;
 assert(!SofSpeakerReadPosition(&ctx,&positionTest,copy));
 positionTest.PositionReady=1;positionTest.Mode=1;positionTest.Samples=pcm;
 SofSpeakerFillTestSamples(pcm,6144,0);memset(&ctx.SpeakerSnapshot,0,sizeof(ctx.SpeakerSnapshot));
 CAPTURE progress={.Bytes=48000*8};starts=stops=0;
 assert(SofSpeakerTick(&ctx,&positionTest,&progress)==0);
 assert(ctx.SpeakerSnapshot.PositionReadMask==1 && ctx.SpeakerSnapshot.PositionFirst[9]==9);
 record[9]=123456;progress.Bytes=4*48000*8;
 assert(SofSpeakerTick(&ctx,&positionTest,&progress)==0);
 assert(ctx.SpeakerSnapshot.PositionReadMask==3 && ctx.SpeakerSnapshot.PositionSecond[9]==123456);
 assert(!starts && !stops);
 _Static_assert(sizeof(SOFSPEAKER_SNAPSHOT)==216,"snapshot ABI");
 puts("PASS: speaker SSP1/S32 ABI, channel isolation, fixed low signal, dry mode, one pulse, and shutdown-before-IPC cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-check-') as d:
 p=Path(d);(p/'test.c').write_text(c)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    '-fno-omit-frame-pointer','-I',str(root/'driver/speaker'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
