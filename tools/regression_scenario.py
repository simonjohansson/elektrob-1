# SPDX-License-Identifier: GPL-2.0-or-later
"""Record a fixed device scenario, or compare two runs for manual review."""
import argparse
import hashlib
import json
import subprocess
import time
import tempfile
import wave
from pathlib import Path

import numpy as np

from regression import Device, RATE, ROOT, USB_BLOCK_FRAMES, wav_info, write_json, write_wav
from regression_assert import db, measurements, read_json

INITIAL = dict(note=57, wav1=63, wav2=63, frq2=32, mix1=6, mix2=3, mod=0, nois=1)
DURATION = 23


def scenario():
    return [
        (0, 'reset and hold A3', [[0xb0, 9, 127]]),
        (3, 'VCO1 sub on', [[0xb0, 25, 106]]),
        (4, 'VCO2 sub on', [[0xb0, 28, 106]]),
        (5, 'VCO2 fundamental off', [[0xb0, 27, 21]]),
        (6, 'restore fundamental and remove subs', [[0xb0, 27, 64], [0xb0, 25, 21], [0xb0, 28, 21]]),
        (7, 'legato E4', [[0x90, 64, 100], [0x80, 57, 0]]),
        (8, 'VCO1 triangle', [[0xb0, 19, 0]]),
        (9, 'VCO1 pulse', [[0xb0, 18, 1], [0xb0, 19, 32]]),
        (10, 'VCO2 narrow pulse', [[0xb0, 22, 1], [0xb0, 23, 16]]),
        (11, 'legato A3', [[0x90, 57, 100], [0x80, 64, 0]]),
        (12, 'fifth and sync', [[0xb0, 20, 39], [0xb0, 29, 127]]),
        (13, 'cross modulation', [[0xb0, 29, 0], [0xb0, 26, 127]]),
        (14, 'modulation off and noise on', [[0xb0, 26, 0], [0xb0, 30, 106]]),
        (15, 'noise off', [[0xb0, 30, 21]]),
        (16, 'saws at unison', [[0xb0, 18, 0], [0xb0, 19, 63], [0xb0, 22, 0],
                                 [0xb0, 23, 63], [0xb0, 20, 32]]),
        (17, 'legato A2', [[0x90, 45, 100], [0x80, 57, 0]]),
        (18, 'release and rest', [[0x80, 45, 0]]),
        (20, 'A5 after rest', [[0x90, 81, 100]]),
        (21, 'final release', [[0x80, 81, 0]]),
    ]


def firmware_start(pcm, trigger):
    threshold = 64
    if trigger < RATE // 4 or np.max(np.abs(pcm[trigger - RATE // 4:trigger])) > threshold:
        raise ValueError('Firmware scenario requires a silent pre-trigger source and a held stock AMP')
    onset = np.flatnonzero(np.max(np.abs(pcm[trigger:trigger + RATE // 2]), axis=1) > threshold)
    if not len(onset):
        raise ValueError('No firmware scenario audio within 500 ms of CC 12')
    start = trigger + int(onset[0])
    if start + DURATION * RATE > len(pcm):
        raise ValueError('Incomplete firmware scenario recording')
    return start


def play_scenario(device, firmware=False):
    device.setup(INITIAL | dict(mix1=0, mix2=0, nois=0) if firmware else INITIAL)
    device.send([0xb0, 123, 0])
    device.note_on(INITIAL['note'])
    device.wait(1.5)
    start = device.mark('scenario start')
    if firmware:
        device.mark('firmware scenario trigger')
        device.send([0xb0, 12, 127])
        device.wait(DURATION + device.input_latency + .5)
        return start
    for second, label, messages in scenario():
        target = start + second * RATE
        device.wait(max(0, (target - len(device.pcm) // 6) / RATE))
        device.mark(label)
        for message in messages:
            device.send(message)
            device.mark('sent')
    device.wait(max(0, (start + DURATION * RATE - len(device.pcm) // 6) / RATE))
    device.wait(.25)
    return start


def capture(args):
    firmware_hash = hashlib.sha256(args.firmware.read_bytes()).hexdigest()
    args.out.mkdir(parents=True, exist_ok=False)
    firmware_clock = getattr(args, 'firmware_scenario', False)
    report = dict(clock='firmware-blocks' if firmware_clock else 'host-midi', kind='device-scenario', status='running', scenario=scenario(), initial=INITIAL,
                  duration=DURATION, stock_os=args.os, channel=args.channel, fx_channel=args.fx_channel,
                  firmware_sha256=firmware_hash, midi_port=args.midi_port,
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  working_tree=subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True),
                  setup_verification='sent only; installed firmware and device preparation are operator-declared')
    device, errors = None, []
    try:
        device = Device(args, args.out)
        device.pcm, device.events = bytearray(), []
        device.started = time.monotonic()

        def record(data, frames, timing, status):
            device.pcm.extend(data)
            if status:
                errors.append(str(status))

        with device.sd.RawInputStream(device=args.audio_device, channels=2, samplerate=RATE,
                                      dtype='int24', blocksize=USB_BLOCK_FRAMES, callback=record) as stream:
            device.input_latency = stream.latency
            report['start_frame'] = play_scenario(device, firmware_clock)
        if errors:
            raise RuntimeError('USB audio capture errors: ' + '; '.join(errors))
        if len(device.pcm) // 6 < report['start_frame'] + DURATION * RATE:
            raise RuntimeError('Incomplete scenario recording')
        report.update(status='captured', audio_device=dict(device.audio), input_latency_seconds=stream.latency)
    except BaseException as exc:
        report.update(status='acquisition error', error=str(exc) or type(exc).__name__)
        raise
    finally:
        if device is not None:
            cleanup = []
            if firmware_clock:
                try:
                    device.send([0xb0, 9, 0])
                except Exception as exc:
                    cleanup.append(str(exc))
            for note in sorted({INITIAL['note']} | {m[1] for _, _, messages in scenario()
                                                   for m in messages if m[0] == 0x90}):
                try:
                    device.note_off(note)
                except Exception as exc:
                    cleanup.append(str(exc))
            try:
                device.close()
            except Exception as exc:
                cleanup.append(str(exc))
            write_wav(args.out / 'audio.wav', device.pcm, 3)
            write_json(args.out / 'events.json', device.events)
            try:
                report.update(wav_info(args.out / 'audio.wav'))
            except (OSError, RuntimeError, wave.Error) as exc:
                report.update(status='acquisition error', recording_error=str(exc))
            report['wav_sha256'] = hashlib.sha256((args.out / 'audio.wav').read_bytes()).hexdigest()
            report['events_sha256'] = hashlib.sha256((args.out / 'events.json').read_bytes()).hexdigest()
            if cleanup:
                report.update(status='acquisition error', cleanup_errors=cleanup)
        if firmware_clock and report['status'] == 'captured':
            try:
                _, pcm = load(args.out, detect_start=True, report=report)
                report['trigger_frame'] = report['start_frame']
                report['start_frame'] = firmware_start(pcm, report['trigger_frame'])
                report['start_detection'] = 'first stereo sample above 64 PCM24 LSB after trigger; preceding 250 ms quiet'
            except (ValueError, OSError, RuntimeError) as exc:
                report.update(status='acquisition error', error=str(exc))
        write_json(args.out / 'run.json', report)
    if report['status'] != 'captured':
        raise RuntimeError('Scenario acquisition failed; see run.json')
    print(f"Recorded: {args.out / 'audio.wav'}")


def load(folder, detect_start=False, report=None):
    report = dict(report) if report is not None else read_json(folder / 'run.json')
    clock = report.get('clock', 'host-midi')
    if clock not in ('host-midi', 'firmware-blocks'):
        raise ValueError('Unknown scenario clock')
    expected_scenario = json.loads(json.dumps(scenario()))
    if (report.get('kind') != 'device-scenario' or report.get('status') != 'captured'
            or report.get('scenario') != expected_scenario or report.get('initial') != INITIAL
            or report.get('duration') != DURATION):
        raise ValueError('Expected a completed recording of the current scenario')
    path = folder / 'audio.wav'
    info = wav_info(path)
    if any(report.get(k) != v for k, v in info.items()) or info['channels'] != 2 or info['sample_bytes'] != 3:
        raise ValueError('Scenario PCM metadata mismatch')
    if hashlib.sha256(path.read_bytes()).hexdigest() != report.get('wav_sha256'):
        raise ValueError('Scenario WAV digest mismatch')
    events_path = folder / 'events.json'
    if hashlib.sha256(events_path.read_bytes()).hexdigest() != report.get('events_sha256'):
        raise ValueError('Scenario event digest mismatch')
    recorded = read_json(events_path)
    events = [e for e in recorded if e.get('mark') not in (None, 'sent')]
    expected_marks = ['scenario start'] + (['firmware scenario trigger'] if clock == 'firmware-blocks' else [s[1] for s in scenario()])
    if ([e.get('mark') for e in events] != expected_marks
            or any(type(e.get('frame')) is not int or not 0 <= e['frame'] <= info['frames'] for e in events)
            or any(a['frame'] > b['frame'] for a, b in zip(events, events[1:]))):
        raise ValueError('Scenario event markers mismatch')
    report['events'] = events[1:]
    report['setup_messages'] = [e['midi'] for e in recorded[:recorded.index(events[0])] if 'midi' in e]
    start = report.get('start_frame')
    if type(start) is not int or not 0 <= start <= info['frames'] - DURATION * RATE:
        raise ValueError('Incomplete scenario window')
    with wave.open(str(path), 'rb') as wav:
        raw = np.frombuffer(wav.readframes(info['frames']), np.uint8).reshape(-1, 2, 3).astype(np.int32)
    value = raw[:, :, 0] | raw[:, :, 1] << 8 | raw[:, :, 2] << 16
    pcm = ((value ^ 8388608) - 8388608).astype(np.int64)
    if clock == 'firmware-blocks' and not detect_start:
        trigger = report.get('trigger_frame')
        if (type(trigger) is not int or trigger != events[0]['frame']
                or report['start_frame'] != firmware_start(pcm, trigger)):
            raise ValueError('Firmware scenario start mismatch')
    return report, pcm


def alignment(reference, candidate, radius):
    ref = reference.astype(float)
    ref -= ref.mean()
    if np.sqrt(np.mean(ref * ref)) < 1:
        raise ValueError('Alignment anchor is silent')
    search = candidate.astype(float)
    n = len(ref)
    sums, powers = np.r_[0, np.cumsum(search)], np.r_[0, np.cumsum(search * search)]
    energy = powers[n:] - powers[:-n] - (sums[n:] - sums[:-n]) ** 2 / n
    scores = np.correlate(search, ref, mode='valid') / np.sqrt(np.maximum(energy, 1) * np.dot(ref, ref))
    best = int(np.argmax(scores))
    if best in (0, len(scores) - 1) or scores[best] < .5:
        raise ValueError('Cannot align initial held sound within search window')
    return best - radius, float(scores[best])


def difference(reference, candidate):
    delta = candidate - reference
    return dict(exact_samples_percent=(100 * np.mean(delta == 0, axis=0)).tolist(),
                max_abs_lsb=np.max(np.abs(delta), axis=0).tolist(),
                rms_error_dbfs=db(np.sqrt(np.mean((delta / 8388608) ** 2, axis=0))).tolist())


def compare(baseline, candidate, out):
    before, a = load(baseline)
    after, b = load(candidate)
    if before.get('clock', 'host-midi') != after.get('clock', 'host-midi'):
        raise ValueError('Scenario clock mismatch')
    for field in ('stock_os', 'channel', 'fx_channel', 'midi_port'):
        if before[field] != after[field]:
            raise ValueError(f'Scenario setup mismatch: {field}')
    if before['setup_messages'] != after['setup_messages']:
        raise ValueError('Scenario MIDI preparation differs')
    if before['audio_device']['name'] != after['audio_device']['name']:
        raise ValueError('Scenario audio device mismatch')
    origin_a, origin_b = before['start_frame'], after['start_frame']
    anchor, size, radius = RATE, 8192, RATE // 10
    shift, correlation = alignment(a[origin_a + anchor:origin_a + anchor + size, 0],
                                   b[origin_b + anchor - radius:origin_b + anchor + size + radius, 0], radius)
    origin_b += shift
    frames = DURATION * RATE
    if origin_b < 0 or origin_b + frames > len(b):
        raise ValueError('Aligned scenario does not contain the complete duration')
    a, b = a[origin_a:origin_a + frames], b[origin_b:origin_b + frames]
    sections = []
    for index, (second, label, _) in enumerate(scenario()):
        stop = scenario()[index + 1][0] if index + 1 < len(scenario()) else DURATION
        start, end = round((second + .25) * RATE), round((stop - .1) * RATE)
        left, right = measurements(a[start:end] / 8388608, False), measurements(b[start:end] / 8388608, False)
        sections.append(dict(label=label, seconds=[start / RATE, end / RATE],
                             **difference(a[start:end], b[start:end]), baseline=left, candidate=right,
                             level_change_db=(np.asarray(right['rms_dbfs']) - left['rms_dbfs']).tolist(),
                             band_change_db=(np.asarray(right['band_rms_dbfs']) - left['band_rms_dbfs']).tolist()))
    delta = b - a
    clipped = np.clip(delta, -8388608, 8388607)
    raw = np.stack([((clipped >> shift) & 255).astype(np.uint8) for shift in (0, 8, 16)], axis=2).tobytes()
    report = dict(status='compared; manual review required', baseline=str(baseline.resolve()),
                  candidate=str(candidate.resolve()), baseline_firmware_sha256=before['firmware_sha256'],
                  candidate_firmware_sha256=after['firmware_sha256'], alignment_samples=shift,
                  alignment_correlation=correlation, **difference(a, b), sections=sections,
                  difference_wav_clipped_samples=int(np.count_nonzero(delta != clipped)),
                  host_event_timing=[dict(label=a['mark'], delta_frames=(b['frame'] - origin_b) - (a['frame'] - origin_a))
                                     for a, b in zip(before['events'], after['events'])],
                  timing=('One global alignment only. Firmware DSP events use fixed 32-sample blocks; stock AMP is held.'
                          if before.get('clock') == 'firmware-blocks' else
                          'One global alignment only. Later MIDI arrival timing and phase differences remain in the diff.'))
    out.mkdir(parents=True, exist_ok=False)
    write_wav(out / 'difference.wav', raw, 3)
    write_json(out / 'comparison.json', report)
    print(f"Manual comparison: {out / 'comparison.json'}")
    print('Firmware block clock.' if before.get('clock') == 'firmware-blocks' else 'MIDI timing and free-running phase can produce large differences even with unchanged firmware.')
    print(f"Global alignment: {shift} samples; correlation {correlation:.9f}")
    for section in sections:
        print(f"{section['label']}: error RMS {section['rms_error_dbfs']} dBFS; level change {section['level_change_db']} dB")
    return report


def build_diagnostic(stock, out):
    from elekloader.sdk.build import build
    source = ROOT / 'machines/digitakt'
    manifest = read_json(source / 'mod.json')
    manifest['sources'] = [str((source / name).resolve()) for name in manifest['sources']]
    flag = '-DOB1_DIAGNOSTIC_SCENARIO'
    manifest['cflags'] = manifest.get('cflags', []) + [flag]
    for port in manifest.get('ports', {}).values():
        if 'cflags' in port:
            port['cflags'] = port['cflags'] + [flag]
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as folder:
        write_json(Path(folder) / 'mod.json', manifest)
        path, _ = build(folder, str(stock), str(out))
    print(f'Diagnostic OB-1 mod (CC 12 enabled): {path}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    commands = ap.add_subparsers(dest='command', required=True)
    builder = commands.add_parser('build')
    builder.add_argument('--stock', type=Path, required=True)
    builder.add_argument('--out', type=Path, required=True)
    record = commands.add_parser('capture')
    record.add_argument('--firmware-scenario', action='store_true', help='Trigger the fixed DSP block scenario with CC 12; hold stock AMP and prepare a silent source')
    record.add_argument('--list', action='store_true', help='Print the scenario without opening MIDI or audio')
    record.add_argument('--firmware', type=Path)
    record.add_argument('--os', choices=('1.53', '1.54'))
    record.add_argument('--midi-port')
    record.add_argument('--audio-device', type=int)
    record.add_argument('--channel', type=int, default=1)
    record.add_argument('--fx-channel', type=int, default=9)
    record.add_argument('--out', type=Path)
    diff = commands.add_parser('compare')
    diff.add_argument('baseline', type=Path)
    diff.add_argument('candidate', type=Path)
    diff.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if args.command == 'build':
        build_diagnostic(args.stock, args.out)
    elif args.command == 'compare':
        compare(args.baseline, args.candidate, args.out)
    elif args.list:
        listing = (dict(clock='firmware-blocks', trigger_cc=12,
                        events=[dict(block=second * 1500, label=label) for second, label, _ in scenario()])
                   if args.firmware_scenario else scenario())
        print(json.dumps(listing, indent=2))
    else:
        if any(getattr(args, field) is None for field in ('firmware', 'os', 'midi_port', 'audio_device', 'out')):
            ap.error('Capture requires --firmware, --os, --midi-port, --audio-device and --out')
        if not 1 <= args.channel <= 16 or not 1 <= args.fx_channel <= 16 or args.channel == args.fx_channel:
            ap.error('Use distinct track and FX receive channels in 1–16')
        capture(args)


if __name__ == '__main__':
    main()
