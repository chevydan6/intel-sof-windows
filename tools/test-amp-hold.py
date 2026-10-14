#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise production GPIO-resource policy and held D0Entry without hardware."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'driver/acx/sofplay-startup.inc').read_text()
start = source.index('NTSTATUS\nSofPlayEvtD0Entry(')
body = source.index('{', start)
depth = 1
end = body + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
entry = source[start:end]

program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sofamp-policy.h"
#define _In_
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_NOT_READY (-1)
#define UNREFERENCED_PARAMETER(x) (void)(x)
typedef int NTSTATUS;
typedef int WDF_POWER_DEVICE_STATE;
typedef struct Context {
    int AcxPowerLock, AcxBufferLock, AcxHardwareReady;
    void *AcxRing;
    unsigned AcxStartupCount, AcxStartupStage;
} *PSOFPROBE_DEVICE_CONTEXT, *WDFDEVICE;
static int powerLocked, bufferLocked, saves, savedStatus;
static PSOFPROBE_DEVICE_CONTEXT SofProbeGetContext(WDFDEVICE d) { return d; }
static void WdfWaitLockAcquire(int lock, void *timeout) {
    (void)lock; (void)timeout; assert(!powerLocked); powerLocked = 1;
}
static void WdfWaitLockRelease(int lock) {
    (void)lock; assert(powerLocked && !bufferLocked); powerLocked = 0;
}
static void WdfSpinLockAcquire(int lock) {
    (void)lock; assert(powerLocked && !bufferLocked); bufferLocked = 1;
}
static void WdfSpinLockRelease(int lock) {
    (void)lock; assert(bufferLocked); bufferLocked = 0;
}
static void SofPlaySaveStartupResult(WDFDEVICE d, int status) {
    assert(powerLocked && !d->AcxHardwareReady && d->AcxRing == NULL);
    savedStatus = status; ++saves;
}
'''
program += entry
program += r'''
int main(void) {
    /* Captured PUJJOTEEN _CRS fixture: exactly one output pin, 75. */
    unsigned char crs[] = {
        0x8c,0x25,0,1,1,1,0,2,0,0,0,0,0,0,0x17,0,0,0x19,0,
        0x28,0,0,0,0x4b,0,0x5c,0x5f,0x53,0x42,0x2e,0x50,0x43,
        0x49,0x30,0x2e,0x47,0x50,0x49,0x4f,0,0x79,0
    };
    assert(SofAmpMatchesResource(crs, sizeof(crs)));
    assert(!SofAmpMatchesResource(NULL, sizeof(crs)));
    for (unsigned n = 0; n < sizeof(crs); ++n) {
        /* Allocation ends exactly at supplied size: catch overreads. */
        unsigned char *shortBuffer = malloc(n ? n : 1);
        memcpy(shortBuffer, crs, n);
        assert(!SofAmpMatchesResource(shortBuffer, n));
        free(shortBuffer);
    }
    assert(!SofAmpMatchesResource(crs, sizeof(crs) + 1));
    /* Reject altered pin, direction, controller path, and every other bit. */
    for (unsigned i = 0; i < sizeof(crs); ++i)
        for (unsigned bit = 1; bit < 256; bit <<= 1) {
            crs[i] ^= bit;
            assert(!SofAmpMatchesResource(crs, sizeof(crs)));
            crs[i] ^= bit;
        }
    struct Context d = {0};
    for (int state = 0; state < 8; ++state) {
        d.AcxHardwareReady = 1; d.AcxRing = &d;
        assert(SofPlayEvtD0Entry(&d, state) == STATUS_SUCCESS);
        assert(!d.AcxHardwareReady && d.AcxRing == NULL);
        assert(d.AcxStartupCount == (unsigned)state + 1);
        assert(d.AcxStartupStage == 7 && savedStatus == STATUS_DEVICE_NOT_READY);
        assert(!powerLocked && !bufferLocked && saves == state + 1);
    }
    /* No firmware/DMA/GPIO-enable stubs exist: adding those calls to the
       held entrypoint fails compilation rather than silently mocking them. */
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='sofamp-test-') as directory:
    c = pathlib.Path(directory) / 'test.c'
    binary = pathlib.Path(directory) / 'test'
    c.write_text(program)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(root / 'driver/amp'), str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: exact GPIO policy, malformed resources, and held D0 entry (ASan/UBSan)')
