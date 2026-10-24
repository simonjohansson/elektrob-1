# OB-1 for the Digitakt and Digitone

An Oberheim OB-1 oscillator section as a machine for the Digitakt mk1 and Digitone mk1 / Keys. It's added
to the OS by [elekloader](https://github.com/irpina/elekloader). Pick OB-1 as a track's machine and its
machine pages play two analog-style VCOs. The track's filter, envelopes, LFOs, level and sends work on it
as they do on the stock machine.

- **Two VCOs.** Each has the OB-1's two waveshapes:
  - **saw type**, from triangle to sawtooth;
  - **pulse type**, from a narrow pulse to a square;
  - a **sub-octave** square.
- **Hard sync** (VCO2 to VCO1) and **cross mod** (VCO2's saw sweeping VCO1's pitch, about ±3.92 octaves).
- **A noise source.**
- **The OB-1's mixer:** VCO1, VCO1 OCT, VCO2, VCO2 OCT and NOISE, each ON, −3 dB or OFF as on the panel, AC-coupled.
- **Reduced aliasing** with polyBLEP and polyBLAMP corrections, with [limits at high pitches and under sync](docs/dsp.md#band-limiting).
- **Two machine pages** with fifteen controls. On the Digitakt, the SRC pages put oscillators first and mixer/routing second; on the Digitone, SYN1 is mixer/routing and SYN2 is oscillators.
- **Display graphics** showing waveform shapes, tuning and routing, with vertical three-position switches for output levels.

Digitakt: OS 1.53 and 1.54. Digitone: OS 1.44. Both use machine id 69.

The Digitone machine is documented in [machines/digitone/README.md](machines/digitone/README.md).

## Install

You need the stock OS file for the device you are building, from Elektron, and macOS or Linux with [mise](https://mise.jdx.dev). Run `mise bootstrap` once for the m68k toolchain, tools, submodules and the emulator's dependencies.

### Digitakt mk1

`Digitakt_OS1.54.syx` (or 1.53's, with `ELEKLOADER_STOCK` in `mise.toml` pointed at it). The build patches it, and only its main OS section changes.

```sh
mkdir -p firmware
cp ~/Downloads/Digitakt_OS1.54.syx firmware/
mise run firmware:digitakt   # builds everything and writes out/Digitakt_custom.syx
```

### Digitone mk1 / Keys

`Digitone_and_Digitone_Keys_OS1.44.syx`, with `DIGITONE_STOCK` in `mise.toml` pointed at yours. The build patches it the same way; the bootloader is never changed.

```sh
mkdir -p firmware
cp ~/Downloads/Digitone_and_Digitone_Keys_OS1.44.syx firmware/
mise run firmware:digitone   # builds everything and writes out/ob1-dn-1.44-0.1a.syx
```

`mise run firmware` builds both devices and needs both stock files.

### Flash it

With Elektron Transfer, as for any OS update ([elekloader's steps](https://github.com/irpina/elekloader/blob/main/docs/BUILDING.md#flash-it)):

1. Connect the unit over USB and open Transfer.
2. Select the unit and **Connect**.
3. Drag the built file onto **Drop files here** (`out/Digitakt_custom.syx` or `out/ob1-dn-1.44-0.1a.syx`).
4. Press **YES** on the unit, and don't power it off until it has written the OS and restarted.

### If it won't start

The bootloader is never changed, so the stock OS always restores the unit. On the Digitakt, hold **FUNC** while powering on,
press **TRIG 4** for **OS UPGRADE**, and send the stock file with Transfer's legacy OS upgrade mode. For the Digitone,
use Elektron's OS-upgrade recovery for that unit.

## Digitakt mk1

On a track's SRC page, open the machine menu (**FUNC + SRC**) and pick **OB-1**.

### Init patch

A track switched to OB-1 starts with two sawtooths: VCO1's fundamental ON, VCO2's at −3 dB,
subs, noise and modulation OFF. Both coarse tunings are centred; **FIN1** is **+0.0 cents** and
**FIN2** is **+2.4 cents**. This small detune lets the oscillators move through phase cancellation
instead of holding it. Set FIN2 to +0.0 for exact unison. Saved sounds keep their stored tuning.

### Controls

The first SRC page puts both oscillators' performance controls together: VCO1 on the top row,
VCO2 on the bottom row. Press **SRC** to switch to the mixer and routing page; press it again to return.
Knobs A–D control the top row and E–H the bottom row. The mixer page leaves knob D unused.

| Page 1 — Oscillators | Page 2 — Mixer and routing |
|---|---|
| ![Digitakt oscillator page with independent tuning, wave family and shape controls for both VCOs](src-page.png) | ![Digitakt mixer page with three-position level switches, cross modulation and sync controls](src-mixer-page.png) |

Captured directly from a Digitakt mk1 running OB-1. The settings shown are examples, not the defaults.

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
and fine tuning spans −50.0 to +50.0 cents. **TYP1/TYP2** select **SAW** or **PULS** independently.
**WAV1/WAV2** span 0–63 within the selected family: triangle to saw for SAW, narrow pulse to square
for PULS. Sweeping WAV never switches family. The waveform pictures follow both the family and shape,
including the values shown while editing held-step locks.
Fundamental, sub-octave and noise levels use vertical three-position switches: ON at the top,
−3DB in the middle, OFF at the bottom.

The parameter mapping replaces earlier builds without migration. Sounds saved by this build use
machine-pages' version-4 format and require compatible custom firmware to reload.

Some things to try:

- **Sync sweep:** SYNC ON, VCO2 alone, then sweep FRQ2 or put an LFO on it.
- **Bells and gongs:** XMOD ON with VCO2 a few octaves down. That's what the OB-1's manual suggests cross mod for.
- **A fat unison:** both fundamentals ON, FRQ1 and FRQ2 at +0, then detune FIN2 by a few cents and add the subs to taste.

The original OB-1's VCOs run continuously, even between notes and when their mixer outputs are OFF.
Preserving that phase progression matters for faithful mixer and modulation behavior. Notes and
mixer switches do not reset this model's oscillator phase: both VCOs and their sub-octaves keep
running throughout an active voice, including muted sources. Phase continuity through every fully
silent period is not yet guaranteed.

### Voices and CPU

Computing both oscillators, their subs and selected sync/cross-modulation paths has a substantial
CPU cost on top of the stock engine, even when their mixer outputs are OFF. Muting a source does
not reclaim that work. Waveform and routing choices still affect the cost per voice.

We recommend including [DigiHealth](https://github.com/irpina/digihealth#install) when composing your
firmware in Elekloader, and leaving **SETTINGS → FAST AUDIO** ON. With two demanding OB-1 tracks on a
Digitakt mk1 running OS 1.54, it reduced average render load from **89.1% to 80.2%** and increased the
observed peak margin from **4.2% to 14.6%**. See the [matched FAST AUDIO test](docs/performance.md#fast-audio-on-the-device).

See the [two-instance device measurements](docs/performance.md#two-instance-device-timing) and
[matched CPU-cost comparison](docs/performance.md#continuous-muted-oscillators).
`mise run bench` counts ColdFire instructions for the DSP alone; it does not measure complete firmware
timing or establish a safe voice count. Leave headroom and check the intended sound on the device.
See [benchmark interpretation](docs/development.md#benchmark-interpretation).

## Digitone mk1 / Keys

The same oscillator section, this time as a Digitone mk1 / Keys machine (`ob1-dn`, version 0.1a, machine id 69), for OS 1.44. Pick OB-1 as a track's machine and its SYN pages show the two VCOs, mixer and routing; the Digitone's filter, envelopes, effects, level and amp work downstream.

### Init patch

A track switched to OB-1 starts as two saws: VCO1's fundamental ON, VCO2 at −3 dB, subs, noise and modulation OFF — the Digitakt default. Both VCOs run freely between notes, as the OB-1's do. Turn an encoder to show its value in place of the label; turn popups carry it too. **FUNC + turn** resets a knob to OB-1's default, or to a held step's lock in GRID/STEP record.

### Controls

**SYN1** is the mixer and routing page; **SYN2** is the oscillator page. Knobs A–D control the top row and E–H the bottom row. SYN1 leaves knob D unused.

| SYN1 — Mixer and routing | SYN2 — Oscillators |
|---|---|
| ![Digitone SYN1 mixer page with three-position level switches, cross modulation and sync controls](machines/digitone/syn1-mix-page.png) | ![Digitone SYN2 oscillator page with independent tuning, wave family and shape controls for both VCOs](machines/digitone/syn2-osc-page.png) |

Captured from digiemu.

| Knob | SYN1 — Mixer / routing | SYN2 — Oscillators |
|---|---|---|
| A | **FND1** — VCO1 fundamental level | **FRQ1** — VCO1 coarse tuning |
| B | **OCT1** — VCO1 sub-octave level | **FIN1** — VCO1 fine tuning |
| C | **XMOD** — cross modulation OFF / ON | **TYP1** — VCO1 saw / pulse family |
| D | Blank | **WAV1** — VCO1 waveform shape |
| E | **FND2** — VCO2 fundamental level | **FRQ2** — VCO2 coarse tuning |
| F | **OCT2** — VCO2 sub-octave level | **FIN2** — VCO2 fine tuning |
| G | **SYNC** — hard sync OFF / ON | **TYP2** — VCO2 saw / pulse family |
| H | **NOIS** — noise level | **WAV2** — VCO2 waveform shape |

The tuning ranges, waveform families and three-position levels match the Digitakt machine. **SYN2** holds the oscillator page because **SYN1**'s stock ALGO range (0–7) is too coarse for FRQ1's ±32 semitones. P-locks, LFOs, MIDI CCs and presets work through the borrowed stock SYN slots; turn popups and LFO destination lists keep FM names.

The sync, cross-mod and unison examples above work the same way.

### Voice handling

One note per track, as on the OB-1 and the Digitakt machine. The newest held note sounds; releasing it returns to the newest note still held, as long as that note still has a stock voice. Each track keeps the same two VCOs across note changes, with a short crossfade, so the handover stays click-free and phase-continuous. The tracks keep independent note stacks; under normal allocation, notes on one track do not silence another track.

This differs from the Digitakt implementation. The Digitakt machine owns one oscillator voice per track directly. The Digitone adapter runs inside the companion's stock-allocated voices: it uses the stock note-on and release events to maintain the last-note stack, and silences every voice on the track except the one that should sound. The companion keeps ownership of the voice, the AMP envelope and velocity, so the normal AMP page and FM tracks are unchanged.

The adapter cannot control the Digitone's voice allocator. With the default dynamic allocation, an overlapping note gets another voice and the held-note return works. If **LOCKED VOICES** gives the track one voice, the overlapping note reuses that voice; the earlier note no longer has a voice, and releasing the new note can fall silent instead of returning. The same limit applies once the stock allocator has stolen a held note's voice. Leave the track's voices dynamic for normal last-note priority.

A proper one-voice policy belongs upstream. `machines-dn1` would have to keep the track's held-note stack independently of the stock voices, decide the single sounding note before the allocator commits, and reserve, retrigger or redirect one voice on note-on and note-off. `core-dn1` exposes `ev_voice_on` only after the allocator has chosen and possibly reused a voice, plus a per-block mask of released voices; it has no supported pre-allocation or voice-control hook. Retuning `fw_voice_pitch` is not enough because the earlier note's gate and AMP envelope have already moved to the reused voice. `DNM_OWN_ENV` would not fix allocation and would duplicate the companion's AMP envelope and velocity, risking the normal AMP-page behaviour.

That work can affect shared elekloader behaviour for every machine and for FM allocation, and needs its own design, tests and release across the supported OS versions. It is not part of this machine's release. The current adapter is therefore the reasonable choice for now: one audible note per track, last-note priority, phase continuity and the stock AMP path, with the locked-voice limitation above.

### CPU

Computing both oscillators, their subs and selected sync/cross-modulation paths costs more than the stock engine, and muting a source does not reclaim it. The audible voice uses the same shared DSP as on the Digitakt. A stock voice the adapter has silenced is written as silence, but it still occupies a voice and the companion still runs its AMP bookkeeping; overlapping notes can use more than one voice.

Emulator fwcheck against stock and scripted interaction coverage remain pending; hardware testing covered the mono voice handling, both pages, turns, locks and the FUNC+turn reset. Re-pick the machine after flashing; the knob map changed during development, so sounds saved with earlier builds may point at a different control.

## How faithful it is

The model follows the OB-1's oscillator topology, with saw-family shape/level, cross-modulation depth,
mixer steps and white-noise output calibrated against the reference. Both mixer middle positions
are approximately −3 dB. The minimum pulse width remains provisional at 6.7 %; drift and other analog
variation are omitted. Agreement with the reference is distinct from validation against a
physical OB-1, for which no controlled capture set is included.

[Sources and confidence](docs/references.md) separates printed facts, calculations, measurements and
model choices. [The DSP reference](docs/dsp.md) explains the sound and its approximations.

## Technical documentation

- [Architecture](docs/architecture.md): signal flow, device boundaries, parameter encoding and saved sounds.
- [DSP](docs/dsp.md): numeric formats, waveforms, modulation, mixing and approximation limits.
- [Development](docs/development.md): setup, commands, verification, benchmarks and releases.
- [Digitone machine](machines/digitone/README.md): install, controls and limits.
- [Sources and confidence](docs/references.md): manuals, schematics, derivations and unresolved questions.

For host-only work, start with `mise run test`; stock firmware and the cross-toolchain are not needed.

## Contributors

- [x2k77m1](https://github.com/x2k77m1) added Digitone support.

## License

GPL-2.0-or-later.
