#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the production PCM parameter guard in both build modes."""
from pathlib import Path
import subprocess, tempfile
s=Path('driver/sofprobe.c').read_text()
macros=s[s.index('#ifdef SOFPLAY_HEADPHONE\n#define SOFPROBE_PCM_FRAME_FORMAT'):].split('#endif',1)[0]+'\n#endif\n'
guard=s.split('    if (words[1] == 0x60010000) {',1)[1].split('    } else if',1)[0]
c=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef struct {uint32_t LowPart;} PHYSICAL_ADDRESS;
typedef struct {int TonePrepared,TonePcmConfigured;void *TonePageTable;} ContextType;
#define STATUS_INVALID_DEVICE_STATE -1
#define STATUS_INVALID_PARAMETER -2
#define SOFPROBE_TONE_PAGES 12
#define SOFPROBE_TONE_BYTES 49152
#define SOFPROBE_TONE_PERIOD_BYTES 12288
static PHYSICAL_ADDRESS WdfCommonBufferGetAlignedLogicalAddress(void *p) {(void)p;return (PHYSICAL_ADDRESS){0x1000};}
'''+macros+'\nstatic int validate(ContextType *Context,uint32_t *words,uint32_t size) {\n'+guard+r'''
return 0;
}
int main(void) {
 ContextType ctx={1,0,(void*)1};
 uint32_t p[27]={108,0x60010000};
 p[6]=84;p[8]=0x1000;p[9]=12;p[10]=49152;
 p[15]=SOFPROBE_PCM_FRAME_FORMAT;p[17]=48000;p[18]=0x00020001;
 p[19]=SOFPROBE_PCM_SAMPLE_BYTES;p[20]=12288;p[21]=1;
 assert(validate(&ctx,p,108)==0);
 int fields[]={2,6,7,8,9,10,14,15,17,18,19,20,21};
 for (unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
  uint32_t old=p[fields[i]];p[fields[i]]^=1;
  assert(validate(&ctx,p,108)==STATUS_INVALID_PARAMETER);p[fields[i]]=old;
 }
 assert(validate(&ctx,p,104)==STATUS_INVALID_DEVICE_STATE);
 ctx.TonePrepared=0;assert(validate(&ctx,p,108)==STATUS_INVALID_DEVICE_STATE);
 ctx.TonePrepared=1;ctx.TonePcmConfigured=1;assert(validate(&ctx,p,108)==STATUS_INVALID_DEVICE_STATE);
 ctx.TonePcmConfigured=0;ctx.TonePageTable=0;assert(validate(&ctx,p,108)==STATUS_INVALID_DEVICE_STATE);
 printf("PASS: PCM format %lu accepts intended stream and rejects mismatched buffers, formats and state\n",SOFPROBE_PCM_FRAME_FORMAT);
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(c)
 for flags in [[],['-DSOFPLAY_HEADPHONE=1']]:
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']+flags+[str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
