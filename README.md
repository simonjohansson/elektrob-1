# OB-1 for the Digitakt

![OB-1's SRC page on the Digitakt: TUNE, FRQ2, WAV1, WAV2, MOD, MIX1, MIX2 and NOIS](src-page.png)

An Oberheim OB-1 oscillator section as a sample-track machine for the Digitakt mk1. It's an SRC machine,
added to the OS by [elekloader](https://github.com/irpina/elekloader). Pick OB-1 as a track's machine and
its SRC page plays two analog-style VCOs instead of a sample. The track's filter, envelopes, LFOs, level
and sends work on it as they do on a sample.

- **Two VCOs.** Each has the OB-1's two waveshapes:
  - **saw type**, from triangle to sawtooth;
  - **pulse type**, from a narrow pulse to a square;
  - a **sub-octave** square.
- **Hard sync** (VCO2 to VCO1) and **cross mod** (VCO2's saw sweeping VCO1's pitch, ±5 octaves).
- **A noise source.**
- **The OB-1's mixer:** VCO1, VCO1 OCT, VCO2, VCO2 OCT and NOISE, each ON, −3 dB or OFF as on the panel, AC-coupled.
- **Reduced aliasing** with polyBLEP and polyBLAMP corrections, with [limits at high pitches and under sync](docs/dsp.md#band-limiting).
- **Knob graphics** that show each setting: the waveform, the interval, the routing, the mix.

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

| Knob | Name | Values | What it does |
|---|---|---|---|
| A | TUNE | as a sample's | VCO1's pitch against the trig's note |
| B | FRQ2 | −32 to +31 | VCO2, in semitones from VCO1 |
| C | WAV1 | S0–S63, P0–P63 | VCO1's waveshape: S is saw type (S0 triangle, S63 saw); P is pulse type (P0 a 6.7 % pulse, P63 a square) |
| D | WAV2 | S0–S63, P0–P63 | VCO2's waveshape, as WAV1 |
| E | MOD | OFF, SYNC, XMOD, BOTH | VCO2 hard-synced to VCO1, VCO1 cross-modulated by VCO2's saw, or both |
| F | MIX1 | FUND/OCT, each ON, -3 or - (OFF) | VCO1's fundamental and sub-octave in the mix |
| G | MIX2 | FUND/OCT, each ON, -3 or - (OFF) | VCO2's, as MIX1 |
| H | NOIS | OFF, -3DB, ON | Noise in the mix |

Some things to try:

- **Sync sweep:** MOD on SYNC, VCO2 alone, then sweep FRQ2 or put an LFO on it.
- **Bells and gongs:** MOD on XMOD with VCO2 a few octaves down. That's what the OB-1's manual suggests cross mod for.
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

The model follows the OB-1's oscillator topology, schematics and owner's manual. It has not been validated
against controlled recordings of a physical OB-1. The 6.7 % minimum pulse width is provisional,
the ±5-octave cross-mod depth depends on a circuit assumption, and noise level was set by ear.
The panel's −3 dB mixer position currently produces about −6.6 dB for oscillators; the hardware switch
interpretation remains unresolved. Drift and other analog variation are omitted.

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
