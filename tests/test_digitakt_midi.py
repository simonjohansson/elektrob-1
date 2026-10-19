# SPDX-License-Identifier: GPL-2.0-or-later
"""Compiled-firmware MIDI integration, using the emulator's DIN UART input."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from regression import Device, source_cc, select_cases, write_json
from regression_firmware import Firmware


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--firmware', type=Path, required=True)
    ap.add_argument('--os', choices=('1.53', '1.54'), required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(stock_os=args.os, firmware_sha256=hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
                  transport='emulated DIN UART; physical USB unverified', checks=[], status='running')
    firmware = None
    try:
        firmware = Firmware(args, args.out)
        from emu.midi import MidiIn
        from emu.harness import call
        from unicorn.m68k_const import UC_M68K_REG_PC, UC_M68K_REG_D2, UC_M68K_REG_A2

        session, midi = firmware.session, MidiIn()

        # Observe initialization through actual DIN input without writing guest voice state.
        from unicorn import UC_HOOK_CODE
        from unicorn.m68k_const import UC_M68K_REG_A7
        symbols = json.loads(Path(str(args.firmware) + '.map.json').read_text())
        render = int(symbols['ob1:ob1_voice_render'], 0)
        initial = struct.pack('>18I', *([0, 0, 0, 0, 0, 32767, 32767] * 2 + [0, 1, 0, 0]))
        starts = []
        address = int(symbols['ob1:scenario'], 0) if 'ob1:scenario' in symbols else None
        steps = {}
        wanted = {2, 4502, 6002, 7502, 9002, 10502, 12002, 13502, 15002,
                  16502, 18002, 19502, 21002, 22502, 24002, 25502, 30002}

        def observe(uc, _address, _size, _data):
            if address is not None:
                raw = bytes(uc.mem_read(address, 21))
                block = int.from_bytes(raw[:4], 'big')
                if block in wanted:
                    steps[block] = dict(note=raw[4], gate=raw[5], controls=list(raw[6:21]))
            stack = uc.reg_read(UC_M68K_REG_A7)
            pointer = int.from_bytes(uc.mem_read(stack + 4, 4), 'big')
            if bytes(uc.mem_read(pointer, len(initial))) == initial:
                starts.append(session.ms)

        hook = session.m.uc.hook_add(UC_HOOK_CODE, observe, begin=render, end=render)

        # Fresh project: enable FX reception on channel 9 through the native menu.
        firmware.tap('GLOBAL')
        for _ in range(4):
            firmware.tap('DOWN')
        firmware.tap('YES')
        for _ in range(2):
            firmware.tap('DOWN')
        firmware.tap('YES')
        for _ in range(16):
            firmware.tap('DOWN')
        for _ in range(9):
            firmware.tap('RIGHT')
        firmware.screen(args.out / 'fx-channel.png')
        for _ in range(3):
            firmware.tap('NO')

        def send(data):
            midi.put(data)
            for _ in range(100):
                if midi.delivered == midi.received:
                    break
                if midi.deliver(session.m):
                    session.pc = session.m.uc.reg_read(UC_M68K_REG_PC)
                firmware.wait(.005)
            else:
                raise RuntimeError('Firmware did not accept queued MIDI bytes')
            firmware.wait(.1)

        def live():
            values = struct.unpack('>53h', firmware.read('FW_VOICE_PARAMS', 106))
            return list(values[:8] + values[29:36])

        defaults = [value << 8 for value in (32, 64, 0, 63, 32, 67, 0, 63, 106, 0, 0, 64, 0, 0, 0)]
        kit = int.from_bytes(firmware.read('FW_KIT', 4), 'big')
        other_sound = bytes(session.m.uc.mem_read(kit + 0x20 + 162, 162))
        sound = firmware.sound()
        assert sound[17:25] + sound[46:53] == defaults, ('initial', sound)

        # Exercise the trampoline's ABI and delegation separately from MIDI routing.
        for track, machine, cc, expected in ((0, 69, 24, 164), (7, 69, 30, 170),
                                             (0, 69, 16, 132), (0, 69, 9, 0), (0, 3, 9, 0), (0, 3, 24, 0),
                                             (8, 69, 24, 0), (16, 0, 24, 78), (16, 0, 30, 85)):
            uc = session.m.uc
            context = uc.context_save()
            try:
                uc.reg_write(UC_M68K_REG_D2, 0x12345678)
                uc.reg_write(UC_M68K_REG_A2, 0x23456788)
                # A direct call must not inherit the exhausted session block budget.
                session.m._fast_stepper_obj.left = 100000
                actual = call(session.m, 0x4007919e, [track, machine, cc])
                assert uc.reg_read(UC_M68K_REG_PC) == 0xDEADBEE0
                assert actual == expected, (track, machine, cc, actual, expected)
                assert uc.reg_read(UC_M68K_REG_D2) == 0x12345678
                assert uc.reg_read(UC_M68K_REG_A2) == 0x23456788
            finally:
                uc.context_restore(context)
        report['checks'].append('native lookup: OB-1, stock/MIDI fallback and callee-saved registers')

        # Exercise the cold machine before note/reselection warmup can hide an initialization fault.
        for value in (0, 1, 42, 43, 63, 64, 85, 86, 127):
            send([byte for cc in range(16, 31) for byte in (0xb0, cc, value)])
            expected = [min(value, limit) << 8 for limit in (63, 127, 1, 63, 63, 127, 1, 63, *([127] * 7))]
            sound = firmware.sound()
            assert sound[17:25] + sound[46:53] == expected, (value, sound)
            send([0x90, 57, 100])
            firmware.wait(1)
            assert live() == expected, (value, live(), expected)
            assert int.from_bytes(firmware.read('FW_VOICE_NOTE', 4), 'big') == 57 << 16
            assert int.from_bytes(firmware.read('FW_AMP_ENV', 12)[4:8], 'big') > 0
            send([0x80, 57, 0])
        report['checks'].append('all fifteen stored/live source values: endpoints, zones and range clamps')

        assert firmware.sound()[17:25] + firmware.sound()[46:53] != defaults
        for direction, machine in (('UP', 0), ('DOWN', 69)):
            firmware.chord('FUNC', 'SRC')
            for _ in range(4):
                firmware.tap(direction)
            firmware.tap('YES')
            assert bytes(session.m.uc.mem_read(kit + 0x20 + 126, 1)) == bytes([machine])
        assert bytes(session.m.uc.mem_read(kit + 0x20 + 162, 162)) == other_sound
        sound = firmware.sound()
        assert sound[17:25] + sound[46:53] == defaults, ('switched back', sound)
        send([0x90, 57, 100])
        firmware.wait(1)
        assert live() == defaults, ('switched back', live())
        send([0x80, 57, 0])
        report['checks'].append('machine-pages supplies defaults on first selection and stored/live FIN2 detune after reselection')

        before = firmware.sound()
        send([0xb1, 24, 64])
        assert firmware.sound() == before
        send([0xb1, 7, 118])
        kit = int.from_bytes(firmware.read('FW_KIT', 4), 'big')
        track2 = struct.unpack('>53h', session.m.uc.mem_read(kit + 0x20 + 162 + 20, 106))
        assert track2[45] == 118 << 8
        assert firmware.sound() == before
        report['checks'].append('channel isolation and stock track AMP CC fallback')

        send([byte for cc, value in ((99, 2), (98, 23), (6, 64), (38, 0), (99, 127), (98, 127))
              for byte in (0xb8, cc, value)])
        assert int.from_bytes(session.m.uc.mem_read(kit + 0x580, 2), 'big') == 64 << 8
        adapter = object.__new__(Device)
        adapter.args = SimpleNamespace(fx_channel=9)
        adapter.send = lambda message, channel=1: send([message[0] | (channel - 1), *message[1:]])
        Device.setup(adapter, select_cases('smoke', [])[0])
        assert int.from_bytes(session.m.uc.mem_read(kit + 0x580, 2), 'big') == 0
        firmware.require_slots({4: 0, 8: 64, 12: 0, 16: 64, 25: 0, 27: 0, 28: 64,
                                34: 127, 35: 0, 36: 0, 38: 0, 39: 127, 40: 0,
                                41: 0, 42: 0, 43: 0, 44: 64, 45: 127})
        for case in select_cases('smoke', []):
            send([byte for cc, value in source_cc(case) for byte in (0xb0, cc, value)])
            send([0x90, case['note'], 100])
            firmware.wait(1)
            firmware.case = case
            firmware.mark('midi setup')
            send([0x80, case['note'], 0])
        firmware.wait(1)
        # Native release has an exponential tail; check the gate state, not exact silence.
        assert int.from_bytes(firmware.read('FW_AMP_ENV', 12)[:4], 'big') == 0
        send([0x90, 57, 100])
        firmware.wait(1)
        before_sound = firmware.sound()
        try:
            for value in (0, 127, 64):
                count = len(starts)
                send([0xb1, 9, value])
                assert len(starts) == count, 'Other MIDI channel reset track 1'
                send([0xb0, 9, value])
                assert len(starts) == count + 1, ('CC reset count', starts)
                assert firmware.sound() == before_sound, 'Reset changed saved controls'
        finally:
            send([0x80, 57, 0])
        report['checks'].append('CC 9 values 0/127/64: full initialized voice at render entry, channel isolation and unchanged saved controls')
        report['reset_render_starts_ms'] = starts
        if address is not None:
            # The diagnostic sequencer advances in audio blocks without changing native note/controls.
            send([0x90, 57, 100])
            before_sound = firmware.sound()
            before_live = live()
            steps.clear()
            try:
                send([0xb1, 12, 127])
                assert not steps, 'Other MIDI channel triggered track 1 scenario'
                send([0xb0, 12, 127])
                active_block = int.from_bytes(session.m.uc.mem_read(address, 4), 'big')
                assert 1 < active_block < 1000, ('CC 12 did not start scenario', active_block)
                firmware.wait(23.2)
                assert set(steps) == wanted, ('missing scenario boundaries', steps)
                assert steps[2]['note'] == 57 and steps[10502]['note'] == 64
                assert steps[25502]['note'] == 45 and steps[30002]['note'] == 81
                assert steps[4502]['controls'][9] == 106 and steps[6002]['controls'][12] == 106
                assert steps[18002]['controls'][13] == 127 and steps[19502]['controls'][10] == 127
                assert steps[24002]['controls'][4] == 32
                assert int.from_bytes(session.m.uc.mem_read(address, 4), 'big') == 0
                assert firmware.sound() == before_sound and live() == before_live
                assert int.from_bytes(firmware.read('FW_VOICE_NOTE', 4), 'big') == 57 << 16
            finally:
                send([0xb0, 9, 0])
                send([0x80, 57, 0])
            report['firmware_scenario_steps'] = steps
            report['checks'].append('CC 12: channel isolation, exact DSP block boundaries, notes/control changes and completion without native note/control writes')
        session.m.uc.hook_del(hook)
        before = session.frames[-1][1]
        firmware.tap('GLOBAL')
        assert session.frames[-1][1] != before, 'Panel stopped responding after MIDI reception'
        firmware.screen(args.out / 'panel-after-midi.png')
        report['checks'].append('regression MIDI setup, smoke live values and note-on/off')
        report.update(status='passed', midi_bytes_delivered=midi.delivered, observations=firmware.events)
    except BaseException as exc:
        report.update(status='failed', error=str(exc) or type(exc).__name__)
        raise
    finally:
        write_json(args.out / 'midi.json', report)
        if firmware:
            firmware.close()
    print(f'MIDI integration passed: {args.out / "midi.json"}')


if __name__ == '__main__':
    main()
