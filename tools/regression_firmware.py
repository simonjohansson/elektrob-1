# SPDX-License-Identifier: GPL-2.0-or-later
"""Acquisition through the pinned emulator's actual front-panel input model."""
import contextlib
import json
import os
import struct
import sys

from regression import ROOT, play, source_controls, write_json, write_wav


class Firmware:
    def __init__(self, args, out):
        sys.path.insert(0, str(ROOT / 'vendor/digiemu'))
        from emu.bootstrap import first_run
        from emu.fwcheck import prepare
        from emu.session import Session
        from emu.longrun import never_fake_set_posts

        self.args, self.out, self.session = args, out, None
        self.events = []
        core = json.loads((ROOT / 'vendor/elekloader/mods/core/mod.json').read_text())
        definitions = core['defsym'] | core.get('ports', {}).get(args.os, {}).get('defsym', {})
        self.symbols = {k: int(v, 0) for k, v in definitions.items()}
        try:
            with (out / 'firmware.log').open('w') as log, contextlib.redirect_stdout(log):
                paths, device = prepare(str(args.firmware.resolve()), str(out / 'emulator'))
                if device.short != 'dt1':
                    raise ValueError('Only Digitakt mk1 is supported')
                os.environ.update(paths.env())
                snapshot = first_run(paths)
                self.session = Session(snapshot, paths.syx, hle=False)
                never_fake_set_posts(self.session.m, self.session.ev, self.session.profile.set_post)
            self.wait(.1)
            self.chord('FUNC', 'SRC')
            for _ in range(4):
                self.tap('DOWN')
            self.tap('YES')
            kit = int.from_bytes(self.read('FW_KIT', 4), 'big')
            if bytes(self.session.m.uc.mem_read(kit + 0x20 + 0x7e, 1)) != b'\x45':
                raise RuntimeError('Panel did not select OB-1 machine 69 on track 1')
            self.tap('AMP')
            for encoder, slot, value in ((1, 38, 0), (2, 39, 127), (3, 40, 0), (4, 41, 0),
                                         (5, 42, 0), (6, 43, 0), (7, 44, 64), (8, 45, 127)):
                self.set_knob(encoder, slot, value)
            self.screen(out / 'amp.png')
            self.tap('FLTR')
            self.set_knob(7, 25, 0)
            self.set_knob(6, 27, 0)
            self.screen(out / 'filter.png')
            # A fresh isolated card supplies the remaining neutral settings; verify them.
            self.require_slots({4: 0, 8: 64, 12: 0, 16: 64, 25: 0, 27: 0, 28: 64,
                                34: 127, 35: 0, 36: 0, 38: 0, 39: 127, 40: 0,
                                41: 0, 42: 0, 43: 0, 44: 64, 45: 127})
            # Native kit header: track-1 level; FX record: compressor dry/wet.
            kit = int.from_bytes(self.read('FW_KIT', 4), 'big')
            level = int.from_bytes(self.session.m.uc.mem_read(kit + 0x10, 2), 'big')
            compressor = int.from_bytes(self.session.m.uc.mem_read(kit + 0x580, 2), 'big')
            if level != 100 << 8 or compressor != 0:
                raise RuntimeError('Fresh project does not have track level 100 and dry compressor')
            self.tap('SRC')
            self.source_page = 0
            self.chord('FUNC', 'TRK')
            self.octave = 0
            write_json(out / 'setup.json', dict(sound_slots_8_8=self.sound(), panel=self.session.inputs,
                                               track_level_8_8=level, compressor_mix_8_8=compressor))
        except BaseException:
            self.close()
            raise

    def read(self, symbol, count):
        return bytes(self.session.m.uc.mem_read(self.symbols[symbol], count))

    def sound(self):
        kit = int.from_bytes(self.read('FW_KIT', 4), 'big')
        return list(struct.unpack('>53h', self.session.m.uc.mem_read(kit + 0x20 + 20, 106)))

    def require_slots(self, expected):
        actual = self.sound()
        mismatches = {str(k): dict(requested=v, actual=actual[k] / 256)
                      for k, v in expected.items() if actual[k] != v << 8}
        if mismatches:
            raise RuntimeError('Setup readback mismatch: ' + json.dumps(mismatches))

    def wait(self, seconds):
        self.session.run_ms(seconds * 1000)
        if self.session.halted:
            raise RuntimeError(self.session.halted)

    def tap(self, key):
        self.session.tap(self.session.code(key), hold_ms=80, after_ms=120)
        if self.session.halted:
            raise RuntimeError(self.session.halted)

    def chord(self, modifier, key):
        self.session.press(self.session.code(modifier))
        try:
            self.wait(.08)
            self.tap(key)
        finally:
            self.session.release(self.session.code(modifier))
        self.wait(.12)

    def set_knob(self, encoder, slot, value, zones=None):
        for _ in range(256):
            current = self.sound()[slot]
            actual = current >> 8
            reached = actual == value if zones is None else actual * zones >> 7 == value * zones >> 7
            if reached and (zones is not None or value not in (0, 127) or current == value << 8):
                return
            delta = value * 256 - current
            # Stay within one panel packet; allow the firmware to process each turn.
            detents = (1 if delta > 0 else -1) * min(8, max(1, abs(delta) // 512))
            self.session.turn(encoder, detents, after_ms=150)
            if self.session.halted:
                raise RuntimeError(self.session.halted)
        raise RuntimeError(f'Encoder {encoder} could not set slot {slot} to {value}: {self.sound()[slot] / 256}')

    def screen(self, path):
        from emu import panel
        if not self.session.frames:
            raise RuntimeError('Firmware has not drawn a screen')
        panel.write_png(self.session.frames[-1][1], str(path), scale=4)

    def setup(self, case):
        if case['note'] % 12 != 9:
            raise ValueError('The initial panel catalogue uses A notes only')
        self.tap('AMP')
        self.tap('SRC')
        if self.source_page:
            self.tap('SRC')
            self.source_page = 0
        target_octave = (case['note'] - 69) // 12
        while self.octave != target_octave:
            direction = 1 if self.octave < target_octave else -1
            self.tap('UP' if direction > 0 else 'DOWN')
            self.octave += direction
        values = source_controls(case)
        for index, value in enumerate(values[:8]):
            self.set_knob(index + 1, 17 + index, value)
        self.screen(self.folder / 'oscillators.png')
        self.tap('SRC')
        self.source_page = 1
        for encoder, index in ((1, 8), (2, 9), (3, 10), (5, 11), (6, 12), (7, 13), (8, 14)):
            self.set_knob(encoder, 46 + index - 8, values[index], zones=2 if index in (10, 13) else 3)
        self.screen(self.folder / 'mixer.png')
        self.events.append(dict(ms=self.session.ms, mark='setup', sound_slots_8_8=self.sound()))

    def note_on(self, note):
        self.session.press(self.session.code('14'))
        self.wait(.1)

    def note_off(self, note):
        self.session.release(self.session.code('14'))
        self.wait(.1)

    def mark(self, name):
        note = int.from_bytes(self.read('FW_VOICE_NOTE', 4), 'big', signed=True)
        live = struct.unpack('>53h', self.read('FW_VOICE_PARAMS', 106))
        params = list(live[:8] + live[29:36])
        amp = self.read('FW_AMP_ENV', 12)
        frame = len(self.session.pcm) // 4
        self.events.append(dict(ms=self.session.ms, mark=name, frame=frame, voice_note_16_16=note,
                                velocity_8_8=int.from_bytes(self.read('FW_VOICE_VEL', 2), 'big'),
                                voice_params_8_8=params, amp_state=amp.hex()))
        if note != self.case['note'] << 16:
            raise RuntimeError('The requested chromatic key note did not reach the firmware voice')
        if self.read('RENDER_MACHINE', 1) != b'\x45':
            raise RuntimeError('The triggered track is not rendering OB-1')
        for index, value in enumerate(source_controls(self.case)):
            actual = params[index] >> 8
            zones = (2 if index in (10, 13) else 3) if index >= 8 else None
            reached = actual == value if zones is None else actual * zones >> 7 == value * zones >> 7
            if index in (1, 5):
                reached = params[index] == value << 8
            if index in (2, 6):
                reached = (params[index] >= 128) == bool(value)
            if not reached:
                raise RuntimeError(f'Live source control {index + 1} mismatch: requested {value}, actual {actual}')
        # A nonzero release tail is not a held note.
        if int.from_bytes(amp[:4], 'big') != 1 or int.from_bytes(amp[4:8], 'big') == 0:
            raise RuntimeError('The AMP envelope is not holding during sustain')
        return frame

    def capture(self, case, folder):
        self.case, self.folder, self.events = case, folder, []
        self.session.pcm.clear()
        start = len(self.session.inputs)
        try:
            window = play(self, case, self.args)
        finally:
            write_wav(folder / 'audio.wav', self.session.pcm, 2)
            write_json(folder / 'events.json', dict(events=self.events, panel=self.session.inputs[start:],
                                                   final_sound_slots_8_8=self.sound()))
            self.screen(folder / 'final.png')
        return dict(observation='emulated SSI stereo output after stock processing',
                    sustained_window_frames=window,
                    setup_verification='stored controls, live source controls and active AMP after real panel events',
                    note_input='chromatic front-panel key press/release',
                    sound_slots_8_8=self.sound())

    def close(self):
        if self.session:
            self.session.close()
