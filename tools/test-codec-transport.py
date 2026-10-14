#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Test production RT5682 transfer construction and resource gate without I/O."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'driver/codec/sofrt5682.c').read_text()
functions = source[source.index('static NTSTATUS ReadRegister('):
                   source.index('static VOID CodecDelay(')]
program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "codec-policy.h"
typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG, *PULONG;
typedef uintptr_t ULONG_PTR;
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY -1
#define STATUS_DEVICE_DATA_ERROR -2
#define NT_SUCCESS(s) ((s)>=0)
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef struct { void *I2c; ULONG Reads; } CODEC_CONTEXT, *WDFDEVICE;
#define CodecContext(d) (d)
typedef struct { int Direction; unsigned Delay; void *Buffer; unsigned Length; } ENTRY;
typedef struct { unsigned Count; ENTRY Transfers[2]; } LIST;
#define SPB_TRANSFER_LIST_AND_ENTRIES(n) struct { LIST List; }
#define SPB_TRANSFER_LIST_INIT(p,n) ((p)->Count=(n))
#define SpbTransferDirectionToDevice 1
#define SpbTransferDirectionFromDevice 2
#define SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(d,t,b,n) ((ENTRY){d,t,b,n})
typedef struct { void *Buffer; unsigned Length; } WDF_MEMORY_DESCRIPTOR;
#define WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(p,b,n) (*(p)=(WDF_MEMORY_DESCRIPTOR){b,n})
typedef struct { int Timeout; } WDF_REQUEST_SEND_OPTIONS;
#define WDF_REQUEST_SEND_OPTION_TIMEOUT 1
#define WDF_REQUEST_SEND_OPTIONS_INIT(p,x) ((p)->Timeout=0)
#define WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(p,t) ((p)->Timeout=(t))
#define WDF_REL_TIMEOUT_IN_SEC(n) (n)
#define IOCTL_SPB_EXECUTE_SEQUENCE 1
static int result, calls;
static ULONG_PTR count = 4;
static USHORT expectedReg;
static NTSTATUS WdfIoTargetSendIoctlSynchronously(void *target, void *request,
        int ioctl, WDF_MEMORY_DESCRIPTOR *input, void *output,
        WDF_REQUEST_SEND_OPTIONS *options, ULONG_PTR *transferred)
{
    assert(target==(void *)1 && !request && ioctl==1 && !output && options->Timeout==2);
    LIST *s = input->Buffer;
    assert(input->Length==sizeof(LIST) && s->Count==2);
    assert(s->Transfers[0].Direction==1 && s->Transfers[1].Direction==2);
    assert(s->Transfers[0].Length==2 && s->Transfers[1].Length==2);
    assert(!s->Transfers[0].Delay && !s->Transfers[1].Delay);
    UCHAR *addr=s->Transfers[0].Buffer, *data=s->Transfers[1].Buffer;
    assert(addr[0]==(expectedReg>>8) && addr[1]==(expectedReg&255));
    data[0]=0x65; data[1]=0x30;
    *transferred=count; ++calls;
    return result;
}
static NTSTATUS WdfIoTargetSendWriteSynchronously(void *target, void *request,
        WDF_MEMORY_DESCRIPTOR *input, void *offset,
        WDF_REQUEST_SEND_OPTIONS *options, ULONG_PTR *transferred)
{
    const UCHAR expected[]={0xff,0xff,0,1};
    assert(target==(void *)1 && !request && !offset && options->Timeout==2);
    assert(input->Length==4 && !memcmp(input->Buffer,expected,4));
    *transferred=count; ++calls;
    return result;
}
'''
program += functions
program += r'''
int main(void) {
    const unsigned char fixture[] = {
      0x8e,0x1e,0,1,0,1,2,0,0,1,6,0,0x80,0x1a,6,0,0x1a,0,
      0x5c,0x5f,0x53,0x42,0x2e,0x50,0x43,0x49,0x30,0x2e,0x49,0x32,0x43,0x33,0,
      0x8c,0x25,0,1,0,1,0,5,0,0,0,0,0,0,0x17,0,0,0x19,0,0x28,0,0,0,0x57,0,
      0x5c,0x5f,0x53,0x42,0x2e,0x50,0x43,0x49,0x30,0x2e,0x47,0x50,0x49,0x4f,0,0x79,0
    };
    assert(CodecMatchesResource(fixture,sizeof(fixture)));
    assert(!CodecMatchesResource(NULL,sizeof(fixture)));
    for (unsigned n=0;n<sizeof(fixture);++n) {
        UCHAR *shortBuf=malloc(n?n:1); memcpy(shortBuf,fixture,n);
        assert(!CodecMatchesResource(shortBuf,n)); free(shortBuf);
    }
    assert(!CodecMatchesResource(fixture,sizeof(fixture)+1));
    for (unsigned i=0;i<sizeof(fixture);++i) {
        UCHAR copy[sizeof(fixture)]; memcpy(copy,fixture,sizeof(copy));
        for (unsigned bit=1;bit<256;bit<<=1) {
            copy[i]^=bit; assert(!CodecMatchesResource(copy,sizeof(copy))); copy[i]^=bit;
        }
    }
    CODEC_CONTEXT c={0}; ULONG value=0;
    assert(ReadRegister(&c,0xff,&value)==STATUS_DEVICE_NOT_READY && !calls);
    c.I2c=(void *)1;
    for (ULONG reg=0;reg<=UINT16_MAX;++reg) {
        expectedReg=(USHORT)reg;
        assert(ReadRegister(&c,expectedReg,&value)==0 && value==0x6530);
    }
    assert(c.Reads==65536);
    for (count=0;count<4;++count) {
        value=123;
        assert(ReadRegister(&c,expectedReg,&value)==STATUS_DEVICE_DATA_ERROR);
        assert(value==123 && c.Reads==65536);
        assert(SelectI2cMode(&c)==STATUS_DEVICE_DATA_ERROR);
    }
    assert(SelectI2cMode(&c)==0);
    result=-99; value=123;
    assert(ReadRegister(&c,expectedReg,&value)==-99 && value==123 && c.Reads==65536);
    assert(SelectI2cMode(&c)==-99);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='sof-codec-test-') as directory:
    p = pathlib.Path(directory)
    (p / 'test.c').write_text(program)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', '-I', str(root / 'driver/codec'),
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS: exact resource policy, all register address encodings, endian decoding, short transfers and error propagation')
