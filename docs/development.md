# Development and verification

Use mise for language runtimes and project tasks. The [architecture](architecture.md) describes the
device boundaries; [DSP](dsp.md) and [sources](references.md) separate model behavior from hardware evidence.

## Host-only setup

With mise and a host C compiler available, run from the repository root:

```sh
mise install
mise run test
mise run render out/saw.wav note=57 seconds=4 mix2=0
mise run analyze out/saw.wav
mise run knobs:digitakt
```

These tasks need neither stock firmware nor the m68k cross-toolchain. The analysis task uses uv to
obtain its Python dependencies. Host tests check oscillator events, spectra, sync,
cross modulation, noise and mixer behavior. They do not test the complete device wrapper or saved sounds.

## Commands and firmware

| Task | Result |
|---|---|
| `mise bootstrap` | Installs configured tools and packages, initializes submodules and prepares emulator dependencies. |
| `mise run build` | Builds the device mods and dependencies for each stock OS file in `firmware/`, into `out/os<OS>/`. |
| `mise run build:deps` | Builds device dependencies; the Digitakt build recreates `out/os*`. |
| `mise run firmware` | Patches the selected stock OS into `out/Digitakt_custom.syx`. |
| `mise run emu` | Builds the firmware and opens it in digiemu. Use FUNC+SRC to choose the machine; shift-click FUNC to latch it. |
| `mise run test` | Compiles and runs the host DSP checks. |
| `mise run render OUT.wav [key=value...]` | Produces 48 kHz mono, 24-bit PCM audio. |
| `mise run analyze FILE.wav` | Estimates fundamental frequency, harmonic levels and the strongest non-harmonic spectral bin. |
| `mise run knobs` | Prints knob graphics as text for inspection. |
| `mise run bench` | Counts ColdFire instructions for three sound-engine workloads. |
| `mise run release` | Builds, runs host tests and publishes the OB-1 mod artifacts on GitHub. |

Device tasks dispatch to Digitakt and Digitone. Their direct forms are `build:digitakt`,
`build:deps:digitakt`, `firmware:digitakt`, `emu:digitakt`, `knobs:digitakt` and `release:digitakt`;
replace `digitakt` with `digitone` for the other target. Every Digitone task currently prints a
placeholder message and succeeds without producing artifacts, running an emulator or publishing.
Shared host tasks and bootstrap run once.

For firmware work, obtain stock Digitakt mk1 OS 1.53 or 1.54 from Elektron. Create `firmware/` before
copying it there, and set `ELEKLOADER_STOCK` in `mise.toml` to the desired stock file. Run `mise bootstrap`
and then the relevant build task. Bootstrap installs Homebrew's m68k-elf gcc and binutils on macOS and
Linux. See the [installation and recovery steps](../README.md#install) before using the output on a unit.

The dependency submodule revisions pin the SDK and emulator sources. OB-1's Core 3.0 minimum describes
interface compatibility; a build can select a later compatible Core. Core 4.0 increases shared mod
RAM from 128 KB to 256 KB; it does not add CPU capacity. Check the actual generated artifacts when
identifying what a firmware build contains. Do not infer that from the minimum Core requirement.

## Rendering controls

The host defaults match switching to OB-1: both saws in unison, VCO1 FUND ON and VCO2 FUND −3DB,
everything else OFF. Set `mix2=0` explicitly when measuring VCO1 alone.

| Setting | Host values |
|---|---|
| `note`, `seconds` | MIDI note, default 57 (A3, 220 Hz); duration, default 4 seconds. |
| `wav1`, `wav2` | 0–63 triangle to saw, 64–127 narrow pulse to square; default 63. |
| `frq2` | 0–63, unison 32; the semitone interval is value minus 32. |
| `mix1`, `mix2` | Decoded FUND/OCT combinations, 0–8; defaults 6 and 3. See [parameter encoding](architecture.md#parameters-and-saved-sounds). |
| `mod` | 0 OFF, 1 SYNC, 2 XMOD, 3 BOTH. |
| `nois` | 0 OFF, 1 −3DB, 2 ON. |
| `sweep=KNOB:FROM:TO` | Linear movement of one knob, updated once per block. |

```sh
mise run render out/sync.wav note=57 seconds=4 mix1=0 mix2=6 mod=1 sweep=frq2:32:63
```

Host rendering produces the oscillator source without the stock track's filter, envelope or effects.
Every invocation starts a fresh voice. It cannot establish device note-to-note phase behavior.

## Verification by change

| Change | Useful verification | Evidence still needed |
|---|---|---|
| Documentation or comments | Check links, commands and whitespace; confirm executable content is unchanged. | Any new factual claims need their own sources. |
| Oscillator, mixer or modulation | Host tests, isolated renders, spectral and level comparisons. | Device timing and any claimed analog fidelity. |
| Parameter mapping or defaults | Relevant host checks, firmware build, emulator control checks. | Locks, LFOs, machine switching, save/reload and reboot on a unit. |
| Graphics | Text previews plus emulator or device screenshots. | Physical readability and control behavior. |
| SDK, firmware or release changes | Build each supported OS, inspect artifacts, exercise the complete firmware. | Exact-build hardware qualification. |

CI runs host DSP tests. A passing job does not establish firmware linking, device UI, persistence,
CPU headroom or agreement with physical OB-1 hardware. Repeat broader checks when a change crosses
those boundaries, rather than treating every task as requiring every check.

## Benchmark interpretation

The instruction benchmark uses eight voices at fixed pitches, after warm-up. Its cases are one saw
with other sources OFF, both VCOs and their subs, and a heavy combination with sync, cross modulation
and noise. The one-saw case is not the default sound. The heavy case is a selected workload, not an
exhaustive search across notes, controls and simultaneous device activity.

The displayed block percentages assume stock rendering costs 84,000 instructions and uses 80 % of
a block, giving an inferred full-block budget of 105,000 instructions. They estimate added load from
that baseline. Instruction counts do not account for differing instruction timings, memory stalls,
interrupts or the rest of the firmware. They are useful for comparing the same workload before and
after a DSP change, not for certifying voice capacity. Record the source revision, toolchain and
settings with performance claims, and measure complete firmware on the intended device.

## Releases

Use a clean source revision already reachable from `origin/main` and the intended mod version.
Pushing a feature branch alone does not satisfy this requirement. The release task builds each
available stock OS, runs host tests, requires a clean working tree and a revision reachable from `origin/main`,
then creates `ob1-v<version>` on GitHub with the corresponding `ob1-<version>*.elemod` files. It needs
the local stock firmware, so it runs locally. Publication itself does not qualify the build on hardware.

Before publishing, verify the intended firmware on the unit: controls, trigs, locks, LFOs, switching
between stock and OB-1 machines, save/reload/reboot, and the expensive sounds expected in use.
