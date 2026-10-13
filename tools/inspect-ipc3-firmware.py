#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Inspect an uncompressed SOF IPC3 image without loading it to hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MAGIC = 0x6E614D58  # XMan
HEADER = struct.Struct('<IIII')
ELEMENT = struct.Struct('<II')


def inspect(data: bytes) -> dict:
    if len(data) < HEADER.size:
        raise ValueError('image is shorter than the extended manifest header')
    magic, full_size, header_size, version = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError('not an IPC3 XMan image')
    if not HEADER.size <= header_size <= full_size < len(data):
        raise ValueError('invalid manifest or payload boundary')
    if version >> 24 != 1:
        raise ValueError(f'unsupported extended manifest major version: {version >> 24}')
    if (full_size | header_size) & 3:
        raise ValueError('manifest boundaries are not 4-byte aligned')
    elements = []
    offset = header_size
    while offset < full_size:
        if offset + ELEMENT.size > full_size:
            raise ValueError('truncated manifest element header')
        kind, size = ELEMENT.unpack_from(data, offset)
        if size < ELEMENT.size or size & 3 or size > full_size - offset:
            raise ValueError(f'invalid manifest element at offset {offset}')
        elements.append({'type': kind, 'offset': offset, 'size': size})
        offset += size
    payload = data[full_size:]
    return {
        'format': 'SOF IPC3 XMan',
        'image_size': len(data),
        'manifest_size': full_size,
        'manifest_header_size': header_size,
        'manifest_version': f'0x{version:08X}',
        'elements': elements,
        'dma_payload_offset': full_size,
        'dma_payload_size': len(payload),
        'dma_payload_sha256': hashlib.sha256(payload).hexdigest(),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path, help='uncompressed SOF IPC3 .ri file')
    args = parser.parse_args()
    print(json.dumps(inspect(args.image.read_bytes()), indent=2))


if __name__ == '__main__':
    main()
