#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check microphone IPC against SOF v2.2 ABI and exercise DMA cleanup failures."""
from pathlib import Path
import re,struct,subprocess,tempfile
s=Path('driver/headphone/microphone.inc').read_text()
def words(name):
 m=re.search(r'const ULONG '+name+r'\[(\d*)\] = \{([^}]+)\}',s)
 assert m,name
 a=[int(x.strip(),0) for x in m[2].split(',') if x.strip()]
 if m[1]: a += [0]*(int(m[1])-len(a))
 assert a[0]==len(a)*4,(name,'size')
 return a
pipe,buf,host,dai,cfg=map(words,['pipeline','buffer','host','dai','config'])
assert pipe[4]==dai[2], 'Capture scheduler must be the capture DAI'
assert len({pipe[2],buf[2],host[2],dai[2]})==4
assert not {pipe[2],buf[2],host[2],dai[2]} & {0,2,4,5}
assert pipe[3]==buf[4]==host[4]==dai[4]==2
assert dai[16]==host[16]==1 and dai[12]==host[12]==2
assert dai[17]==cfg[3]==0 and dai[18]==cfg[2]==2
assert words('daiToBuffer')[2:]==[dai[2],buf[2]]
assert words('bufferToHost')[2:]==[buf[2],host[2]]
assert words('complete')[2]==pipe[2]
p=struct.pack('<54I',*cfg)[52:]
assert len(p)==164
assert struct.unpack_from('<6I',p)==(164,1,2400000,4800000,48000,0)
assert struct.unpack_from('<4H',p,24)==(32,0,40,60)
assert struct.unpack_from('<4I',p,32)==(1,0,0,200)
assert struct.unpack_from('<I10H',p,68)==(24,0,1,1,0,0,0,0,0,0,0)
assert p[92:]==bytes(72),'Unused PDM controllers disabled'
assert 'pcm[18] = 0x00020001' in s, 'SOF input channel = stream_tag - 1'
assert '0x00140002' in s and 'hda + 0x80' in s
fn=s.split('static NTSTATUS\nSofMicRestoreInput',1)[1].split('\nstatic VOID',1)[0]
c=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t *PUCHAR;
typedef uint32_t ULONG,*PULONG;
typedef uint16_t *PUSHORT;
typedef int NTSTATUS;
typedef void VOID;
typedef struct {int64_t QuadPart;} LARGE_INTEGER;
#define STATUS_SUCCESS 0
#define STATUS_IO_TIMEOUT -1
#define STATUS_DEVICE_DATA_ERROR -2
#define KernelMode 0
#define FALSE 0
static uint8_t mem[4096];
static int fault,delays,zero_bdl;
#define READ_REGISTER_ULONG(p) (*(p))
#define READ_REGISTER_USHORT(p) (*(p))
static void WRITE_REGISTER_UCHAR(PUCHAR p, uint8_t v) {
 if (p==mem+0x80 && fault==1) return;
 *p=v;
}
static void WRITE_REGISTER_ULONG(PULONG p,ULONG v) {
 if ((PUCHAR)p==mem+0x98) {
  assert((*(PULONG)(mem+0x80)&3)==0); // Never detach a live DMA buffer.
  zero_bdl++;
  if(fault==2)return;
 }
 *p=v;
}
#define WRITE_REGISTER_USHORT(p,v) (*(p)=(v))
#define KeMemoryBarrier() ((void)0)
static int KeDelayExecutionThread(int a,int b,LARGE_INTEGER *d){(void)a;(void)b;(void)d;delays++;return 0;}
static NTSTATUS SofMicRestoreInput'''+fn+r'''
int main(void) {
 for(fault=0;fault<3;fault++) {
  memset(mem,0,sizeof(mem));delays=zero_bdl=0;
  *(PULONG)(mem+0x80)=0x140002;
  *(PULONG)(mem+0x98)=0x1234000;
  *(PULONG)(mem+0x804)=0x40000081;
  int r=SofMicRestoreInput(mem);
  if(fault==1) {assert(r==STATUS_IO_TIMEOUT && delays==300 && zero_bdl==0);}
  else if(fault==2) assert(r==STATUS_DEVICE_DATA_ERROR);
  else assert(r==0 && *(PULONG)(mem+0x98)==0);
  if(fault!=1) assert(*(PULONG)(mem+0x804)==0x40000080); // Playback preserved.
 }
 puts("PASS: capture graph ABI, PDM0 stereo, host channel, DMA stop timeout and restoration failure");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(c)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
