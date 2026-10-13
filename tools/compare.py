# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare #15's captures with this machine's renders of the same tests (tools/protocol.py).
compare.py REFDIR OURDIR: prints, per test, what each side measures. Files missing on either side are
skipped. Notes are found by their silences, and measured over their steady middle."""
import os
import sys

import numpy as np
import soundfile as sf

REF, OURS = sys.argv[1], sys.argv[2]


def load(d, name):
    p = os.path.join(d, name + '.wav')
    if not os.path.exists(p):
        return None
    x, sr = sf.read(p, dtype='float64', always_2d=True)
    return x.mean(axis=1), sr


def notes(x, sr):
    """The notes in x: (start, end) sample spans louder than -50 dB of the file's peak, at least 0.3 s."""
    win = sr // 100
    env = np.sqrt(np.convolve(x * x, np.ones(win) / win, 'same'))
    on = env > np.abs(x).max() * 10 ** (-50 / 20)
    spans, i = [], 0
    while i < len(on):
        if on[i]:
            j = i
            while j < len(on) and on[j]:
                j += 1
            if j - i > 0.3 * sr:
                spans.append((i, j))
            i = j
        else:
            i += 1
    return spans


def steady(x, sr, span, seconds=1.0):
    a, b = span
    mid = (a + b) // 2
    n = min(int(seconds * sr), (b - a) // 2)
    return x[mid - n // 2:mid + n // 2]


def spectrum(seg, sr):
    sp = np.abs(np.fft.rfft(seg * np.blackman(len(seg))))
    return sp, np.fft.rfftfreq(len(seg), 1 / sr)


def f0(seg, sr):
    sp, f = spectrum(seg, sr)
    ref = sp.max()
    for i in range(1, len(sp) - 1):
        if f[i] > 20 and sp[i] > ref / 100 and sp[i] >= sp[i - 1] and sp[i] >= sp[i + 1]:
            a, b, c = np.log(sp[i - 1:i + 2] + 1e-20)
            return f[i] + (a - c) / (2 * (a - 2 * b + c)) * (f[1] - f[0])
    return float('nan')


def harmonics(seg, sr, n=8):
    sp, f = spectrum(seg, sr)
    fund = f0(seg, sr)
    df = f[1] - f[0]
    lv = []
    for k in range(1, n + 1):
        i = int(round(k * fund / df))
        lv.append(sp[max(i - 3, 0):i + 4].max() if i < len(sp) else 1e-20)
    return fund, [20 * np.log10(v / lv[0]) for v in lv]


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def rms(seg):
    return db(np.sqrt(np.mean(seg ** 2)))


def duty(levels):
    """The duty cycle whose pulse spectrum, |sin(pi k d)| / k against k = 1, best fits the harmonic levels."""
    k = np.arange(1, len(levels) + 1)
    best = None
    for d in np.arange(0.01, 0.5001, 0.0025):
        model = np.abs(np.sin(np.pi * k * d)) / k
        model = 20 * np.log10(np.maximum(model / model[0], 1e-6))
        err = np.sum((np.maximum(model, -60) - np.maximum(levels, -60)) ** 2)
        best = min(best or (err, d), (err, d))
    return best[1]


def cycle_freqs(seg, sr):
    """A saw's frequency cycle by cycle: the time between its resets, the drops of over half its swing within
    a sample, placed to a fraction of a sample. Holds even when the pitch moves at audio rate."""
    d = np.diff(seg)
    swing = np.percentile(seg, 99) - np.percentile(seg, 1)
    drops = np.nonzero((d < -0.5 * swing) & (np.r_[d[1:], 0] > d) & (np.r_[0, d[:-1]] >= d))[0]
    if len(drops) < 3:
        return np.array([])
    return sr / np.diff(drops.astype(float))


def side(name, fn):
    out = []
    for d in (REF, OURS):
        got = load(d, name)
        out.append(fn(*got) if got else None)
    return out


def show(title, name, fn, fmt):
    ref, ours = side(name, fn)
    if ref is None and ours is None:
        return
    print('  %-28s ref %-40s ours %s' % (title, fmt(ref) if ref is not None else '-', fmt(ours) if ours is not None else '-'))


def per_note(fn):
    return lambda x, sr: [fn(steady(x, sr, s), sr) for s in notes(x, sr)]


hfmt = lambda r: ' | '.join('%.0fHz %s' % (f, ' '.join('%.0f' % v for v in lv[1:6])) for f, lv in r)

print('T1 saw type: f0, then H2-H6 in dB against H1, for each note')
for pos in (0, 25, 50, 75, 100):
    show('T1-saw-%d' % pos, 'T1-saw-%d' % pos, per_note(harmonics), hfmt)
show('sweep: H2 at 0,25,50,75,100 %', 'T1-saw-sweep',
     lambda x, sr: [harmonics(x[int((1 + 20 * p) * sr):int((1.5 + 20 * p) * sr)], sr)[1][1] for p in (0, .25, .5, .75, 1 - 0.03)],
     lambda r: ' '.join('%.0f' % v for v in r))
print('T2 pulse type: fitted duty cycle (or 1 - it) for each note')
for pos in (0, 25, 50, 75, 100):
    show('T2-pulse-%d' % pos, 'T2-pulse-%d' % pos, per_note(lambda s, sr: duty(harmonics(s, sr, 12)[1])),
         lambda r: ' '.join('%.1f%%' % (100 * d) for d in r))
print('T3 levels in dB against T3-fund')
base = side('T3-fund', lambda x, sr: rms(steady(x, sr, notes(x, sr)[0])))
for name in ('T3-sub', 'T3-both', 'T3-fund-half'):
    vals = side(name, lambda x, sr: rms(steady(x, sr, notes(x, sr)[0])))
    rel = [None if v is None or b is None else v - b for v, b in zip(vals, base)]
    print('  %-28s ref %-40s ours %s' % (name, '-' if rel[0] is None else '%+.1f dB' % rel[0], '-' if rel[1] is None else '%+.1f dB' % rel[1]))
print('T4 VCO2 frequency, in semitones from A3 (220 Hz)')
for name in ('min', 'mid', 'max'):
    show('T4-freq-' + name, 'T4-freq-' + name, lambda x, sr: f0(steady(x, sr, notes(x, sr)[0]), sr),
         lambda r: '%.1f Hz (%+.1f st)' % (r, 12 * np.log2(r / 220)))
print('T5 sync: f0, then H2-H6 in dB')
for name in ('T5-sync-0', 'T5-sync-7', 'T5-sync-12', 'T5-sync-19', 'T5-sync-12-square'):
    show(name, name, per_note(harmonics), hfmt)
def mean_freq(x, sr):
    """A saw's mean frequency over 4 s: its resets a second, exact however fast its pitch moves."""
    p = cycle_freqs(steady(x, sr, notes(x, sr)[0], 4.0), sr)
    return len(p) / np.sum(1 / p), 12 * np.log2(np.percentile(p, 98) / np.percentile(p, 2))


def depth(ratio):
    """The depth D (octaves each way) of a saw sweeping VCO1 by 2^u, u even over [-D, D], that raises its mean
    frequency by `ratio`: (2^D - 2^-D) / (2 D ln 2), solved by bisection."""
    lo, hi = 1e-6, 8.0
    for _ in range(60):
        d = (lo + hi) / 2
        lo, hi = (d, hi) if (2 ** d - 2 ** -d) / (2 * d * np.log(2)) < ratio else (lo, d)
    return (lo + hi) / 2


print('T6 cross mod: VCO1\'s mean frequency with it, against without, and the depth (octaves each way at')
print('   VCO2\'s saw\'s ends) that raises it so; the cycle-by-cycle swing, smeared at audio rate, for reference')
slow, off = side('T6-xmod-slow', mean_freq), side('T6-xmod-off', mean_freq)
for label, a, b in (('ref', slow[0], off[0]), ('ours', slow[1], off[1])):
    if a and b:
        print('  %-28s %-4s %.1f Hz against %.1f Hz: depth +-%.2f oct (swing %.1f st)' % (
            'T6-xmod-slow', label, a[0], b[0], depth(a[0] / b[0]), a[1]))
for name in ('T6-xmod-0', 'T6-xmod-m12', 'T6-xmod-m24'):
    show(name, name, per_note(harmonics), hfmt)
print('T7 noise: slope (dB/octave, 200 Hz-8 kHz) and level against T3-fund')
for name in ('T7-noise-white', 'T7-noise-pink'):
    def slope(x, sr):
        seg = steady(x, sr, notes(x, sr)[0], 2.0)
        sp, f = spectrum(seg, sr)
        bands = [(200 * 2 ** i, 400 * 2 ** i) for i in range(5)]
        p = [10 * np.log10(np.mean(sp[(f >= a) & (f < b)] ** 2)) for a, b in bands]
        return np.polyfit(range(len(p)), p, 1)[0], rms(seg)
    ref, ours = side(name, slope)
    fmt = lambda r, b: '-' if r is None else '%+.1f dB/oct, %+.1f dB' % (r[0], r[1] - (b or 0))
    print('  %-28s ref %-40s ours %s' % (name, fmt(ref, base[0]), fmt(ours, base[1])))
print('T8 pitch: the held note\'s drift (cents, first to last second) and each short note (cents from 220 Hz)')
for name in ('T8-vintage-default', 'T8-vintage-max'):
    def drift(x, sr):
        sp = notes(x, sr)
        held = x[sp[0][0]:sp[0][1]]
        a, b = f0(held[sr:2 * sr], sr), f0(held[-2 * sr:-sr], sr)
        shorts = [1200 * np.log2(f0(steady(x, sr, s, 0.3), sr) / 220) for s in sp[1:]]
        return 1200 * np.log2(b / a), shorts
    show(name, name, drift, lambda r: '%+.1f c; %s' % (r[0], ' '.join('%+.0f' % c for c in r[1])))
