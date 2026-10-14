#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regenerate the original fixed alternating-note listening waveform."""
from pathlib import Path
import math
values=[]
for frequency in (750,1000):
    half=[round(32767*math.sin(2*math.pi*frequency*(i+.5)/48000)*min((i+.5)/384,1)) for i in range(1536)]
    values += half + [-v for v in reversed(half)]
assert len(values)==6144 and sum(values)==0 and max(map(abs,values))<=32767
path=Path(__file__).resolve().parents[1]/'driver/speaker/speaker-listen-samples.h'
source=path.read_text()
start=source.index('static const short SofSpeakerListenPcm[6144] = {')
end=source.index('};',start)+2
array='static const short SofSpeakerListenPcm[6144] = {\n'+''.join('    '+','.join(map(str,values[i:i+16]))+',\n' for i in range(0,len(values),16))+'};'
path.write_text(source[:start]+array+source[end:])
