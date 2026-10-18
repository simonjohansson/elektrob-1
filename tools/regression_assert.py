# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure retained captures and check reviewed JSON expectations; no reference WAVs."""
import argparse
import hashlib
import json
import math
import sys
import wave
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from regression import RATE, ROOT, wav_info, write_json

VERSION = 2
CYCLE_POINTS = 256
BANDS = ((20, 80), (80, 200), (200, 1000), (1000, 4000),
         (4000, 8000), (8000, 16000), (16000, 24000))


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f'Duplicate JSON key: {key}')
            result[key] = value
        return result

    def invalid(value):
        raise ValueError(f'Non-finite JSON number: {value}')

    def finite_float(value):
        number = float(value)
        return number if math.isfinite(number) else invalid(value)

    return json.loads(path.read_text(), object_pairs_hook=unique, parse_constant=invalid, parse_float=finite_float)


def numbers(value, shape=None):
    array = np.asarray(value)
    if array.dtype.kind not in 'ifu' or not np.all(np.isfinite(array)):
        raise ValueError('Expected finite numeric values')
    if shape is not None and array.shape != shape:
        raise ValueError(f'Expected dimensions {shape}, got {array.shape}')
    return array.astype(float)


def db(amplitude):
    # A finite floor represents digital zero without nonstandard JSON infinities.
    return 20 * np.log10(np.maximum(amplitude, 1e-12))


def periodic(x, note=None):
    window = np.hanning(len(x) + 1)[:-1]
    signal = (x - x.mean()) * window
    nfft = 1 << (8 * len(x) - 1).bit_length()
    spectrum = abs(np.fft.rfft(signal, nfft))
    low, high = math.ceil(20 * nfft / RATE), math.floor(12000 * nfft / RATE)
    if note is not None:
        if type(note) is not int or not 0 <= note <= 127:
            raise ValueError('Invalid isolated-wave MIDI note')
        expected = 440 * 2 ** ((note - 69) / 12)
        low, high = math.ceil(expected * .98 * nfft / RATE), math.floor(expected * 1.02 * nfft / RATE)
    peak = low + int(np.argmax(spectrum[low:high + 1]))
    if spectrum[peak] == 0:
        raise ValueError('Cannot measure a periodic cycle from silence')
    if note is not None and (peak in (low, high) or spectrum[peak] < spectrum.max() * .1):
        raise ValueError('No measurable fundamental near the played note')
    a, b, c = np.log(np.maximum(spectrum[peak - 1:peak + 2], 1e-30))
    offset = .5 * (a - c) / (a - 2 * b + c)
    hz = (peak + offset) * RATE / nfft
    if len(x) * hz / RATE < 8:
        raise ValueError('Periodic measurements require at least eight cycles')
    harmonics = np.arange(1, min(64, int(.475 * RATE / hz)) + 1)
    step = np.exp(-2j * np.pi * hz * np.arange(len(x)) / RATE)
    basis = step.copy()
    coefficients = []
    for _ in harmonics:
        coefficients.append(2 * np.sum(signal * basis) / window.sum())
        basis *= step
    coefficients = np.asarray(coefficients)
    # Fix the time origin by the fundamental; never select a polarity or reverse time.
    coefficients *= np.exp(-1j * harmonics * np.angle(coefficients[0]))
    phase = np.arange(CYCLE_POINTS) / CYCLE_POINTS
    cycle = np.real(np.exp(2j * np.pi * phase[:, None] * harmonics) @ coefficients)
    norm = np.sqrt(np.mean(cycle * cycle))
    if not math.isfinite(norm) or norm == 0:
        raise ValueError('No measurable periodic cycle')
    return float(hz), (cycle / norm).round(8).tolist()


def measurements(pcm, is_wave, note=None):
    result = dict(rms_dbfs=db(np.sqrt(np.mean(pcm * pcm, axis=0))).tolist(),
                  peak=np.max(abs(pcm), axis=0).tolist(), dc=pcm.mean(axis=0).tolist())
    size = min(8192, len(pcm))
    window = np.hanning(size + 1)[:-1]
    frequencies = np.fft.rfftfreq(size, 1 / RATE)
    bands, pitch, cycles = [], [], []
    for channel in pcm.T:
        blocks = np.stack([channel[start:start + size]
                           for start in range(0, len(channel) - size + 1, size // 2)])
        power = np.mean(abs(np.fft.rfft(blocks * window, axis=1)) ** 2, axis=0) / (RATE * np.sum(window ** 2))
        power[1:-1 if size % 2 == 0 else None] *= 2
        bands.append(db(np.sqrt([power[(frequencies >= low) & (frequencies < high)].sum() * RATE / size
                                 for low, high in BANDS])).tolist())
        if is_wave:
            hz, cycle = periodic(channel, note)
            pitch.append(hz)
            cycles.append(cycle)
    result['band_rms_dbfs'] = bands
    if is_wave:
        result.update(pitch_hz=pitch, cycle=cycles)
    return result


def measure_run(folder):
    run_path = folder / 'run.json'
    run = read_json(run_path)
    if run['status'] != 'captured' or run['target'] not in ('dsp', 'firmware', 'device'):
        raise ValueError('A complete captured run is required')
    duration = run['sustain_seconds']
    if isinstance(duration, bool) or not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration < .01:
        raise ValueError('Invalid sustained duration')
    frames = math.floor(duration * RATE / 32) * 32
    records = {}
    for record in run['cases']:
        case = record['case']
        key = case['id']
        if not isinstance(key, str) or not key or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in key):
            raise ValueError('Invalid case ID')
        if key in records or record['status'] != 'captured':
            raise ValueError(f'Duplicate or incomplete case: {key}')
        path = folder / key / 'audio.wav'
        info = wav_info(path)
        if any(record[k] != info[k] for k in info) or info['channels'] not in (1, 2) or info['sample_bytes'] not in (2, 3):
            raise ValueError(f'PCM metadata mismatch: {key}')
        start, end = record['sustained_window_frames']
        if type(start) is not int or type(end) is not int or not 0 <= start < end <= info['frames'] or end - start < frames:
            raise ValueError(f'Incomplete sustained window: {key}')
        if not isinstance(record['observation'], str) or not record['observation']:
            raise ValueError(f'Missing observation point: {key}')
        with wave.open(str(path), 'rb') as wav:
            wav.setpos(start)
            raw = wav.readframes(frames)
        if info['sample_bytes'] == 2:
            pcm = np.frombuffer(raw, dtype='<i2').astype(float) / 32768
        else:
            b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
            value = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
            pcm = ((value ^ 8388608) - 8388608) / 8388608
        pcm = pcm.reshape(frames, info['channels'])
        try:
            metrics = measurements(pcm, key.startswith('wave-'), case['note'])
        except ValueError as exc:
            raise ValueError(f'{key}: {exc}') from exc
        records[key] = dict(settings=case, observation=record['observation'], channels=info['channels'],
                            frames=frames, wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                            metrics=metrics)
    planned = run['planned_cases']
    if not records or len(planned) != len(records) or set(planned) != set(records):
        raise ValueError('Planned and captured cases differ')
    return dict(format=VERSION, role='measurements', target=run['target'], stock_os=run['stock_os'],
                source=dict(run_sha256=hashlib.sha256(run_path.read_bytes()).hexdigest(),
                            revision=run['revision'], firmware_sha256=run['firmware_sha256'], numpy=np.__version__),
                cases=records)


def compare(actual, expected):
    if not isinstance(expected, dict) or expected.get('role') != 'expectations' or not isinstance(expected.get('source'), dict):
        raise ValueError('Reviewed expectations require role=expectations and source.description')
    if not isinstance(expected['source'].get('description'), str) or not expected['source']['description'].strip():
        raise ValueError('Reviewed expectations require role=expectations and source.description')
    if type(expected.get('format')) is not int:
        raise ValueError('Expectation format must be an integer version')
    for key in ('format', 'target', 'stock_os'):
        if expected[key] != actual[key]:
            raise ValueError(f'Expectation {key} mismatch')
    results = []
    for key, observed in actual['cases'].items():
        if key not in expected['cases']:
            raise ValueError(f'Missing expectations: {key}')
        reference = expected['cases'][key]
        if not isinstance(reference, dict):
            raise ValueError(f'{key}: invalid expectations')
        if set(reference) - {'settings', 'observation', 'channels', 'frames', 'wav_sha256', 'limits', 'cycle'}:
            raise ValueError(f'{key}: unknown expectation fields')
        for field in ('settings', 'observation', 'channels', 'frames'):
            if reference[field] != observed[field]:
                raise ValueError(f'{key}: {field} mismatch')
        limits = reference.get('limits', {})
        if not isinstance(limits, dict):
            raise ValueError(f'{key}: invalid limits')
        if not limits and 'cycle' not in reference:
            raise ValueError(f'{key}: no assertions')
        checks = []
        for name, bounds in limits.items():
            if name == 'cycle' or name not in observed['metrics']:
                raise ValueError(f'{key}: unknown metric {name}')
            value = numbers(observed['metrics'][name])
            if not isinstance(bounds, dict) or not bounds or set(bounds) - {'min', 'max'}:
                raise ValueError(f'{key}: invalid bounds for {name}')
            lower = numbers(bounds['min'], value.shape) if 'min' in bounds else None
            upper = numbers(bounds['max'], value.shape) if 'max' in bounds else None
            if lower is not None and upper is not None and np.any(lower > upper):
                raise ValueError(f'{key}: reversed bounds for {name}')
            passed = (lower is None or np.all(value >= lower)) and (upper is None or np.all(value <= upper))
            checks.append(dict(property=name, status='passed' if passed else 'failed', actual=value.tolist(), expected=bounds))
        if 'cycle' in reference:
            if not {'pitch_hz', 'rms_dbfs'} <= set(limits) or 'cycle' not in observed['metrics']:
                raise ValueError(f'{key}: cycle assertions also require pitch_hz and rms_dbfs limits')
            cycle = reference['cycle']
            if set(cycle) != {'values', 'max_rmse'}:
                raise ValueError(f'{key}: invalid cycle expectation')
            values = numbers(cycle['values'], (observed['channels'], CYCLE_POINTS))
            threshold = numbers(cycle['max_rmse'], ())
            if threshold < 0:
                raise ValueError(f'{key}: negative cycle tolerance')
            error = np.sqrt(np.mean((numbers(observed['metrics']['cycle'], values.shape) - values) ** 2, axis=1))
            checks.append(dict(property='cycle_rmse', status='passed' if np.all(error <= threshold) else 'failed',
                               actual=error.tolist(), expected_max=float(threshold)))
        results.append(dict(id=key, status='passed' if all(c['status'] == 'passed' for c in checks) else 'failed', checks=checks))
    return dict(status='passed' if all(c['status'] == 'passed' for c in results) else 'failed', cases=results)


def compare_reference(actual, expected, reference):
    if set(expected) != {'format', 'role', 'source', 'reference', 'targets', 'tolerances'}:
        raise ValueError('Invalid reference assertion fields')
    if expected['role'] != 'expectations' or not isinstance(expected['source'], dict) or not isinstance(expected['source'].get('description'), str) or not expected['source']['description'].strip():
        raise ValueError('Reference assertions require expectation provenance')
    if type(expected['format']) is not int or expected['format'] != VERSION or actual['format'] != VERSION:
        raise ValueError('Reference assertion measurement version mismatch')
    if type(reference['format']) is not int or reference['format'] != 1 or reference['role'] != 'measurements' or reference['target'] != 'reference':
        raise ValueError('Expected the recorded numeric reference dataset')
    if not actual['cases'] or actual['target'] not in expected['targets']:
        raise ValueError('Missing reference cases or target calibration')
    target = expected['targets'][actual['target']]
    if set(target) != {'stock_os', 'channels', 'observation', 'gain_db', 'calibration'}:
        raise ValueError('Invalid target calibration fields')
    if target['stock_os'] != actual['stock_os'] or type(target['channels']) is not int or target['channels'] not in (1, 2):
        raise ValueError('Reference assertion target/OS mismatch')
    if not isinstance(target['calibration'], str) or not target['calibration'].strip():
        raise ValueError('Missing fixed-gain calibration provenance')
    gain = float(numbers(target['gain_db'], ()))
    limits = expected['tolerances']
    wanted = {'anchor_db', 'pitch_cents', 'wave_level_db', 'cycle_rmse', 'source_level_db',
              'middle_db', 'noise_db', 'spectrum_db', 'modulation_db', 'mixture_db',
              'silence_margin_db', 'band_floor_db'}
    if set(limits) != wanted:
        raise ValueError('Invalid reference tolerance fields')
    for name, value in limits.items():
        value = float(numbers(value, ()))
        if (name == 'band_floor_db' and value >= 0) or (name != 'band_floor_db' and value < 0):
            raise ValueError(f'Invalid reference tolerance: {name}')
    channels = target['channels']

    def metric(entry, name):
        shape = (entry['channels'], CYCLE_POINTS) if name == 'cycle' else (entry['channels'], 7) if name == 'band_rms_dbfs' else (entry['channels'],)
        return numbers(entry['metrics'][name], shape)[:channels]

    def ref(key, name):
        entry = reference['cases'][key]
        if entry['channels'] != 2 or entry['frames'] != 96000:
            raise ValueError(f'{key}: incompatible reference recording')
        return metric(entry, name)

    def observed(key, name):
        if key not in actual['cases']:
            raise ValueError(f'Include reference assertion dependency in capture: {key}')
        return metric(actual['cases'][key], name)

    anchor = 'wave-vco1-n57-w63'
    reference_anchor = ref(anchor, 'rms_dbfs')
    floor = limits['band_floor_db']

    def band_level(entry):
        bands = metric(entry, 'band_rms_dbfs')[:, 2:4]
        return 10 * np.log10(np.sum(10 ** (bands / 10), axis=1))

    results = []
    for key, entry in actual['cases'].items():
        baseline = reference['cases'][key]
        if entry['settings'] != baseline['settings'] or entry['channels'] != channels or entry['frames'] != 96000 or entry['observation'] != target['observation']:
            raise ValueError(f'{key}: reference settings, boundary or window mismatch')
        case = entry['settings']
        checks = []

        def bound(name, value, lower, upper, diagnostic=False):
            value = numbers(value)
            lower, upper = np.broadcast_to(lower, value.shape), np.broadcast_to(upper, value.shape)
            if not np.isfinite(lower).all() or not np.isfinite(upper).all() or np.any(lower > upper):
                raise ValueError(f'{key}: invalid bounds for {name}')
            status = 'diagnostic' if diagnostic else 'passed' if np.all((value >= lower) & (value <= upper)) else 'failed'
            checks.append(dict(property=name, status=status,
                               actual=value.tolist(), expected=dict(min=lower.tolist(), max=upper.tolist())))

        def near(name, value, center, tolerance):
            bound(name, value, center - tolerance, center + tolerance)

        def band_bounds(name, value, lower, upper):
            # The reference's filter and resampling color bands from 4 kHz upward.
            bound(name, value[:, :4], lower[:, :4], upper[:, :4])
            bound(name + '_4000_24000_hz_diagnostic', value[:, 4:], lower[:, 4:], upper[:, 4:], diagnostic=True)

        level = metric(entry, 'rms_dbfs')
        if key in (anchor, 'mix-6-0-0'):
            near('absolute_anchor_dbfs', level, ref(key, 'rms_dbfs') + gain, limits['anchor_db'])
        if key.startswith('wave-'):
            tuned_hz = 440 * 2 ** ((case['note'] - 69) / 12)
            near('pitch_error_cents', 1200 * np.log2(metric(entry, 'pitch_hz') / tuned_hz), 0, limits['pitch_cents'])
            near('rms_dbfs', level, ref(key, 'rms_dbfs') + gain, limits['wave_level_db'])
            saw = f'wave-vco1-n{case["note"]}-w63'
            near('relative_wave_level_db', level - observed(saw, 'rms_dbfs'),
                 ref(key, 'rms_dbfs') - ref(saw, 'rms_dbfs'), limits['wave_level_db'])
            error = np.sqrt(np.mean((metric(entry, 'cycle') - ref(key, 'cycle')) ** 2, axis=1))
            bound('signed_cycle_rmse', error, 0, limits['cycle_rmse'])
        elif key.startswith('mod-'):
            near('modulation_rms_dbfs', level, ref(key, 'rms_dbfs') + gain, limits['modulation_db'])
            bands = np.maximum(metric(entry, 'band_rms_dbfs') - gain - reference_anchor[:, None], floor)
            centers = np.maximum(ref(key, 'band_rms_dbfs') - reference_anchor[:, None], floor)
            band_bounds('modulation_bands_relative_saw_db', bands,
                        centers - limits['spectrum_db'], centers + limits['spectrum_db'])
        elif key.startswith('mix-'):
            sources = []
            for vco in (1, 2):
                for sub, value in ((False, case[f'mix{vco}'] // 3), (True, case[f'mix{vco}'] % 3)):
                    if value:
                        mix = value * (1 if sub else 3)
                        sources.append((f'mix-{mix if vco == 1 else 0}-{mix if vco == 2 else 0}-0', value))
            noise = f'mix-0-0-{case["nois"]}' if case['nois'] else None
            count = len(sources) + bool(noise)
            if not count:
                ceiling = np.maximum(-240, ref(key, 'rms_dbfs') + gain + limits['silence_margin_db'])
                bound('silence_rms_dbfs', level, -240, ceiling)
            elif count == 1:
                near('isolated_source_rms_dbfs', level, ref(key, 'rms_dbfs') + gain, limits['source_level_db'])
                saw = 'mix-6-0-0'
                near('relative_source_level_db', level - observed(saw, 'rms_dbfs'),
                     ref(key, 'rms_dbfs') - ref(saw, 'rms_dbfs'), limits['source_level_db'])
                if noise:
                    near('noise_200_4000_relative_saw_db', band_level(entry) - observed(saw, 'rms_dbfs'),
                         band_level(baseline) - ref(saw, 'rms_dbfs'), limits['noise_db'])
                    bands = np.maximum(metric(entry, 'band_rms_dbfs') - gain - reference_anchor[:, None], floor)
                    centers = np.maximum(ref(key, 'band_rms_dbfs') - reference_anchor[:, None], floor)
                    band_bounds('noise_bands_relative_saw_db', bands,
                                centers - limits['spectrum_db'], centers + limits['spectrum_db'])
                if (sources and sources[0][1] == 1) or case['nois'] == 1:
                    on = dict(case)
                    on['mix1'] = case['mix1'] * 2
                    on['mix2'] = case['mix2'] * 2
                    on['nois'] = case['nois'] * 2
                    on_key = f'mix-{on["mix1"]}-{on["mix2"]}-{on["nois"]}'
                    near('middle_minus_on_db', level - observed(on_key, 'rms_dbfs'),
                         ref(key, 'rms_dbfs') - ref(on_key, 'rms_dbfs'), limits['noise_db'] if noise else limits['middle_db'])
            else:
                # Coherent tones may reinforce or cancel; noise power adds independently.
                for name in ('rms_dbfs', 'band_rms_dbfs'):
                    shape = (channels, 7) if name == 'band_rms_dbfs' else (channels,)
                    tones = np.array([10 ** (ref(source, name) / 20) for source, _ in sources])
                    total = tones.sum(axis=0) if len(tones) else np.zeros(shape)
                    largest = tones.max(axis=0) if len(tones) else np.zeros(shape)
                    n = 10 ** (ref(noise, name) / 20) if noise else np.zeros(shape)
                    lower = db(np.sqrt(np.maximum(0, 2 * largest - total) ** 2 + n*n)) + gain
                    upper = db(np.sqrt(total*total + n*n)) + gain
                    value = metric(entry, name)
                    if name == 'band_rms_dbfs':
                        value = np.maximum(value - gain - reference_anchor[:, None], floor)
                        lower = np.maximum(lower - gain - reference_anchor[:, None], floor)
                        upper = np.maximum(upper - gain - reference_anchor[:, None], floor)
                    property = 'phase_aware_band_relative_saw_db' if name == 'band_rms_dbfs' else 'phase_aware_rms_dbfs'
                    if name == 'band_rms_dbfs':
                        band_bounds(property, value, lower - limits['mixture_db'], upper + limits['mixture_db'])
                    else:
                        bound(property, value, lower - limits['mixture_db'], upper + limits['mixture_db'])
        else:
            raise ValueError(f'Unknown reference case class: {key}')
        results.append(dict(id=key, status='failed' if any(c['status'] == 'failed' for c in checks) else 'passed', checks=checks))
    return dict(status='passed' if all(c['status'] == 'passed' for c in results) else 'failed',
                target=actual['target'], fixed_gain_db=gain, cases=results)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action', choices=('measure', 'check'))
    ap.add_argument('run', type=Path)
    ap.add_argument('--expectations', type=Path)
    ap.add_argument('--out', type=Path, help='new JSON file for measure; new report directory for check')
    args = ap.parse_args()
    if args.action == 'measure':
        if not args.out or args.expectations:
            ap.error('measure requires --out NEW_JSON and does not accept --expectations')
        actual = measure_run(args.run)
        with args.out.open('x') as output:
            json.dump(actual, output, indent=2, allow_nan=False)
            output.write('\n')
        print(f'Measurements only, not approved expectations: {args.out}')
        return 0
    if not args.expectations:
        ap.error('check requires --expectations JSON')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out = args.out or ROOT / 'out/assertions' / stamp
    out.mkdir(parents=True, exist_ok=False)
    report = dict(status='error', run=str(args.run), expectations=str(args.expectations))
    try:
        expected = read_json(args.expectations)
        actual = measure_run(args.run)
        write_json(out / 'measurements.json', actual)
        if 'reference' in expected:
            spec = expected['reference']
            if set(spec) != {'path', 'sha256'} or not isinstance(spec['path'], str):
                raise ValueError('Invalid numeric reference specification')
            path = args.expectations.parent / spec['path']
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != spec['sha256']:
                raise ValueError('Numeric reference SHA-256 mismatch')
            report.update(compare_reference(actual, expected, read_json(path)), reference_sha256=digest)
        else:
            report.update(compare(actual, expected))
        report['expectations_sha256'] = hashlib.sha256(args.expectations.read_bytes()).hexdigest()
    except (ValueError, KeyError, TypeError, OverflowError, OSError, RuntimeError, wave.Error) as exc:
        report['error'] = str(exc)
    write_json(out / 'assertions.json', report)
    print(f'{report["status"]}: {out / "assertions.json"}')
    if 'error' in report:
        print(report['error'], file=sys.stderr)
    if report.get('cases'):
        failed = sum(case['status'] == 'failed' for case in report['cases'])
        print(f'{len(report["cases"])} cases checked: {len(report["cases"]) - failed} passed, {failed} failed')
        diagnostics = sum(check['status'] == 'diagnostic' for case in report['cases'] for check in case['checks'])
        if diagnostics:
            print(f'{diagnostics} high-frequency comparisons retained as diagnostics (4000–24000 Hz); excluded from pass/fail')
    for case in report.get('cases', []):
        for check in case['checks']:
            if check['status'] == 'failed':
                limit = check.get('expected', check.get('expected_max'))
                print(f'{case["id"]} {check["property"]}: actual={check["actual"]}, expected={limit}')
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    sys.exit(main())
