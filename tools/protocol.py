# SPDX-License-Identifier: GPL-2.0-or-later
"""Render this machine's side of #15's protocol (T1-T8) into OUTDIR with the protocol's file names, each test
mapped onto the SRC page's knobs. protocol.py RENDER OUTDIR: RENDER is the built tools/render.c."""
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf

RENDER, OUT = sys.argv[1], sys.argv[2]
RATE = 48000
A1, A2, A3, A5 = 33, 45, 57, 81
os.makedirs(OUT, exist_ok=True)


def render(seconds, note, **knobs):
    with tempfile.NamedTemporaryFile(suffix='.wav') as f:
        args = [RENDER, f.name, 'note=%d' % note, 'seconds=%g' % seconds]
        args += ['%s=%s' % (k, v) for k, v in knobs.items()]
        subprocess.run(args, check=True, stdout=subprocess.DEVNULL)
        return sf.read(f.name, dtype='float32')[0]


def save(name, *parts):
    sf.write(os.path.join(OUT, name + '.wav'), np.concatenate(parts), RATE, subtype='PCM_24')
    print(name)


gap = np.zeros(RATE, np.float32)


def notes(name, notes_=(A1, A3, A5), seconds=4, **knobs):
    parts = []
    for n in notes_:
        parts += [render(seconds, n, **knobs), gap]
    save(name, *parts)


def sweep(name, knob, a, b, note=A3, **knobs):
    lead = render(1, note, **dict(knobs, **{knob: a}))
    save(name, lead, render(20, note, sweep='%s:%d:%d' % (knob, a, b), **knobs), render(1, note, **dict(knobs, **{knob: b})), gap)


# T1: saw type, WAV1 0-63 (triangle to saw) at 0, 1/4, 1/2, 3/4, full
for pos, w in ((0, 0), (25, 16), (50, 32), (75, 47), (100, 63)):
    notes('T1-saw-%d' % pos, wav1=w)
sweep('T1-saw-sweep', 'wav1', 0, 63)
# T2: pulse type, WAV1 64-127 (narrowest to square)
for pos, w in ((0, 64), (25, 80), (50, 96), (75, 111), (100, 127)):
    notes('T2-pulse-%d' % pos, wav1=w)
sweep('T2-pulse-sweep', 'wav1', 64, 127)
# T3: MIX1 H/- (6), -/H (2), H/H (8), L/- (3): the OB-1's HALF, as "half travel"
notes('T3-fund', (A3,), mix1=6)
notes('T3-sub', (A3,), mix1=2)
notes('T3-both', (A3,), mix1=8)
notes('T3-fund-half', (A3,), mix1=3)
# T4: VCO2 alone, FRQ2 0, 32, 63
for name, f in (('min', 0), ('mid', 32), ('max', 63)):
    notes('T4-freq-' + name, (A3,), mix1=0, mix2=6, frq2=f)
sweep('T4-freq-sweep', 'frq2', 0, 63, mix1=0, mix2=6)
# T5: sync, VCO2 alone
for st in (0, 7, 12, 19):
    notes('T5-sync-%d' % st, (A3,), mix1=0, mix2=6, mod=1, frq2=32 + st)
sweep('T5-sync-sweep', 'frq2', 32, 63, note=A2, mix1=0, mix2=6, mod=1)
notes('T5-sync-12-square', (A3,), mix1=0, mix2=6, mod=1, frq2=44, wav2=127)
# T6: cross mod, VCO1 alone (VCO2 mixed off but still running: the machine keeps it for XMOD)
notes('T6-xmod-slow', (A3,), seconds=10, mix1=6, mod=2, frq2=0)
for name, st in (('0', 0), ('m12', -12), ('m24', -24)):
    notes('T6-xmod-' + name, (A3,), mix1=6, mod=2, frq2=32 + st)
notes('T6-xmod-off', (A3,), seconds=10, mix1=6, mod=0, frq2=0)
# T7: noise (ours is white only)
notes('T7-noise-white', (A3,), mix1=0, nois=2)
notes('T7-noise-vs-fund', (A3,), mix1=6, nois=2)
# T8: no Vintage: a held note, then eight short ones
for name in ('T8-vintage-default', 'T8-vintage-max'):
    parts = [render(20, A3), np.zeros(RATE // 2, np.float32)]
    for _ in range(8):
        parts += [render(0.5, A3), np.zeros(RATE // 2, np.float32)]
    save(name, *parts)
