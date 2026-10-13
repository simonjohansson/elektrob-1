# SPDX-License-Identifier: GPL-2.0-or-later
"""f0, harmonic levels and the aliasing floor of a WAV: `mise run analyze FILE.wav`."""
import sys

import numpy as np
import soundfile as sf

x, rate = sf.read(sys.argv[1], dtype='float64', always_2d=True)
x = x[:, 0]
loud = np.nonzero(np.abs(x) > 0.01)[0]
if not len(loud):
    sys.exit('analyze: silent')
x = x[loud[0]:loud[-1]]
n = min(len(x), rate)                          # the loudest second, or all of it
start = max(range(0, len(x) - n + 1, max(n // 4, 1)), key=lambda i: np.sum(x[i:i + n] ** 2))
spec = np.abs(np.fft.rfft(x[start:start + n] * np.blackman(n)))
freqs = np.fft.rfftfreq(n, 1 / rate)
ref = spec.max()
# f0: the lowest peak within 40 dB of the loudest (a folded wave's 3rd harmonic can outshine its 1st)
peaks = [i for i in range(1, len(spec) - 1) if freqs[i] > 20 and spec[i] > ref / 100
         and spec[i] >= spec[i - 1] and spec[i] >= spec[i + 1]]
f0 = freqs[peaks[0]]
db = lambda v: 20 * np.log10(max(v, 1e-12) / ref)
print('rate %d Hz, peak %.3f, f0 %.1f Hz' % (rate, np.abs(x).max(), f0))
harmonic = np.zeros(len(spec), bool)
for k in range(1, int(rate / 2 / f0) + 1):
    b = int(round(k * f0 * n / rate))
    harmonic[max(b - 4, 0):b + 5] = True
    if k <= 10:
        print('H%-2d %7.0f Hz %7.1f dB' % (k, k * f0, db(spec[max(b - 4, 0):b + 5].max())))
rest = spec[~harmonic & (freqs > 20)]
print('aliasing floor (strongest non-harmonic bin): %.1f dB' % db(rest.max() if len(rest) else 0))
