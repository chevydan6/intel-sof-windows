#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read SSP1 hardware configuration directly from an ALSA ABI-5 topology.

Some alsatplg text decoders omit hw_config values. Layouts below follow Linux
include/uapi/sound/asoc.h. This tool does not load firmware or touch hardware.
"""
import argparse
import ctypes as c
import hashlib
import json
import pathlib
import struct

class Stream(c.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [('size', c.c_uint32), ('name', c.c_char * 44),
                ('format', c.c_uint64)] + [(n, c.c_uint32) for n in
                ('rate', 'period_bytes', 'buffer_bytes', 'channels')]

class Hw(c.LittleEndianStructure):
    _pack_ = 1
    _fields_ = ([(n, c.c_uint32) for n in ('size', 'id', 'fmt')] +
                [(n, c.c_uint8) for n in ('clock_gated', 'invert_bclk',
                  'invert_fsync', 'bclk_provider', 'fsync_provider', 'mclk_direction')] +
                [('reserved', c.c_uint16)] + [(n, c.c_uint32) for n in
                  ('mclk_rate', 'bclk_rate', 'fsync_rate', 'tdm_slots',
                   'tdm_slot_width', 'tx_slots', 'rx_slots', 'tx_channels')] +
                [('tx_chanmap', c.c_uint32 * 8), ('rx_channels', c.c_uint32),
                 ('rx_chanmap', c.c_uint32 * 8)])

class Link(c.LittleEndianStructure):
    _pack_ = 1
    _fields_ = ([('size', c.c_uint32), ('id', c.c_uint32),
                 ('name', c.c_char * 44), ('stream_name', c.c_char * 44),
                 ('stream', Stream * 8), ('num_streams', c.c_uint32),
                 ('hw_config', Hw * 8)] + [(n, c.c_uint32) for n in
                 ('num_hw_configs', 'default_hw_config_id', 'flag_mask', 'flags', 'priv_size')])

def inspect(path):
    data = path.read_bytes()
    offset = 0
    results = []
    while offset < len(data):
        if len(data) - offset < 36:
            raise ValueError('Truncated topology header')
        magic, abi, version, kind, size, vendor, payload, index, count = struct.unpack_from('<9I', data, offset)
        end = offset + size + payload
        if magic != 0x41536F43 or abi != 5 or size != 36 or end > len(data):
            raise ValueError('Unsupported or malformed topology block')
        if kind == 10:  # SND_SOC_TPLG_TYPE_BACKEND_LINK
            pos = offset + size
            for _ in range(count):
                if end - pos < c.sizeof(Link):
                    raise ValueError('Truncated link')
                link = Link.from_buffer_copy(data, pos)
                if link.size != c.sizeof(Link) or link.num_hw_configs > 8:
                    raise ValueError('Unsupported link layout')
                if pos + link.size + link.priv_size > end:
                    raise ValueError('Truncated link private data')
                if link.name == b'SSP1-Codec':
                    for hw in link.hw_config[:link.num_hw_configs]:
                        if hw.size != c.sizeof(Hw):
                            raise ValueError('Unsupported hardware configuration layout')
                        results.append({n: getattr(hw, n) for n, _ in Hw._fields_
                                        if not n.endswith('chanmap')})
                pos += link.size + link.priv_size
            if pos != end:
                raise ValueError('Unexpected trailing link data')
        offset = end
    if not results:
        raise ValueError('No SSP1-Codec configuration found')
    return {'sha256': hashlib.sha256(data).hexdigest(), 'SSP1-Codec': results}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('topology', type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.topology), indent=2))
