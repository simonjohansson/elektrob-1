# OB-1 for the Digitakt

An Oberheim OB-1 oscillator section as a sample-track machine for the Digitakt mk1. It's an SRC machine,
added to the OS by [elekloader](https://github.com/irpina/elekloader). Pick OB-1 as a track's machine and
its SRC page plays two analog-style VCOs instead of a sample. The track's filter, envelopes, LFOs, level
and sends work on it as they do on a sample.

- **Two VCOs.** Each has the OB-1's two waveshapes:
  - **saw type**, from triangle to sawtooth;
  - **pulse type**, from a narrow pulse to a square;
  - a **sub-octave** square.
- **Hard sync** (VCO2 to VCO1) and **cross mod** (VCO2's saw sweeping VCO1's pitch, about ±3.92 octaves).
- **A noise source.**
- **The OB-1's mixer:** VCO1, VCO1 OCT, VCO2, VCO2 OCT and NOISE, each ON, −3 dB or OFF as on the panel, AC-coupled.
- **Reduced aliasing** with polyBLEP and polyBLAMP corrections, with [limits at high pitches and under sync](docs/dsp.md#band-limiting).
- **Knob graphics** showing each oscillator's waveform, wave family, coarse tuning, routing and noise.

Built for OS 1.53 and 1.54. It's machine id 69.

Digitone support is [scaffolded](machines/digitone/README.md); its build tasks are placeholders.

## Install

You need the stock Digitakt mk1 OS file from Elektron: `Digitakt_OS1.54.syx` (or 1.53's, with `ELEKLOADER_STOCK`
in `mise.toml` pointed at it). The build patches it, and only its main OS section changes. You also need macOS or Linux with [mise](https://mise.jdx.dev).

```sh
mkdir -p firmware
cp ~/Downloads/Digitakt_OS1.54.syx firmware/
mise bootstrap        # the m68k toolchain, tools, submodules, the emulator's dependencies
mise run firmware     # builds everything and writes out/Digitakt_custom.syx
```

### Flash it

With Elektron Transfer, as for any OS update ([elekloader's steps](https://github.com/irpina/elekloader/blob/main/docs/BUILDING.md#flash-it)):

1. Connect the Digitakt over USB and open Transfer.
2. Select the unit and **Connect**.
3. Drag `out/Digitakt_custom.syx` onto **Drop files here**.
4. Press **YES** on the unit, and don't power it off until it has written the OS and restarted.

### If it won't start

The bootloader is never changed, so the stock OS always restores the unit. Hold **FUNC** while powering on,
press **TRIG 4** for **OS UPGRADE**, and send the stock file with Transfer's legacy OS upgrade mode.

## Playing it

On a track's SRC page, open the machine menu (**FUNC + SRC**) and pick **OB-1**. A track switched to OB-1
starts as two sawtooths in unison: VCO1's fundamental ON, VCO2's at −3 dB, everything else OFF.

The first SRC page puts both oscillators' performance controls together: VCO1 on the top row,
VCO2 on the bottom row. Press **SRC** for the mixer and routing page.

| Knob | Page 1 — Oscillators | Page 2 — Mixer / routing |
|---|---|---|
| A | **FRQ1** — VCO1 coarse tuning | **FND1** — VCO1 fundamental level |
| B | **FIN1** — VCO1 fine tuning | **OCT1** — VCO1 sub-octave level |
| C | **TYP1** — VCO1 saw / pulse family | **XMOD** — cross modulation OFF / ON |
| D | **WAV1** — VCO1 waveform shape | Blank |
| E | **FRQ2** — VCO2 coarse tuning | **FND2** — VCO2 fundamental level |
| F | **FIN2** — VCO2 fine tuning | **OCT2** — VCO2 sub-octave level |
| G | **TYP2** — VCO2 saw / pulse family | **SYNC** — hard sync OFF / ON |
| H | **WAV2** — VCO2 waveform shape | **NOIS** — noise level |

Each oscillator tunes independently from the played note: coarse tuning spans −32 to +31 semitones,
and fine tuning spans −50.0 to +50.0 cents. TYPE is a two-position SAW/PULS selector.
WAVE spans 0–63 within the selected family: triangle to saw for SAW, narrow pulse to square for PULS. Sweeping WAVE never switches family.
WAV1 and WAV2 show the resulting waveforms, including the values shown while editing held-step locks.
Fundamental, sub-octave and noise levels each have three settings: OFF, −3DB, ON.

The parameter mapping replaces earlier builds without migration. Sounds saved by this build use
machine-pages' version-4 format and require compatible custom firmware to reload.

Some things to try:

- **Sync sweep:** SYNC ON, VCO2 alone, then sweep FRQ2 or put an LFO on it.
- **Bells and gongs:** XMOD ON with VCO2 a few octaves down. That's what the OB-1's manual suggests cross mod for.
- **A fat unison:** both VCOs ON, with FRQ2 at +0 or +12, and the subs added.

Notes do not reset oscillator phase. To save CPU, phases stop advancing while a voice is silent or an
oscillator is neither audible nor needed for modulation; this approximates the OB-1's free-running VCOs.

### Voices and CPU

Each sounding OB-1 voice costs DSP time on top of the stock engine. A voice whose amp envelope has finished
skips sound generation, with a small check and buffer-clearing cost remaining.

| Benchmark sound | Estimated simultaneous voices |
|---|---|
| One VCO, full saw, other sources off | about 8 voices |
| Both VCOs and their subs | about 6 voices |
| Heavy: both VCOs with sync, cross mod and noise | **about 4 voices** |

These are planning estimates from `mise run bench`, not verified hardware limits. The benchmark counts
ColdFire instructions for the DSP alone and extrapolates from a fixed stock-engine baseline. It does not
measure complete firmware timing. The actual default enables both VCOs and is not the one-VCO benchmark.
Leave headroom and check the intended sound on the device. See [benchmark interpretation](docs/development.md#benchmark-interpretation).

## How faithful it is

The model follows the OB-1's oscillator topology, with saw-family shape/level, cross-modulation depth,
mixer steps and white-noise output calibrated against OB-2 by KBPlugs. Both mixer middle positions
are approximately −3 dB. The minimum pulse width remains provisional at 6.7 %; drift and other analog
variation are omitted. Agreement with the reference plugin is distinct from validation against a
physical OB-1, for which no controlled capture set is included.

[Sources and confidence](docs/references.md) separates printed facts, calculations, measurements and
model choices. [The DSP reference](docs/dsp.md) explains the sound and its approximations.

## Technical documentation

- [Architecture](docs/architecture.md): signal flow, device boundaries, parameter encoding and saved sounds.
- [DSP](docs/dsp.md): numeric formats, waveforms, modulation, mixing and approximation limits.
- [Development](docs/development.md): setup, commands, verification, benchmarks and releases.
- [Sources and confidence](docs/references.md): manuals, schematics, derivations and unresolved questions.

For host-only work, start with `mise run test`; stock firmware and the cross-toolchain are not needed.

## License

GPL-2.0-or-later.
