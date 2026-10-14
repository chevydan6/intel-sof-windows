#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check cross-message SSP identity using the SOF IPC3 ABI field offsets.

Reference: thesofproject/sof v2.2 src/include/ipc/{topology,dai}.h.
The component tail is direction/index/type/reserved; the DAI configuration
header instead puts type before index. Reversing them worked accidentally on
SSP1 because type=1 and index=1, but rejected SSP0 with -EINVAL.
"""
from pathlib import Path
import re
s = Path('driver/acx/sofplay-startup.inc').read_text()
s = s[s.index('SofPlayPrepareSpeaker('):]
s = s.split('#ifdef SOFPLAY_HEADPHONE', 1)[1].split('#else', 1)[0]
def words(name):
    match = re.search(r'const ULONG ' + name + r'\[\] = \{([^}]+)\}', s)
    assert match, name
    a = [int(x.strip(),0) for x in match[1].split(',')]
    assert a[0] == len(a)*4, (name, 'IPC size')
    return a
host, dai, config = map(words, ['host','dai','config'])
assert dai[17] == config[3] == 0, 'Component and configuration must both use SSP0'
assert dai[18] == config[2] == 1, 'DAI type must be Intel SSP'
assert dai[16] == host[16] == 0, 'Both components must use playback direction'
assert dai[12] == host[12] == 1, 'Both components must carry S24_4LE'
assert config[16] == 48000
assert config[17] == config[16]*config[18]*config[22], 'BCLK must cover slot width and frame rate'
assert config[21] <= config[22], 'Valid bits must fit within each slot'
assert config[19] == config[20] == (1 << config[18])-1, 'Slot masks must match stereo slots'
print('PASS: headphone IPC3 component/configuration identity, format, direction, clocks and slot masks')
