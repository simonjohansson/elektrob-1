# SPDX-License-Identifier: GPL-2.0-or-later
"""Send a firmware .syx to a Digitakt mk1 over USB MIDI: upload.py FILE.syx [--port NAME] [--delay MS] [--yes].

The unit must be in its bootloader's OS UPGRADE mode: hold FUNC while powering on, then press TRIG 4. That
route takes a plain stream of the file's SysEx messages (it checks the container's checksums before it
writes anything), so it needs no Elektron Transfer. The messages go one at a time, `--delay` ms apart."""
import argparse
import sys
import time

import mido

p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
p.add_argument('syx')
p.add_argument('--port', default='Digitakt', help='a MIDI output whose name contains this (default: Digitakt)')
p.add_argument('--delay', type=float, default=10, help='ms between messages (default: 10)')
p.add_argument('--yes', action='store_true', help='send without asking')
a = p.parse_args()

data = open(a.syx, 'rb').read()
msgs, start = [], None
for i, b in enumerate(data):
    if b == 0xf0:
        start = i
    elif b == 0xf7 and start is not None:
        msgs.append(data[start + 1:i])
        start = None
if not msgs or data[:4] != b'\xf0\x00\x20\x3c':
    sys.exit('upload: %s is not an Elektron SysEx file' % a.syx)

ports = [n for n in mido.get_output_names() if a.port.lower() in n.lower()]
if len(ports) != 1:
    sys.exit('upload: %s MIDI output matches "%s" (outputs: %s)' % (
        'no' if not ports else 'more than one', a.port, ', '.join(mido.get_output_names()) or 'none'))

seconds = len(msgs) * a.delay / 1000
print('%s: %d messages to "%s", about %.0f s.' % (a.syx, len(msgs), ports[0], seconds))
print('The Digitakt must show OS UPGRADE (hold FUNC at power-on, then TRIG 4). Don\'t power it off until it\'s done.')
if not a.yes and input('Send? [y/N] ').strip().lower() != 'y':
    sys.exit('upload: not sent')

with mido.open_output(ports[0]) as out:
    for n, m in enumerate(msgs, 1):
        out.send(mido.Message('sysex', data=m))
        time.sleep(a.delay / 1000)
        if n % 500 == 0 or n == len(msgs):
            print('\r  %d / %d' % (n, len(msgs)), end='', flush=True)
print('\nupload: sent. The Digitakt writes and restarts; if it reports an error, send it again or the stock OS.')
