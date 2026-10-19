# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import json
import sys
import tempfile
import unittest
from contextlib import contextmanager, redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from regression import Device, RATE, wav_info, write_json, write_wav
from regression_scenario import DURATION, INITIAL, capture, compare, load, main, scenario, firmware_start, play_scenario


class ScenarioTest(unittest.TestCase):
    def test_capture_schedule_and_cleanup_keep_failed_recordings(self):
        for failure in (None, 'overflow', 'stalled', 'close'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                clock, messages, closed = [0.0], [], []
                callback = [None]

                def sleep(seconds):
                    clock[0] += seconds
                    if callback[0] is not None and failure != 'stalled':
                        frames = round(seconds * RATE)
                        callback[0](bytes(frames * 6), frames, None, 'overflow' if failure == 'overflow' else '')

                @contextmanager
                def stream(**kwargs):
                    callback[0] = kwargs['callback']
                    try:
                        yield SimpleNamespace(latency=.01)
                    finally:
                        callback[0] = None

                def close():
                    closed.append(True)
                    if failure == 'close':
                        raise OSError('port disconnected')

                def device(args, out):
                    d = object.__new__(Device)
                    d.args, d.audio = args, dict(name='Elektron Digitakt')
                    d.midi = SimpleNamespace(send_message=messages.append, close_port=close)
                    d.sd = SimpleNamespace(RawInputStream=stream)
                    return d

                root = Path(tmp)
                (root / 'firmware.syx').write_bytes(b'test firmware')
                args = SimpleNamespace(firmware=root / 'firmware.syx', out=root / 'run', os='1.54',
                                       channel=1, fx_channel=9, midi_port='Digitakt', audio_device=0)
                with patch('regression_scenario.Device', side_effect=device), \
                        patch('regression.time.sleep', side_effect=sleep), \
                        patch('regression.time.monotonic', side_effect=lambda: clock[0]), redirect_stdout(StringIO()):
                    if failure:
                        with self.assertRaises(RuntimeError):
                            capture(args)
                    else:
                        capture(args)
                report = json.loads((args.out / 'run.json').read_text())
                self.assertEqual(report['status'], 'acquisition error' if failure else 'captured')
                self.assertEqual(closed, [True])
                for note in (45, 57, 64, 81):
                    self.assertIn([0x80, note, 0], messages)
                self.assertEqual(messages[-1], [0xb0, 123, 0])
                self.assertTrue((args.out / 'audio.wav').exists())
                if not failure:
                    self.assertEqual(messages.count([0xb0, 9, 127]), 1)
                    loaded, pcm = load(args.out)
                    origin = report['start_frame']
                    for event, (second, label, _) in zip(loaded['events'], scenario()):
                        self.assertEqual(event['mark'], label)
                        self.assertLessEqual(abs(event['frame'] - origin - second * RATE), 240)
                    self.assertGreaterEqual(len(pcm), origin + DURATION * RATE)
                    with self.assertRaises(FileExistsError):
                        capture(args)

    def test_firmware_clock_sends_only_trigger_and_finds_complete_quiet_onset(self):
        calls = []
        device = SimpleNamespace(setup=lambda c: calls.append(c),
                                 send=lambda m: calls.append(m), note_on=lambda n: calls.append(n),
                                 wait=lambda d: None, mark=lambda m: 12000, input_latency=.01)
        self.assertEqual(play_scenario(device, True), 12000)
        self.assertEqual(calls, [INITIAL | dict(mix1=0, mix2=0, nois=0), [0xb0, 123, 0], 57, [0xb0, 12, 127]])
        pcm = np.zeros((DURATION * RATE + RATE, 2), dtype=np.int64)
        pcm[13000:13010] = 65
        self.assertEqual(firmware_start(pcm, 12000), 13000)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            firmware_start(pcm[:DURATION * RATE], 12000)
        pcm[13000:13010] = 0
        with self.assertRaisesRegex(ValueError, 'No firmware'):
            firmware_start(pcm, 12000)
        pcm[11999] = 65
        with self.assertRaisesRegex(ValueError, 'silent pre-trigger'):
            firmware_start(pcm, 12000)

    def recording(self, folder, pcm):
        folder.mkdir()
        raw = np.stack([((pcm >> shift) & 255).astype(np.uint8) for shift in (0, 8, 16)], axis=2).tobytes()
        write_wav(folder / 'audio.wav', raw, 3)
        events = [dict(mark='scenario start', frame=RATE)]
        events += [dict(mark=label, frame=RATE + second * RATE) for second, label, _ in scenario()]
        write_json(folder / 'events.json', events)
        report = dict(kind='device-scenario', status='captured', scenario=scenario(), initial=INITIAL,
                      duration=DURATION, start_frame=RATE, stock_os='1.54', channel=1, fx_channel=9,
                      midi_port='Digitakt', audio_device=dict(name='Elektron Digitakt'), firmware_sha256='a' * 64,
                      wav_sha256=hashlib.sha256((folder / 'audio.wav').read_bytes()).hexdigest(),
                      events_sha256=hashlib.sha256((folder / 'events.json').read_bytes()).hexdigest(),
                      **wav_info(folder / 'audio.wav'))
        write_json(folder / 'run.json', report)

    def test_single_alignment_preserves_later_phase_and_gain_changes(self):
        with tempfile.TemporaryDirectory() as tmp, redirect_stdout(StringIO()):
            root = Path(tmp)
            pcm = np.random.default_rng(1).integers(-500000, 500000, ((DURATION + 2) * RATE, 2), dtype=np.int64)
            self.recording(root / 'baseline', pcm)
            delayed = np.roll(pcm, 37, axis=0)
            self.recording(root / 'delayed', delayed)
            report = compare(root / 'baseline', root / 'delayed', root / 'same')
            self.assertEqual(report['alignment_samples'], 37)
            self.assertEqual(report['max_abs_lsb'], [0, 0])
            self.assertEqual(report['exact_samples_percent'], [100, 100])
            self.assertEqual(wav_info(root / 'same/difference.wav')['frames'], DURATION * RATE)
            with self.assertRaises(FileExistsError):
                compare(root / 'baseline', root / 'delayed', root / 'same')
            changed = delayed.copy()
            start, end = 8 * RATE + 37, 9 * RATE + 37
            changed[start:end] = np.roll(changed[start:end], 3, axis=0)
            changed[10 * RATE:11 * RATE] *= 2
            self.recording(root / 'changed', changed)
            report = compare(root / 'baseline', root / 'changed', root / 'different')
            self.assertEqual(report['alignment_samples'], 37)
            phase = next(s for s in report['sections'] if s['label'] == 'legato E4')
            self.assertGreater(min(phase['max_abs_lsb']), 1000)
            gain = next(s for s in report['sections'] if s['label'] == 'VCO1 pulse')
            np.testing.assert_allclose(gain['level_change_db'], [6.0206, 6.0206], atol=.01)
            for field, value in (('status', 'acquisition error'), ('stock_os', '1.53'),
                                 ('start_frame', -1), ('wav_sha256', 'bad'), ('events_sha256', 'bad')):
                path = root / 'changed/run.json'
                original = path.read_text()
                metadata = json.loads(original)
                metadata[field] = value
                write_json(path, metadata)
                with self.subTest(field=field), self.assertRaises(ValueError):
                    compare(root / 'baseline', root / 'changed', root / ('invalid-' + field))
                path.write_text(original)
            events_path = root / 'changed/events.json'
            events = json.loads(events_path.read_text())
            write_json(events_path, [dict(midi=[0xb0, 76, 1])] + events)
            path = root / 'changed/run.json'
            metadata = json.loads(path.read_text())
            metadata['events_sha256'] = hashlib.sha256(events_path.read_bytes()).hexdigest()
            write_json(path, metadata)
            with self.assertRaisesRegex(ValueError, 'MIDI preparation differs'):
                compare(root / 'baseline', root / 'changed', root / 'different-setup')

    def test_firmware_capture_validation_and_clock_isolation(self):
        with tempfile.TemporaryDirectory() as tmp, redirect_stdout(StringIO()):
            root = Path(tmp)
            pcm = np.random.default_rng(2).integers(-500000, 500000, ((DURATION + 2) * RATE, 2), dtype=np.int64)
            pcm[:RATE + 17] = 0
            self.recording(root / 'firmware', pcm)
            self.recording(root / 'host', pcm)
            events = [dict(mark='scenario start', frame=RATE), dict(mark='firmware scenario trigger', frame=RATE)]
            path = root / 'firmware'
            write_json(path / 'events.json', events)
            metadata = json.loads((path / 'run.json').read_text())
            metadata.update(clock='firmware-blocks', trigger_frame=RATE, start_frame=RATE+17,
                            events_sha256=hashlib.sha256((path / 'events.json').read_bytes()).hexdigest())
            write_json(path / 'run.json', metadata)
            loaded, _ = load(path)
            self.assertEqual(loaded['start_frame'], RATE+17)
            report = compare(path, path, root / 'self')
            self.assertEqual(report['max_abs_lsb'], [0, 0])
            with self.assertRaisesRegex(ValueError, 'clock mismatch'):
                compare(path, root / 'host', root / 'mixed')
            metadata['start_frame'] += 1
            write_json(path / 'run.json', metadata)
            with self.assertRaisesRegex(ValueError, 'start mismatch'):
                load(path)

    def test_list_does_not_open_device(self):
        with patch.object(sys, 'argv', ['scenario', 'capture', '--list']), \
                patch('regression_scenario.Device', side_effect=AssertionError('must not open device')), \
                redirect_stdout(StringIO()) as output:
            main()
        self.assertEqual(json.loads(output.getvalue())[0][1], 'reset and hold A3')


if __name__ == '__main__':
    unittest.main()
