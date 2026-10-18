# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic measurement and assertion checks; recorded references live separately."""
import copy
import hashlib
import json
import math
import sys
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from regression import cases, wav_info, write_json, write_wav
from regression_assert import BANDS, VERSION, compare, compare_reference, main, measure_run, measurements, periodic, read_json

RATE = 48000


def tone(hz=220, phase=0, gain=.1):
    t = np.arange(RATE) / RATE
    return gain * (np.sin(2*np.pi*hz*t + phase) + .3*np.sin(4*np.pi*hz*t + 2*phase + .4))


def capture(root, samples, width=3, wave_case=True, case=None):
    case = case or (cases()[10] if wave_case else cases()[60])
    folder = root / case['id']
    folder.mkdir(parents=True, exist_ok=True)
    samples = np.asarray(samples)
    if samples.ndim == 1:
        samples = samples[:, None]
    integer = np.rint(samples * 2**(width*8-1)).astype(np.int32).reshape(-1)
    if width == 2:
        pcm = integer.astype('<i2').tobytes()
    else:
        pcm = np.stack([(integer >> shift) & 255 for shift in (0, 8, 16)], axis=1).astype(np.uint8).tobytes()
    write_wav(folder / 'audio.wav', pcm, width, samples.shape[1])
    record = dict(case=case, status='captured', observation='synthetic shared voice boundary',
                  sustained_window_frames=[0, len(samples)], **wav_info(folder / 'audio.wav'))
    run = dict(status='captured', target='dsp', stock_os=None, sustain_seconds=len(samples)/RATE,
               revision='synthetic', firmware_sha256=None, cases=[record], planned_cases=[case['id']])
    write_json(root / 'run.json', run)
    return run


def expectations(actual, cycle=True):
    fixture = copy.deepcopy(actual)
    fixture['role'] = 'expectations'
    fixture['source']['description'] = 'Synthetic two-harmonic signal; validation of checker only'
    for entry in fixture['cases'].values():
        metrics = entry.pop('metrics')
        entry['limits'] = {
            name: dict(min=(np.asarray(metrics[name])-tolerance).tolist(),
                       max=(np.asarray(metrics[name])+tolerance).tolist())
            for name, tolerance in [('rms_dbfs', .05), ('pitch_hz', .01)] if name in metrics}
        if cycle:
            entry['cycle'] = dict(values=metrics['cycle'], max_rmse=.002)
    return fixture


class AssertionTest(unittest.TestCase):
    def test_known_note_fundamental_survives_a_stronger_second_harmonic(self):
        t = np.arange(RATE*2)/RATE
        x = .02*np.sin(2*np.pi*55*t) + .1*np.sin(2*np.pi*110*t+.4)
        hz, cycle = periodic(x, 33)
        self.assertAlmostEqual(hz, 55, places=4)
        self.assertEqual(len(cycle), 256)
        for note in (True, -1, 128, 57.5):
            with self.assertRaisesRegex(ValueError, 'MIDI note'):
                periodic(x, note)
        with self.assertRaisesRegex(ValueError, 'fundamental'):
            periodic(np.sin(2*np.pi*110*t), 33)
        with self.assertRaisesRegex(ValueError, 'fundamental'):
            periodic(np.sin(2*np.pi*58*t), 33)

    def test_measurement_units_and_phase_invariant_signed_cycles(self):
        base = measurements(tone()[:, None], True)
        shifted = measurements(tone(phase=.731)[:, None], True)
        self.assertAlmostEqual(base['pitch_hz'][0], 220, places=4)
        expected_rms = .1 * math.sqrt((1+.3**2)/2)
        self.assertAlmostEqual(base['rms_dbfs'][0], 20*math.log10(expected_rms), places=6)
        self.assertAlmostEqual(base['dc'][0], 0, places=12)
        self.assertLess(np.sqrt(np.mean((np.asarray(base['cycle'])-shifted['cycle'])**2)), .0001)
        with self.assertRaisesRegex(ValueError, 'eight cycles'):
            periodic(tone()[:480])

    def test_gain_pitch_polarity_and_reversal_fail_independent_assertions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            capture(root, tone())
            expected = expectations(measure_run(root))
            for signal, failed in [(tone(gain=.2), 'rms_dbfs'), (tone(hz=221), 'pitch_hz'),
                                   (-tone(), 'cycle_rmse'), (tone()[::-1], 'cycle_rmse')]:
                with self.subTest(property=failed):
                    capture(root, signal)
                    result = compare(measure_run(root), expected)
                    checks = result['cases'][0]['checks']
                    self.assertEqual(result['status'], 'failed')
                    self.assertEqual(next(c['status'] for c in checks if c['property']==failed), 'failed')
            capture(root, tone(phase=1.234))
            self.assertEqual(compare(measure_run(root), expected)['status'], 'passed')

    def test_silence_and_noise_band_power(self):
        silent = measurements(np.zeros((RATE, 2)), False)
        self.assertEqual(silent['rms_dbfs'], [-240, -240])
        self.assertEqual(silent['peak'], [0, 0])
        with self.assertRaisesRegex(ValueError, 'silence'):
            measurements(np.zeros((RATE, 1)), True)
        rng = np.random.default_rng(7)
        noise = rng.normal(0, .01, RATE)
        low = np.fft.rfft(noise)
        low[np.fft.rfftfreq(RATE, 1/RATE) > 1000] = 0
        filtered = np.fft.irfft(low, n=RATE)
        full = measurements(noise[:, None], False)
        narrow = measurements(filtered[:, None], False)
        high_band = BANDS.index((4000, 8000))
        self.assertGreater(full['band_rms_dbfs'][0][high_band]-narrow['band_rms_dbfs'][0][high_band], 60)
        # One-sided PSD integration must recover time-domain mean-square power.
        band_energy = sum(10**(v/10) for v in full['band_rms_dbfs'][0])
        self.assertAlmostEqual(band_energy/np.mean(noise*noise), 1, delta=.02)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            capture(root, noise, wave_case=False)
            actual = measure_run(root)
            expected = expectations(actual, cycle=False)
            record = next(iter(expected['cases'].values()))
            record['limits'] = {'band_rms_dbfs': dict(min=(np.asarray(full['band_rms_dbfs'])-1).tolist())}
            capture(root, filtered, wave_case=False)
            self.assertEqual(compare(measure_run(root), expected)['status'], 'failed')

    def test_pcm_precision_channels_and_window_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stereo = np.column_stack([tone(), tone(gain=.05)])
            for width in (2, 3):
                capture(root, stereo, width)
                actual = next(iter(measure_run(root)['cases'].values()))
                self.assertEqual(actual['channels'], 2)
                levels = actual['metrics']['rms_dbfs']
                self.assertAlmostEqual(levels[0]-levels[1], 6.0206, delta=.002)
            run = capture(root, stereo)
            record = run['cases'][0]
            for window in ([0, RATE-32], [-1, RATE], [0, RATE+1]):
                record['sustained_window_frames'] = window
                write_json(root/'run.json', run)
                with self.assertRaisesRegex(ValueError, 'window'):
                    measure_run(root)
            capture(root, stereo)
            path = root/run['planned_cases'][0]/'audio.wav'
            path.write_bytes(path.read_bytes()[:-6])
            with self.assertRaisesRegex(RuntimeError, 'Incomplete'):
                measure_run(root)
            capture(root, stereo)
            with wave.open(str(path), 'wb') as wav:
                wav.setparams((2, 3, 44100, 0, 'NONE', 'not compressed'))
                wav.writeframes(bytes(RATE*6))
            with self.assertRaisesRegex(RuntimeError, 'wrong-rate'):
                measure_run(root)

    def test_incomplete_runs_and_duplicate_cases_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for change in ('status', 'duplicate', 'planned', 'case_status', 'path', 'duration'):
                run = capture(root, tone())
                if change == 'status': run['status'] = 'acquisition error'
                if change == 'duplicate': run['cases'] *= 2
                if change == 'planned': run['planned_cases'] = []
                if change == 'case_status': run['cases'][0]['status'] = 'running'
                if change == 'path': run['cases'][0]['case']['id'] = '../elsewhere'
                if change == 'duration': run['sustain_seconds'] = False
                write_json(root/'run.json', run)
                with self.subTest(change=change), self.assertRaises(ValueError):
                    measure_run(root)

    def test_wrong_boundary_settings_missing_and_invalid_expectations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            capture(root, tone())
            actual = measure_run(root)
            base = expectations(actual)
            key = next(iter(actual['cases']))
            for field, value in [('format', 999), ('format', True), ('target', 'device'), ('stock_os', '1.54'),
                                 ('role', 'measurements'), ('source', None), ('cases', {})]:
                fixture = copy.deepcopy(base); fixture[field] = value
                with self.subTest(field=field), self.assertRaises(ValueError): compare(actual, fixture)
            for field, value in [('settings', {}), ('observation', 'USB'), ('channels', 2), ('frames', 10),
                                 ('cycles', {}),
                                 ('limits', {}), ('limits', {'typo': {'min': [0]}}),
                                 ('limits', {'rms_dbfs': {'min': [2], 'max': [1]}}),
                                 ('limits', {'rms_dbfs': {'min': [float('nan')]}}),
                                 ('limits', {'rms_dbfs': {'min': [True]}}),
                                 ('limits', {'rms_dbfs': {'min': [0, 0]}}),
                                 ('cycle', {'values': [[0]*256], 'max_rmse': -1})]:
                fixture = copy.deepcopy(base); fixture['cases'][key][field] = value
                with self.subTest(field=field, value=str(value)[:50]), self.assertRaises(ValueError):
                    compare(actual, fixture)
            for raw in ('{"cases":{},"cases":{}}', '{"value":NaN}', '{"value":Infinity}', '{"value":1e999}'):
                path = root/'invalid.json';path.write_text(raw)
                with self.assertRaises(ValueError): read_json(path)

    def test_cli_candidate_is_not_a_baseline_and_failures_keep_reports(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)/'capture'
            capture(root, tone())
            candidate = Path(tmp)/'candidate.json'
            with patch.object(sys, 'argv', ['assert', 'measure', str(root), '--out', str(candidate)]):
                self.assertEqual(main(), 0)
            expected_path = Path(tmp)/'expected.json'
            write_json(expected_path, expectations(read_json(candidate)))
            for label, fixture, code in [('candidate', candidate, 1), ('reviewed', expected_path, 0)]:
                out = Path(tmp)/label
                with patch.object(sys, 'argv', ['assert', 'check', str(root), '--expectations', str(fixture), '--out', str(out)]):
                    self.assertEqual(main(), code)
                report = read_json(out/'assertions.json')
                self.assertEqual(report['status'], 'passed' if code == 0 else 'error')
                self.assertTrue((out/'measurements.json').exists())
            capture(root, tone(gain=.2))
            out = Path(tmp)/'failure'
            with patch.object(sys, 'argv', ['assert', 'check', str(root), '--expectations', str(expected_path), '--out', str(out)]):
                self.assertEqual(main(), 1)
            self.assertEqual(read_json(out/'assertions.json')['status'], 'failed')
            audio = root/next(iter(read_json(candidate)['cases']))/'audio.wav'
            self.assertTrue(audio.exists())
            with patch.object(sys, 'argv', ['assert', 'measure', str(root), '--out', str(candidate)]), \
                    self.assertRaises(FileExistsError):
                main()
            audio.write_bytes(audio.read_bytes()[:-3])
            out = Path(tmp)/'truncated'
            with patch.object(sys, 'argv', ['assert', 'check', str(root), '--expectations', str(expected_path), '--out', str(out)]):
                self.assertEqual(main(), 1)
            self.assertIn('Incomplete', read_json(out/'assertions.json')['error'])
            with patch.object(sys, 'argv', ['assert', 'measure', str(root), '--out', str(candidate)]), \
                    self.assertRaises(RuntimeError):
                main()
            self.assertEqual(read_json(candidate)['role'], 'measurements')


class ReferenceAssertionTest(unittest.TestCase):
    def setUp(self):
        self.profile = read_json(Path(__file__).parent/'reference/recorded/assertions.json')
        self.profile['targets']['dsp'].update(observation='synthetic reference boundary', gain_db=-.7,
                                            calibration='Synthetic known -0.7 dB path')
        self.catalogue = {c['id']: c for c in cases()}
        self.reference = dict(format=1, role='measurements', target='reference', cases={})
        self.actual = dict(format=VERSION, target='dsp', stock_os=None, cases={})
        self.signals = {}
        for key, signal in [('wave-vco1-n57-w63', tone()), ('wave-vco1-n57-w96', tone(phase=.4)),
                            ('mix-6-0-0', tone()), ('mix-3-0-0', tone()*10**(-3/20)),
                            ('mix-0-6-0', tone()), ('mix-6-6-0', tone()*2),
                            ('mod-3-frq2-32', tone())]:
            self.add(key, signal)

    def add(self, key, signal):
        signal = np.tile(signal, 2)
        self.signals[key] = signal
        wave_case = key.startswith('wave-')
        self.reference['cases'][key] = dict(settings=self.catalogue[key], channels=2, frames=96000,
                                           metrics=measurements(np.column_stack([signal, signal]), wave_case, 57))
        self.set_actual(key, signal)

    def set_actual(self, key, signal):
        self.actual['cases'][key] = dict(settings=self.catalogue[key], channels=1, frames=96000,
                                        observation='synthetic reference boundary',
                                        metrics=measurements((signal*10**(-.7/20))[:,None], key.startswith('wave-'), 57))

    def result(self):
        return compare_reference(self.actual, self.profile, self.reference)

    def test_standard_tuning_is_independent_of_reference_detuning(self):
        key = 'wave-vco1-n57-w63'
        self.reference['cases'][key]['metrics']['pitch_hz'] = [223., 223.]
        self.assertEqual(self.result()['status'], 'passed')
        self.set_actual(key, np.tile(tone(hz=223), 2))
        row = next(r for r in self.result()['cases'] if r['id'] == key)
        self.assertEqual(next(c for c in row['checks'] if c['property'] == 'pitch_error_cents')['status'], 'failed')

    def test_reference_gain_pitch_signed_shape_and_modulation_failures(self):
        self.assertEqual(self.result()['status'], 'passed')
        original = copy.deepcopy(self.actual)
        for kind, key, samples, property in [
            ('pitch', 'wave-vco1-n57-w96', np.tile(tone(hz=221),2), 'pitch_error_cents'),
            ('gain', 'wave-vco1-n57-w96', self.signals['wave-vco1-n57-w96']*1.1, 'relative_wave_level_db'),
            ('polarity', 'wave-vco1-n57-w96', -self.signals['wave-vco1-n57-w96'], 'signed_cycle_rmse'),
            ('reversal', 'wave-vco1-n57-w96', self.signals['wave-vco1-n57-w96'][::-1], 'signed_cycle_rmse'),
            ('middle', 'mix-3-0-0', self.signals['mix-3-0-0']*1.03, 'middle_minus_on_db'),
            ('modulation', 'mod-3-frq2-32', np.tile(tone(hz=2200),2), 'modulation_bands_relative_saw_db')]:
            with self.subTest(kind=kind):
                self.actual = copy.deepcopy(original)
                self.set_actual(key, samples)
                result = self.result()
                self.assertEqual(result['status'], 'failed')
                entry = next(r for r in result['cases'] if r['id']==key)
                self.assertEqual(next(c for c in entry['checks'] if c['property']==property)['status'], 'failed')
        self.actual = copy.deepcopy(original)
        self.set_actual('wave-vco1-n57-w96', np.tile(tone(phase=1.31),2))
        self.assertEqual(self.result()['status'], 'passed')
        for key, signal in self.signals.items():
            self.set_actual(key, signal*1.2)
        anchor = next(r for r in self.result()['cases'] if r['id']=='wave-vco1-n57-w63')
        self.assertEqual(next(c for c in anchor['checks'] if c['property']=='absolute_anchor_dbfs')['status'], 'failed')

    def test_mixture_bounds_allow_cancellation_but_detect_excess_gain(self):
        self.add('mix-0-0-0', np.zeros(RATE))
        for ratio in (0, .5, 1, 2):
            self.set_actual('mix-6-6-0', self.signals['mix-6-0-0']*ratio)
            self.assertEqual(self.result()['status'], 'passed')
        self.set_actual('mix-6-6-0', self.signals['mix-6-0-0']*2.5)
        self.assertEqual(self.result()['status'], 'failed')
        self.set_actual('mix-6-6-0', self.signals['mix-6-0-0']*2)
        rng = np.random.default_rng(42)
        noise = rng.normal(0,.01,RATE)
        self.add('mix-0-0-2', noise)
        self.add('mix-0-0-1', noise*10**(-3/20))
        self.add('mix-6-0-2', tone()+noise)
        self.assertEqual(self.result()['status'], 'passed')
        filtered = np.fft.rfft(np.tile(noise,2))
        filtered[np.fft.rfftfreq(RATE*2,1/RATE)>1000] = 0
        self.set_actual('mix-0-0-2', np.fft.irfft(filtered,n=RATE*2))
        self.assertEqual(self.result()['status'], 'failed')
        self.actual['cases'] = {'mix-0-0-0': self.actual['cases']['mix-0-0-0']}
        self.profile['targets']['dsp']['gain_db'] = -20
        self.assertEqual(self.result()['status'], 'passed')

    def test_reference_high_bands_are_diagnostic_but_lower_bands_still_fail(self):
        noise = np.fft.rfft(np.random.default_rng(42).normal(0, .05, RATE))
        noise[np.fft.rfftfreq(RATE, 1/RATE) >= 4000] = 0
        noise = np.fft.irfft(noise, n=RATE)
        self.add('mix-0-0-2', noise)
        self.add('mix-6-0-2', tone() + noise)
        original = copy.deepcopy(self.actual)
        for key in ('mod-3-frq2-32', 'mix-0-0-2', 'mix-6-0-2'):
            for band, hz in ((4, 6000), (5, 12000), (6, 20000)):
                with self.subTest(key=key, hz=hz):
                    self.actual = copy.deepcopy(original)
                    high = .005*np.sin(2*np.pi*hz*np.arange(RATE*2)/RATE)
                    self.set_actual(key, self.signals[key] + high)
                    result = self.result()
                    self.assertEqual(result['status'], 'passed')
                    row = next(r for r in result['cases'] if r['id'] == key)
                    diagnostic = next(c for c in row['checks'] if c['status'] == 'diagnostic')
                    self.assertGreater(diagnostic['actual'][0][band-4], diagnostic['expected']['max'][0][band-4])
                    self.assertEqual(np.shape(self.actual['cases'][key]['metrics']['band_rms_dbfs']), (1, 7))
        self.actual = original
        key = 'mod-3-frq2-32'
        self.set_actual(key, self.signals[key] + .005*np.sin(2*np.pi*3000*np.arange(RATE*2)/RATE))
        row = next(r for r in self.result()['cases'] if r['id'] == key)
        self.assertEqual(row['status'], 'failed')
        self.assertEqual(next(c['status'] for c in row['checks'] if c['property'] == 'modulation_bands_relative_saw_db'), 'failed')

    def test_nonfinite_high_band_diagnostics_are_rejected(self):
        self.actual['cases']['mod-3-frq2-32']['metrics']['band_rms_dbfs'][0][4] = float('nan')
        with self.assertRaisesRegex(ValueError, 'finite'):
            self.result()

    def test_invalid_profiles_boundaries_and_missing_anchors_are_errors(self):
        original = copy.deepcopy(self.profile)
        for field, value in [('format', 1), ('format', True), ('source', None), ('role', 'measurements'), ('tolerances', {})]:
            self.profile = copy.deepcopy(original);self.profile[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):self.result()
        for field, value in [('gain_db', True), ('gain_db', float('nan')), ('calibration', ''), ('channels', 2), ('stock_os', '1.54')]:
            self.profile = copy.deepcopy(original);self.profile['targets']['dsp'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):self.result()
        self.profile = copy.deepcopy(original);self.profile['tolerances']['pitch_cents'] = -1
        with self.assertRaises(ValueError):self.result()
        self.profile = copy.deepcopy(original)
        self.actual['cases'].pop('mix-6-0-0')
        with self.assertRaisesRegex(ValueError, 'dependency'):self.result()

    def test_cli_pins_reference_bytes_and_preserves_failed_comparisons(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            run = root/'run'
            key = 'wave-vco1-n57-w63'
            capture(run, self.signals[key]*10**(-.7/20), case=self.catalogue[key])
            self.profile['targets']['dsp']['observation'] = 'synthetic shared voice boundary'
            ref_path = root/'reference.json';write_json(ref_path,self.reference)
            self.profile['reference'] = dict(path=ref_path.name,sha256=hashlib.sha256(ref_path.read_bytes()).hexdigest())
            profile = root/'profile.json';write_json(profile,self.profile)
            def check(label):
                with patch.object(sys,'argv',['assert','check',str(run),'--expectations',str(profile),'--out',str(root/label)]):return main()
            self.assertEqual(check('pass'),0)
            capture(run, -self.signals[key]*10**(-.7/20), case=self.catalogue[key])
            self.assertEqual(check('polarity-failure'),1)
            report = read_json(root/'polarity-failure/assertions.json')
            self.assertEqual(report['status'],'failed')
            self.assertTrue((root/'polarity-failure/measurements.json').exists())
            key = 'mod-3-frq2-32'
            high = .005*np.sin(2*np.pi*12000*np.arange(RATE*2)/RATE)
            capture(run, (self.signals[key]+high)*10**(-.7/20), case=self.catalogue[key])
            self.assertEqual(check('diagnostic'),0)
            report = read_json(root/'diagnostic/assertions.json')
            diagnostic = next(c for c in report['cases'][0]['checks'] if c['status']=='diagnostic')
            self.assertGreater(diagnostic['actual'][0][1], diagnostic['expected']['max'][0][1])
            measured = read_json(root/'diagnostic/measurements.json')['cases'][key]['metrics']
            self.assertEqual(np.shape(measured['band_rms_dbfs']), (1, 7))
            ref_path.write_text(ref_path.read_text()+'\n')
            self.assertEqual(check('hash-error'),1)
            self.assertIn('SHA-256',read_json(root/'hash-error/assertions.json')['error'])


if __name__ == '__main__':
    unittest.main()
