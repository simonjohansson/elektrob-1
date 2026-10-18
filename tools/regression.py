# SPDX-License-Identifier: GPL-2.0-or-later
"""Set up and capture OB-1 cases. No audio expectations or comparisons."""
import argparse
import hashlib
import itertools
import json
import math
import subprocess
import sys
import time
import wave
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000
USB_BLOCK_FRAMES = 256
WAVES = (0, 16, 32, 48, 63, 64, 80, 96, 112, 127)
# Digitakt mk1 manual, Appendix B. LFO destinations NONE also disable modulation.
SETUP_CC = (
    (76, 0), (74, 127), (75, 0), (77, 64), (84, 0), (85, 127), (87, 0),
    (78, 0), (79, 127), (80, 0), (81, 0), (82, 0), (83, 0), (10, 64), (7, 127),
    (95, 100), (105, 0), (115, 0), (109, 64), (61, 0), (119, 64), (63, 0),
)


def cases():
    result = []

    def add(name, **values):
        result.append(dict(id=name, note=57, wav1=63, wav2=63, frq2=32,
                           mix1=6, mix2=0, mod=0, nois=0) | values)

    for vco, note, shape in itertools.product((1, 2), (33, 57, 81), WAVES):
        add(f'wave-vco{vco}-n{note}-w{shape}', note=note,
            **{f'wav{vco}': shape, 'mix1': 6 if vco == 1 else 0,
               'mix2': 6 if vco == 2 else 0})
    for m1, m2, noise in itertools.product(range(9), range(9), range(3)):
        add(f'mix-{m1}-{m2}-{noise}', mix1=m1, mix2=m2, nois=noise)
    for mod, interval in itertools.product((1, 2, 3), (32, 39, 44)):
        add(f'mod-{mod}-frq2-{interval}', mod=mod, frq2=interval, mix1=6, mix2=6)
    add('mod-muted-master', mod=1, frq2=39, mix1=0, mix2=6)
    add('mod-muted-source', mod=2, frq2=39, mix1=6, mix2=0)
    add('mod-muted-both', mod=3, frq2=39, mix1=6, mix2=0)
    return result


def select_cases(suite, names):
    available = cases()
    if names:
        unknown = set(names) - {c['id'] for c in available}
        if unknown:
            raise ValueError('Unknown cases: ' + ', '.join(sorted(unknown)))
        return [c for c in available if c['id'] in names]
    if suite == 'smoke':
        wanted = {'wave-vco1-n57-w63', 'wave-vco2-n57-w127',
                  'mix-0-0-0', 'mix-8-8-2', 'mod-muted-both'}
        return [c for c in available if c['id'] in wanted]
    prefix = {'waves': 'wave-', 'mixer': 'mix-', 'modulation': 'mod-'}
    return [c for c in available if suite == 'all' or c['id'].startswith(prefix[suite])]


def source_controls(case):
    level = (21, 64, 106)
    return [32, 64, case['wav1'] // 64, case['wav1'] % 64,
            case['frq2'], 64, case['wav2'] // 64, case['wav2'] % 64,
            level[case['mix1'] // 3], level[case['mix1'] % 3], 127 if case['mod'] & 2 else 0,
            level[case['mix2'] // 3], level[case['mix2'] % 3], 127 if case['mod'] & 1 else 0,
            level[case['nois']]]


def source_cc(case):
    return list(zip(range(16, 31), source_controls(case)))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def wav_info(path):
    with wave.open(str(path), 'rb') as wav:
        frames = wav.getnframes()
        size = wav.getsampwidth() * wav.getnchannels()
        if wav.getframerate() != RATE or not frames or len(wav.readframes(frames)) != frames * size:
            raise RuntimeError(f'Incomplete or wrong-rate recording: {path}')
        return dict(frames=frames, rate=RATE, channels=wav.getnchannels(),
                    sample_bytes=wav.getsampwidth())


def write_wav(path, pcm, width, channels=2):
    with wave.open(str(path), 'wb') as wav:
        wav.setparams((channels, width, RATE, 0, 'NONE', 'not compressed'))
        wav.writeframes(pcm)


class DSP:
    def __init__(self, args, out):
        self.args = args
        self.renderer = out / 'render'
        subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Idsp', 'dsp/dsp.c', 'dsp/voice.c', 'tools/render.c', '-lm',
                        '-o', str(self.renderer)], cwd=ROOT, check=True)

    def capture(self, case, folder):
        settings = [f'{k}={v}' for k, v in case.items() if k != 'id']
        command = [str(self.renderer), str(folder / 'audio.wav'),
                   f'seconds={self.args.settle + self.args.seconds}', *settings]
        subprocess.run(command, check=True, capture_output=True, text=True)
        return dict(command=command, observation='shared voice output after mixer/HPF/trim',
                    setup='fresh voice; no filter, AMP, FX or key input exists at this boundary',
                    sustained_window_frames=[int(self.args.settle * RATE),
                                             int((self.args.settle + self.args.seconds) * RATE) // 32 * 32],
                    sustained_window_seconds=[self.args.settle, self.args.settle + self.args.seconds])

    def close(self):
        pass


def play(backend, case, args):
    """The same ordered setup and note lifecycle for panel and physical MIDI input."""
    backend.setup(case)
    backend.wait(.25)
    try:
        backend.note_on(case['note'])
        backend.wait(args.settle)
        start = backend.mark('sustain')
        backend.wait(args.seconds)
        end = backend.mark('sustain_end')
    finally:
        backend.note_off(case['note'])
    backend.wait(.25)
    return [start, end]


class Device:
    def __init__(self, args, out):
        import rtmidi
        import sounddevice as sd
        self.args, self.sd = args, sd
        self.midi = rtmidi.MidiOut()
        ports = self.midi.get_ports()
        if args.midi_port not in ports:
            raise ValueError(f'Exact MIDI output name required. Available: {ports}')
        self.audio = sd.query_devices(args.audio_device, 'input')
        if 'digitakt' not in args.midi_port.lower() or 'digitakt' not in self.audio['name'].lower():
            raise ValueError('Choose the Digitakt USB MIDI and class-compliant audio devices')
        sd.check_input_settings(device=args.audio_device, channels=2, dtype='int24', samplerate=RATE)
        self.midi.open_port(ports.index(args.midi_port))
        self.events = []

    def send(self, message, channel=None):
        message = [message[0] | ((channel or self.args.channel) - 1), *message[1:]]
        self.midi.send_message(message)
        self.events.append(dict(seconds=time.monotonic() - self.started, midi=message))
        time.sleep(.01)

    def setup(self, case):
        # FX-channel NRPNs: compressor makeup and dry/wet, independent of track CCs.
        for parameter in (20, 23):
            for cc, value in ((99, 2), (98, parameter), (6, 0), (38, 0), (99, 127), (98, 127)):
                self.send([0xB0, cc, value], channel=self.args.fx_channel)
        for cc, value in (*SETUP_CC, *source_cc(case)):
            self.send([0xB0, cc, value])

    def wait(self, seconds):
        target = len(self.pcm) // 6 + math.ceil(seconds * RATE)
        deadline = time.monotonic() + seconds + 2
        while len(self.pcm) // 6 < target:
            if time.monotonic() >= deadline:
                raise RuntimeError('USB audio input stalled before the requested samples arrived')
            time.sleep(.005)

    def mark(self, name):
        frame = len(self.pcm) // 6
        self.events.append(dict(seconds=time.monotonic() - self.started, mark=name, frame=frame))
        return frame

    def note_on(self, note):
        self.send([0x90, note, 100])
        # Let the input pipeline reach the note before counting settling samples.
        self.wait(self.input_latency)

    def note_off(self, note):
        self.send([0x80, note, 0])

    def capture(self, case, folder):
        self.pcm, errors = bytearray(), []

        def record(data, frames, timing, status):
            self.pcm.extend(data)
            if status:
                errors.append(str(status))

        self.events, self.started = [], time.monotonic()
        try:
            with self.sd.RawInputStream(device=self.args.audio_device, channels=2,
                                        samplerate=RATE, dtype='int24', blocksize=USB_BLOCK_FRAMES,
                                        callback=record) as stream:
                self.input_latency = stream.latency
                window = play(self, case, self.args)
        finally:
            write_wav(folder / 'audio.wav', self.pcm, 3)
            write_json(folder / 'events.json', self.events)
        if errors:
            raise RuntimeError('Audio capture errors: ' + '; '.join(errors))
        return dict(observation='Digitakt class-compliant USB stereo input', audio_device=dict(self.audio),
                    reported_input_latency_seconds=self.input_latency,
                    sustained_window_frames=window,
                    midi_port=self.args.midi_port, channel=self.args.channel,
                    fx_channel=self.args.fx_channel,
                    setup_verification='sent only; panel prerequisites and installed build are operator-declared')

    def close(self):
        try:
            self.midi.send_message([0xB0 | (self.args.channel - 1), 123, 0])
        finally:
            self.midi.close_port()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('target', choices=('dsp', 'firmware', 'device'))
    ap.add_argument('--suite', choices=('smoke', 'waves', 'mixer', 'modulation', 'all'), default='smoke')
    ap.add_argument('--case', action='append', default=[], help='exact case ID; repeatable')
    ap.add_argument('--list', action='store_true', help='list selected cases without running')
    ap.add_argument('--ports', action='store_true', help='list physical MIDI/audio ports without sending')
    ap.add_argument('--out', type=Path, help='new output directory; existing directories are refused')
    ap.add_argument('--seconds', type=float, default=2, help='sustained capture after settling')
    ap.add_argument('--settle', type=float, default=1)
    ap.add_argument('--firmware', type=Path, help='firmware SYX; required for firmware/device provenance')
    ap.add_argument('--os', choices=('1.53', '1.54'), help='underlying stock OS')
    ap.add_argument('--midi-port', help='exact physical MIDI output name')
    ap.add_argument('--audio-device', type=int, help='class-compliant Digitakt input index from --ports')
    ap.add_argument('--channel', type=int, default=1, help='track 1 receive channel (1-16)')
    ap.add_argument('--fx-channel', type=int, default=9, help='FX control receive channel (1-16)')
    args = ap.parse_args()
    if args.ports:
        import rtmidi
        import sounddevice
        print('MIDI outputs:', rtmidi.MidiOut().get_ports())
        print(sounddevice.query_devices())
        return
    selected = select_cases(args.suite, args.case)
    if args.list:
        print(json.dumps(selected, indent=2))
        return
    if not math.isfinite(args.seconds) or args.seconds < .01 or not math.isfinite(args.settle) or args.settle < 0:
        ap.error('--seconds must be finite and >= .01; --settle must be finite and >= 0')
    if not 1 <= args.channel <= 16 or not 1 <= args.fx_channel <= 16:
        ap.error('--channel and --fx-channel must be 1-16')
    if args.target == 'device' and args.channel == args.fx_channel:
        ap.error('Use separate track 1 and FX receive channels')
    if args.target != 'dsp' and (not args.firmware or not args.os):
        ap.error('firmware/device requires --firmware and --os')
    if args.target == 'device' and (not args.midi_port or args.audio_device is None):
        ap.error('device requires --midi-port and --audio-device; see --ports and docs/regression.md')
    firmware_hash = hashlib.sha256(args.firmware.read_bytes()).hexdigest() if args.firmware else None
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out = (args.out or ROOT / 'out' / 'regression' / f'{stamp}-{args.target}').resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = dict(target=args.target, audio_comparison='not run', status='running', cases=[],
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  working_tree=subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True),
                  python=sys.version.split()[0],
                  dependency_revisions=subprocess.check_output(['git', 'submodule', 'status'], cwd=ROOT, text=True),
                  firmware_sha256=firmware_hash, stock_os=args.os,
                  settle_seconds=args.settle, sustain_seconds=args.seconds,
                  planned_cases=[c['id'] for c in selected])
    backend = None
    try:
        if args.target == 'firmware':
            from regression_firmware import Firmware
            backend = Firmware(args, out)
        else:
            backend = DSP(args, out) if args.target == 'dsp' else Device(args, out)
        for index, case in enumerate(selected, 1):
            print(f'[{index}/{len(selected)}] {case["id"]}', flush=True)
            folder = out / case['id']
            folder.mkdir()
            result = dict(case=case, status='running', audio_comparison='not run')
            report['cases'].append(result)
            try:
                result.update(backend.capture(case, folder))
                result.update(wav_info(folder / 'audio.wav'))
                start, end = result['sustained_window_frames']
                # Marker positions can differ by one audio block at the window edges.
                quantum = USB_BLOCK_FRAMES if args.target == 'device' else 32
                if not 0 <= start < end <= result['frames'] or end - start + quantum < math.ceil(args.seconds * RATE):
                    raise RuntimeError('Incomplete sustained recording window')
                result['status'] = 'captured'
            finally:
                write_json(folder / 'capture.json', result)
        report['status'] = 'captured'
    except (Exception, KeyboardInterrupt) as exc:
        report.update(status='acquisition error', error=str(exc) or type(exc).__name__)
        if report['cases'] and report['cases'][-1]['status'] == 'running':
            report['cases'][-1].update(status='acquisition error', error=report['error'])
            write_json(out / report['cases'][-1]['case']['id'] / 'capture.json', report['cases'][-1])
        raise
    finally:
        try:
            if backend:
                backend.close()
        except Exception as exc:
            report.update(status='acquisition error', cleanup_error=str(exc))
            raise
        finally:
            write_json(out / 'run.json', report)
            print(f'Capture report: {out / "run.json"}', flush=True)


if __name__ == '__main__':
    main()
