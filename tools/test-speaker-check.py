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
typedef struct {uint8_t *ToneHda;BOOLEAN ToneDmaRunning;} CONTEXT,*PSOFPROBE_DEVICE_CONTEXT;
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
'''+function('static NTSTATUS\nSofSpeakerTick(')+'\n'+function('static NTSTATUS\nSofSpeakerStopOutput(')+r'''
int main(void){
 LONG pcm[6144*2];
 for(ULONG ch=0;ch<2;ch++){
  int64_t sum=0;
  SofSpeakerFillTestSamples(pcm,6144,ch);
  for(ULONG i=0;i<6144;i++){
   assert(pcm[2*i+1-ch]==0);assert(llabs(pcm[2*i+ch])<=8388608);
   assert(pcm[2*i+ch]==pcm[2*(i%48)+ch]);sum+=pcm[2*i+ch];
  }
  assert(sum==0);
 }
 _Alignas(8) uint8_t registers[0x200]={0};CONTEXT ctx={registers,1};
 *(PULONG)(registers+0x164)=384;
 for(ULONG mode=0;mode<=4;mode++){
  SOFSPEAKER_CHECK t={.Mode=mode,.Amp=&ctx,.InitialOnWrites=3};CAPTURE r={0};
  starts=stops=0;now=10000000;
  r.Bytes=48000*8-8;assert(SofSpeakerTick(&ctx,&t,&r)==0 && !starts);
  r.Bytes=48000*8;assert(SofSpeakerTick(&ctx,&t,&r)==0);
  if(mode>=3){
   assert(starts==1 && t.Pulsed && (r.Reserved&0x500)==0x500);
   now+=399999;assert(SofSpeakerTick(&ctx,&t,&r)==0 && stops==0);
   now++;assert(SofSpeakerTick(&ctx,&t,&r)==0 && stops==1 && (r.Reserved&0x200));
   now+=1000000;assert(SofSpeakerTick(&ctx,&t,&r)==0 && starts==1 && stops==1);
  }else assert(starts==0 && stops==0);
 }
 for(int fault=0;fault<3;fault++){
  SOFSPEAKER_CHECK t={.Mode=3,.Amp=&ctx,.Configured=1,.Triggered=1};CAPTURE r={.Bytes=48000*8};
  starts=stops=closed=ipcs=sequence=0;ctx.ToneDmaRunning=1;
  failStart=fault==1;failStop=fault==2;now=10000000;
  NTSTATUS start=SofSpeakerTick(&ctx,&t,&r);assert((start==0)==!failStart);
  NTSTATUS stop=SofSpeakerStopOutput(&ctx,&t,&r);
  assert((stop==0)==!failStop && stops==1 && closed==1 && ipcs==1);
  assert(offOrder<ipcOrder && !t.Amp && !ctx.ToneDmaRunning);
 }
 puts("PASS: speaker SSP1/S32 ABI, channel isolation, fixed low signal, dry mode, one pulse, and shutdown-before-IPC cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-check-') as d:
 p=Path(d);(p/'test.c').write_text(c)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    '-fno-omit-frame-pointer','-I',str(root/'driver/speaker'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
