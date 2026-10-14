#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Private acoustic analysis of the fixed 997/1499 Hz headphone test.

Includes headphone, room and microphone effects; this is not electrical THD.
Requires numpy and scipy. Writes only JSON to stdout; recordings stay local.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import welch


def analyze(path):
    rate, raw = wavfile.read(path)
    if rate != 48000 or raw.ndim != 2 or raw.shape[1] != 2 or raw.dtype != np.int32:
        raise ValueError('Expected 48 kHz stereo signed 32-bit microphone capture')
    if len(raw) != 5 * rate:
        raise ValueError('Expected a complete five-second diagnostic recording')
    data = raw.astype(np.float64) / 2147483648.0
    channels = []
    for channel in range(2):
        samples = data[rate // 2:rate * 9 // 2, channel]
        frequencies, psd = welch(samples, rate, nperseg=rate)
        def band(hz):
            return float(psd[np.abs(frequencies - hz) <= 2].sum())
        tones = {}
        for hz in (997, 1499):
            fundamental = band(hz)
            harmonics = sum(band(hz * n) for n in range(2, 6))
            tones[str(hz)] = {
                'fundamental_dbfs': float(10 * np.log10(max(fundamental, 1e-30))),
                'harmonics_2_to_5_percent': float(100 * np.sqrt(harmonics / max(fundamental, 1e-30))),
            }
        channels.append({
            'peak_dbfs': float(20 * np.log10(max(np.abs(data[:, channel]).max(), 1e-15))),
            'full_scale_samples': int(np.count_nonzero((raw[:, channel] == -2147483648) | (raw[:, channel] == 2147483647))),
            'tones': tones,
        })
    return {'recording': str(path), 'sample_rate': rate, 'channels': channels,
            'scope': 'Acoustic chain only; not calibrated electrical THD or a universal safe volume limit'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recording', type=Path)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    result = analyze(args.recording)
    if args.reference:
        ref = analyze(args.reference)
        result['reference'] = str(args.reference)
        for current, prior in zip(result['channels'], ref['channels']):
            for hz in ('997', '1499'):
                current['tones'][hz]['rise_from_reference_db'] = (
                    current['tones'][hz]['fundamental_dbfs'] - prior['tones'][hz]['fundamental_dbfs'])
    print(json.dumps(result, indent=2, allow_nan=False))
