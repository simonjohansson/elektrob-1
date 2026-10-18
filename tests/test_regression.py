# SPDX-License-Identifier: GPL-2.0-or-later
"""Acquisition plumbing only: no expected sound, spectrum or level."""
import itertools
import io
import json
from contextlib import contextmanager, redirect_stderr, redirect_stdout
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from regression import Device, RATE, SETUP_CC, cases, main, play, select_cases, source_controls, wav_info, write_wav
from regression_firmware import Firmware


class Recorder:
    def __init__(self, fail=False):
        self.events, self.fail = [], fail

    def setup(self, case):
        self.events.append(('setup', case['id']))

    def wait(self, seconds):
        self.events.append(('wait', seconds))
        if self.fail and len(self.events) > 3:
            raise RuntimeError('capture disconnected')

    def note_on(self, note):
        self.events.append(('on', note))

    def note_off(self, note):
        self.events.append(('off', note))

    def mark(self, name):
        self.events.append(('mark', name))


class AcquisitionTest(unittest.TestCase):
    def test_case_matrix_and_isolation(self):
        all_cases = cases()
        self.assertEqual(len(all_cases), len({c['id'] for c in all_cases}))
        mixer = select_cases('mixer', [])
        sources = {(c['mix1'] // 3, c['mix1'] % 3, c['mix2'] // 3, c['mix2'] % 3, c['nois'])
                   for c in mixer}
        self.assertEqual(sources, set(itertools.product(range(3), repeat=5)))
        for c in select_cases('waves', []):
            self.assertEqual((c['mod'], c['nois'], c['mix1'] + c['mix2']), (0, 0, 6))
            self.assertEqual((c['mix1'] % 3, c['mix2'] % 3), (0, 0))

    def test_reference_selection_preserves_all_other_cases(self):
        all_cases = {c['id']: c for c in select_cases('all', [])}
        reference = {c['id']: c for c in select_cases('reference', [])}
        excluded = {'mod-3-frq2-32', 'mod-3-frq2-39', 'mod-3-frq2-44', 'mod-muted-both'}
        self.assertEqual(set(all_cases) - set(reference), excluded)
        self.assertEqual(len(reference), 311)
        for key, case in reference.items():
            self.assertEqual(case, all_cases[key])
        self.assertEqual(select_cases('reference', ['mod-muted-both']), [all_cases['mod-muted-both']])
        argv = ['regression.py', 'firmware', '--suite', 'reference', '--list']
        with patch.object(sys, 'argv', argv), redirect_stdout(io.StringIO()) as output:
            main()
        self.assertEqual(json.loads(output.getvalue()), list(reference.values()))

    def test_musical_settings_reach_firmware_zones(self):
        # Independent OFF / -3 dB / ON control positions for each separate source.
        levels = (21, 64, 106)
        for c in cases():
            values = source_controls(c)
            self.assertEqual(values[:8], [32, 64, c['wav1'] >= 64, c['wav1'] % 64,
                                          c['frq2'], 64, c['wav2'] >= 64, c['wav2'] % 64])
            self.assertEqual(values[8:], [levels[c['mix1'] // 3], levels[c['mix1'] % 3],
                                          127 if c['mod'] in (2, 3) else 0,
                                          levels[c['mix2'] // 3], levels[c['mix2'] % 3],
                                          127 if c['mod'] in (1, 3) else 0, levels[c['nois']]])
            self.assertTrue(all(0 <= value <= 127 for value in values))
        self.assertEqual(dict(SETUP_CC)[76], 0)
        self.assertEqual(dict(SETUP_CC)[79], 127)
        self.assertEqual(dict(SETUP_CC)[80], 0)
        self.assertEqual(dict(SETUP_CC)[81], 0)

    def test_selection_does_not_silently_ignore_typo(self):
        with self.assertRaisesRegex(ValueError, 'Unknown cases'):
            select_cases('smoke', ['not-a-case'])
        self.assertEqual(len(select_cases('smoke', [])), 5)
        self.assertEqual([c['id'] for c in select_cases('all', ['mix-0-0-0'])], ['mix-0-0-0'])

    def test_panel_switch_accepts_a_reachable_value_in_the_requested_zone(self):
        firmware = object.__new__(Firmware)
        firmware.sound = lambda: [10 * 256]
        firmware.session = SimpleNamespace(turn=lambda *a, **k: self.fail('already in OFF zone'))
        firmware.set_knob(1, 0, 21, zones=3)

    def test_setup_precedes_note_and_note_is_released_on_failure(self):
        args = SimpleNamespace(settle=1, seconds=2)
        case = select_cases('smoke', [])[0]
        ok = Recorder()
        play(ok, case, args)
        self.assertEqual(ok.events, [('setup', case['id']), ('wait', .25), ('on', case['note']),
                                     ('wait', 1), ('mark', 'sustain'), ('wait', 2),
                                     ('mark', 'sustain_end'),
                                     ('off', case['note']), ('wait', .25)])
        broken = Recorder(fail=True)
        with self.assertRaisesRegex(RuntimeError, 'disconnected'):
            play(broken, case, args)
        self.assertEqual(broken.events[-1], ('off', case['note']))

    def test_encoder_approaches_target_without_oscillating_across_it(self):
        raw = [57 * 256]
        firmware = object.__new__(Firmware)
        firmware.sound = lambda: raw

        def turn(encoder, detents, after_ms):
            raw[0] = min(127 * 256, max(0, raw[0] + detents * 320))

        firmware.session = SimpleNamespace(turn=turn, halted=None)
        firmware.set_knob(4, 0, 63)
        self.assertEqual(raw[0] >> 8, 63)

    def test_silent_recordings_are_valid_but_truncation_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'silent.wav'
            write_wav(path, bytes(480 * 4), 2)
            self.assertEqual(wav_info(path)['frames'], 480)
            path.write_bytes(path.read_bytes()[:-4])
            with self.assertRaisesRegex(RuntimeError, 'Incomplete'):
                wav_info(path)
            write_wav(path, b'', 2)
            with self.assertRaisesRegex(RuntimeError, 'Incomplete'):
                wav_info(path)

    def test_encoder_waits_for_queued_input_before_accepting_a_zone(self):
        raw, clock, pending = [0], [0], []

        def advance(ms):
            clock[0] += ms
            while pending and pending[0][0] <= clock[0]:
                _, delta = pending.pop(0)
                raw[0] = min(127 << 8, max(0, raw[0] + delta))

        def turn(encoder, detents, after_ms):
            pending.append((clock[0] + 80, detents * 5 * 256))
            advance(after_ms)

        firmware = object.__new__(Firmware)
        firmware.sound = lambda: raw
        firmware.session = SimpleNamespace(turn=turn, halted=None)
        firmware.set_knob(6, 0, 64, zones=3)
        advance(100)
        self.assertEqual((raw[0] >> 8) * 3 >> 7, 1)
        self.assertEqual(pending, [])

    def test_runner_rejects_short_windows_and_retains_failure_evidence(self):
        for frames, window, fails in ((1, [0, 1], True),
                                      (48000, [48000, 48000], True),
                                      (480, [0, 481], True),
                                      (480, [0, 480], False)):
            with self.subTest(frames=frames, window=window), tempfile.TemporaryDirectory() as tmp:
                closed = []

                class Capture:
                    def __init__(self, args, out):
                        pass

                    def capture(self, case, folder):
                        write_wav(folder / 'audio.wav', bytes(frames * 6), 3)
                        return dict(sustained_window_frames=window)

                    def close(self):
                        closed.append(True)

                out = Path(tmp) / 'capture'
                argv = ['regression.py', 'dsp', '--seconds', '.01', '--settle', '0',
                        '--case', 'mix-0-0-0', '--out', str(out)]
                with patch('regression.DSP', Capture), patch.object(sys, 'argv', argv):
                    if fails:
                        with self.assertRaisesRegex(RuntimeError, 'Incomplete sustained'):
                            main()
                    else:
                        main()
                report = json.loads((out / 'run.json').read_text())
                expected = 'acquisition error' if fails else 'captured'
                self.assertEqual(report['status'], expected)
                self.assertEqual(report['cases'][0]['status'], expected)
                self.assertEqual(json.loads((out / 'mix-0-0-0/capture.json').read_text())['status'], expected)
                self.assertTrue((out / 'mix-0-0-0/audio.wav').exists())
                self.assertEqual(closed, [True])

    def test_firmware_requires_live_source_controls_and_active_amp(self):
        case = select_cases('smoke', [])[0]
        good = [value << 8 for value in source_controls(case)]
        for stale, phase, amp in ((None, 1, 1), (1, 1, 1), (3, 1, 1), (8, 1, 1),
                                  (None, 1, 0), (None, 0, 1)):
            with self.subTest(stale=stale, phase=phase, amp=amp):
                params = good.copy()
                if stale is not None:
                    params[stale] = params[stale] + 1 if stale == 1 else 0
                live = params[:8] + [0] * 21 + params[8:] + [0] * 17
                values = dict(FW_VOICE_NOTE=(case['note'] << 16).to_bytes(4, 'big'),
                              FW_VOICE_VEL=(100 << 8).to_bytes(2, 'big'),
                              FW_VOICE_PARAMS=struct.pack('>53h', *live),
                              FW_AMP_ENV=struct.pack('>3I', phase, amp, 0), RENDER_MACHINE=b'\x45')
                firmware = object.__new__(Firmware)
                firmware.case, firmware.events = case, []
                firmware.session = SimpleNamespace(ms=1000, pcm=bytes(40))
                firmware.read = lambda symbol, count: values[symbol]
                if stale is not None or phase != 1 or not amp:
                    with self.assertRaisesRegex(RuntimeError, 'Live source control|AMP envelope'):
                        firmware.mark('sustain')
                else:
                    self.assertEqual(firmware.mark('sustain'), 10)
                self.assertEqual(firmware.events[-1]['voice_params_8_8'], params)

    def test_device_requires_distinct_track_and_fx_channels(self):
        error_text = io.StringIO()
        argv = ['regression.py', 'device', '--firmware', 'unused.syx', '--os', '1.54',
                '--midi-port', 'Digitakt', '--audio-device', '0', '--fx-channel', '1']
        with patch.object(sys, 'argv', argv), redirect_stderr(error_text), \
                patch('regression.Device', side_effect=AssertionError('must not open a device')):
            with self.assertRaises(SystemExit) as error:
                main()
            self.assertEqual(error.exception.code, 2)
        self.assertIn('separate track 1 and FX receive channels', error_text.getvalue())

    def test_usb_wait_counts_samples_and_times_out_on_a_stalled_stream(self):
        device = object.__new__(Device)
        device.pcm = bytearray()
        chunks = iter((0, 0, 512))
        with patch('regression.time.monotonic', return_value=0), \
                patch('regression.time.sleep', side_effect=lambda _: device.pcm.extend(bytes(next(chunks) * 6))) as sleep:
            device.wait(.01)
        self.assertEqual(sleep.call_count, 3)
        self.assertEqual(len(device.pcm) // 6, 512)
        with patch('regression.time.monotonic', side_effect=(0, 3)), \
                patch('regression.time.sleep') as sleep:
            with self.assertRaisesRegex(RuntimeError, 'USB audio input stalled'):
                device.wait(.01)
            sleep.assert_not_called()

    def test_usb_capture_routes_controls_and_retains_recording_on_overflow(self):
        for overflow in (False, True):
            with self.subTest(overflow=overflow), tempfile.TemporaryDirectory() as tmp:
                messages = []

                @contextmanager
                def stream(**kwargs):
                    with patch('regression.time.sleep', side_effect=lambda seconds: kwargs['callback'](
                            bytes(round(seconds * RATE) * 6), round(seconds * RATE), None,
                            'overflow' if overflow else '')):
                        yield SimpleNamespace(latency=.01)

                device = object.__new__(Device)
                device.args = SimpleNamespace(channel=1, fx_channel=9, midi_port='Digitakt',
                                              audio_device=4, settle=0, seconds=.01)
                device.audio = {'name': 'Digitakt'}
                device.midi = SimpleNamespace(send_message=messages.append, close_port=lambda: None)
                device.sd = SimpleNamespace(RawInputStream=stream)
                try:
                    if overflow:
                        with self.assertRaisesRegex(RuntimeError, 'overflow'):
                            device.capture(cases()[0], Path(tmp))
                    else:
                        result = device.capture(cases()[0], Path(tmp))
                        start, end = result['sustained_window_frames']
                        self.assertEqual(end - start, 480)
                finally:
                    device.close()
                self.assertGreater(wav_info(Path(tmp) / 'audio.wav')['frames'], 480)
                self.assertIn([0xB8, 99, 2], messages)
                self.assertIn([0xB0, 76, 0], messages)
                self.assertIn([0xB0, 24, 106], messages)
                self.assertIn([0xB0, 25, 21], messages)
                self.assertIn([0xB0, 26, 0], messages)
                self.assertIn([0xB0, 30, 21], messages)
                self.assertLess(messages.index([0x90, 33, 100]), messages.index([0x80, 33, 0]))
                self.assertEqual(messages[-1], [0xB0, 123, 0])


if __name__ == '__main__':
    unittest.main()
