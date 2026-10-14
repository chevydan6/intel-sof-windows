#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Test the production format guard and attenuation without audio hardware."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef int32_t LONG;
typedef int64_t LONGLONG;
typedef struct { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } GUID;
#pragma pack(push, 1)
typedef struct {
    uint16_t wFormatTag, nChannels;
    uint32_t nSamplesPerSec, nAvgBytesPerSec;
    uint16_t nBlockAlign, wBitsPerSample, cbSize;
} WAVEFORMATEX;
typedef struct {
    WAVEFORMATEX Format;
    union { uint16_t wValidBitsPerSample; } Samples;
    uint32_t dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE;
#pragma pack(pop)
#define WAVE_FORMAT_EXTENSIBLE 0xfffe
#define KSAUDIO_SPEAKER_STEREO 3
#define RtlEqualMemory(a,b,n) (memcmp(a,b,n)==0)
#include "sofplay-format.h"
#include "sofplay-samples.h"
int main(void) {
    _Static_assert(sizeof(WAVEFORMATEX)==18, "Windows base ABI");
    _Static_assert(sizeof(WAVEFORMATEXTENSIBLE)==40, "Windows extensible ABI");
    WAVEFORMATEXTENSIBLE pcm = {
        {0xfffe,2,48000,384000,8,32,22}, {24}, 3,
        {1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}}
    }, f;
    assert(SofPlaySupportsFormat(&pcm));
    f=pcm; f.Samples.wValidBitsPerSample=32; assert(SofPlaySupportsFormat(&f));
    assert(!SofPlaySupportsFormat(NULL));
    /* Float has the same rate, channels and byte stride: formerly accepted. */
    f=pcm; f.SubFormat.Data1=3; assert(!SofPlaySupportsFormat(&f));
    /* Every byte of the subtype GUID matters, not just Data1. */
    for (unsigned i=0;i<sizeof(GUID);++i) {
        f=pcm; ((unsigned char *)&f.SubFormat)[i]^=1;
        assert(!SofPlaySupportsFormat(&f));
    }
    for (unsigned bits=0;bits<=64;++bits) {
        f=pcm; f.Samples.wValidBitsPerSample=bits;
        assert(SofPlaySupportsFormat(&f)==(bits==24 || bits==32));
    }
    f=pcm; f.Format.wBitsPerSample=24; assert(!SofPlaySupportsFormat(&f));
    f=pcm; f.Format.nSamplesPerSec=44100; assert(!SofPlaySupportsFormat(&f));
    f=pcm; f.Format.nAvgBytesPerSec=192000; assert(!SofPlaySupportsFormat(&f));
    f=pcm; f.Format.nChannels=1; assert(!SofPlaySupportsFormat(&f));
    f=pcm; f.Format.nBlockAlign=4; assert(!SofPlaySupportsFormat(&f));
    f=pcm; f.dwChannelMask=12; assert(!SofPlaySupportsFormat(&f));
    /* Exact base-sized allocations exercise early rejection without reading
       the absent extension. Invalid declared sizes 0..21 are all rejected. */
    for (unsigned n=0;n<22;++n) {
        WAVEFORMATEX *base=malloc(sizeof(*base)); *base=pcm.Format;
        base->cbSize=n;
        assert(!SofPlaySupportsFormat((WAVEFORMATEXTENSIBLE *)base)); free(base);
    }
    WAVEFORMATEX *base=malloc(sizeof(*base)); *base=pcm.Format;
    base->wFormatTag=1;
    assert(!SofPlaySupportsFormat((WAVEFORMATEXTENSIBLE *)base)); free(base);
    /* A 1 kHz sine represented as Windows 24-in-32 PCM. Measure actual
       production conversion gain and its residual, including both signs. */
    double inEnergy=0, outEnergy=0, dot=0, maxResidual=0;
    const unsigned frames=48000;
    LONG *input=malloc(frames*sizeof(LONG)), *output=malloc(frames*sizeof(LONG));
    for (unsigned i=0;i<frames;++i) {
        double x=.5*sin(2*3.14159265358979323846*1000*i/48000);
        input[i]=(LONG)llround(x*8388607)*256;
        output[i]=SofPlayAttenuateSpeakerSample(input[i]);
        inEnergy+=(double)input[i]*input[i];
        outEnergy+=(double)output[i]*output[i];
        dot+=(double)input[i]*output[i];
    }
    double gain=dot/inEnergy, db=10*log10(outEnergy/inEnergy);
    for (unsigned i=0;i<frames;++i) {
        double residual=fabs(output[i]-gain*input[i]);
        if (residual>maxResidual) maxResidual=residual;
    }
    assert(fabs(db+9)<.001);
    assert(maxResidual<2); /* below two S32 least-significant units */
    printf("PASS: format rejection/ABI bounds; sine gain %.6f dB, residual %.3f S32 units\n",db,maxResidual);
    free(input); free(output);
}
'''
with tempfile.TemporaryDirectory(prefix='sof-format-') as directory:
    c = pathlib.Path(directory) / 'test.c'
    binary = pathlib.Path(directory) / 'test'
    c.write_text(program)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(root / 'driver/acx'), str(c), '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
