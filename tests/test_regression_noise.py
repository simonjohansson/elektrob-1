# SPDX-License-Identifier: GPL-2.0-or-later
"""Detailed noise coloration belongs to the inexpensive host render boundary."""
import sys
import tempfile
import unittest
import wave
from pathlib import Path
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from regression import DSP, ROOT, cases
from regression_assert import measurements, read_json


class NoiseSpectrumTest(unittest.TestCase):
    def test_long_noise_histories_and_deliberate_bass_cut(self):
        profile = read_json(ROOT/'tests/reference/recorded/assertions.json')
        reference = read_json(ROOT/'tests/reference/recorded/measurements.json')
        baseline = np.asarray(reference['cases']['mix-0-0-2']['metrics']['band_rms_dbfs'][0])
        baseline += profile['targets']['dsp']['gain_db']
        budget = profile['tolerances']['spectrum_db']
        case = next(c for c in cases() if c['id'] == 'mix-0-0-2')
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            renderer = DSP(SimpleNamespace(settle=1, seconds=128), folder)
            renderer.capture(case, folder)
            with wave.open(str(folder/'audio.wav'), 'rb') as wav:
                for history in range(8):
                    wav.setpos((1+16*history)*48000)
                    raw = np.frombuffer(wav.readframes(16*48000), dtype=np.uint8).reshape(-1,3).astype(np.int32)
                    value = raw[:,0] | raw[:,1]<<8 | raw[:,2]<<16
                    signal = ((value ^ 8388608)-8388608)/8388608
                    bands = np.asarray(measurements(signal[:,None], False)['band_rms_dbfs'][0])
                    with self.subTest(history=history):
                        np.testing.assert_array_less(abs(bands[:4]-baseline[:4]), budget)
                    if history == 0:
                        spectrum = np.fft.rfft(signal)
                        frequency = np.fft.rfftfreq(len(signal), 1/48000)
                        spectrum[frequency < 80] *= 10**(-3/20)
                        changed = np.fft.irfft(spectrum, n=len(signal))
                        changed_bands = measurements(changed[:,None], False)['band_rms_dbfs'][0]
                        self.assertGreater(abs(changed_bands[0]-baseline[0]), budget)


if __name__ == '__main__':
    unittest.main()
