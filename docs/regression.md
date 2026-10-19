# Regression capture setup

Acquisition sets up sounds, plays them and records the output. A separate assertion step compares
numeric measurements with reviewed JSON expectations; reference WAVs are not needed in Git or CI.
`captured` means acquisition finished, not that the sound is correct. Existing DSP correctness
tests protect mechanics, routing, continuity and output policy; recorded reference expectations
belong to the shared assertion step.

The initial catalogue contains 60 isolated waveshape cases (both VCOs, ten positions, three notes),
all 243 FUND/OCT/noise level combinations at a fixed saw/unison/A3, and twelve modulation cases.
Each case specifies every source control. The five-case smoke selection includes both VCOs, silence,
maximum mixing and modulation with a muted source. Persistence, locks/LFO automation, note sequences
and sustained multi-track load are later stages.

## Run the host DSP

From the repository root, with the mise runtimes installed:

```sh
mise run regression:dsp
mise run regression:dsp --suite all
mise run regression:dsp --suite mixer --list
mise run regression:dsp --case wave-vco1-n57-w63
mise run test:reference
```

Suite choices are `smoke`, `waves`, `mixer`, `modulation`, `reference` and `all`.
Repeat `--case` to select exact case IDs. `--list` prints settings without executing them. Defaults are one second settling plus
two seconds sustained audio. `--settle` and `--seconds` change those durations.

The `reference` suite selects 311 cases for routine DSP and emulator CI. It omits the three
SYNC+XMOD interval comparisons and the combined-mode muted-slave comparison: their reference
coupling is not a circuit-supported target. Strict host tests instead check rising raw-saw
feedback, waveform/mixer independence, free slave phase evolution between master resets and
fractional-sample forced resets across pitches and intervals. See the
[circuit rationale](references.md#circuit-constraints-on-sync-and-cross-modulation).
The full 315-case `all` suite and explicit `--case` selections remain available for investigation;
an explicit assertion run on those recordings still performs the combined-mode reference comparisons.

The host captures the shared voice after its mixer, DC removal and output trim. There is no device
filter, AMP, key scanner or note-off envelope at this boundary. It uses the existing renderer with a
fresh voice for each case and labels that distinction in the report.

## Run compiled firmware

Initialize the pinned SDK/emulator and install the emulator's patched Unicorn using the existing
bootstrap workflow. Obtain stock firmware locally as described in [Development](development.md).
The regression task builds the configured Digitakt firmware automatically before starting:

```sh
mise run regression:firmware --firmware out/Digitakt_custom.syx --os 1.54
```

`--os` identifies the underlying stock OS, even when the custom image has another display version.
OS 1.53 and 1.54 are supported. Pass the corresponding image and OS together. The runner creates a
fresh virtual card/project inside its new output directory; it does not open or reset an existing
simulator session. It uses the pinned SDK's OS-specific locations for read-only setup observations.

Panel key events select OB-1. Actual encoder events set both oscillator and mixer pages, filter and
AMP controls, with native sound-slot readback to catch missed or ineffective turns. At both ends of
the sustained recording window, all fifteen live engine source controls must match the case,
including exact zero fine tuning, and the AMP gate must be held with nonzero envelope level. These checks verify setup;
they do not compare the audio to an expected sound. FUNC+TRK enters chromatic mode;
octave arrows and held/released trig key 14 play the catalogue's low/middle/high A notes. The actual
voice note is read back before recording the sustained section. This exercises guest panel/voice
code. Direct envelope writes, forced oscillator blocks and output
substitution are not used.

The setup selects filter TYPE OFF, zero resonance, AMP attack zero, HOLD NOTE, decay zero, overdrive
and sends OFF, centered pan and AMP volume 127. Fresh-project base-width full-span, SRR zero and both
LFOs' neutral settings are checked. The recorder observes the emulator's final stereo SSI output,
including the remaining stock processing. Setup screenshots and native readbacks accompany the audio.
The fresh kit's track level 100 and compressor dry mix are also checked. The playback session disables
the emulator's drawing/float shortcuts; modeled timing and physical performance are outside this run.

## MIDI integration checks

The firmware capture catalogue exercises the panel. A separate firmware integration check exercises
the native MIDI receive path through the emulator's DIN UART:

```sh
mise run test:firmware:midi --firmware out/Digitakt_custom.syx --os 1.54 --out out/midi-check
```

The check reads the linker symbol map beside the firmware image to observe reset initialization.
It checks native lookup and its calling convention, all fifteen stored/live source values at range
and zone boundaries, track isolation, stock/FX fallback, the device adapter's setup messages and
note-on/off. It enables the fresh project's FX receive channel through panel events, then checks
that compressor control reaches the native kit. CC 9 resets are checked at render entry, including
channel isolation and unchanged saved controls. It records firmware provenance and setup observations.
This is firmware-dispatch evidence; USB transport and physical acquisition require a device run.

## Physical Digitakt

Install the intended MIDI-enabled firmware separately. The runner never flashes a device, and its
firmware hash describes the supplied file; it cannot read back the installed build. Use a disposable
project, stop the sequencer, select OB-1 on track 1 and silence the other tracks and external inputs.
Use class-compliant USB audio and USB MIDI, enable RECEIVE NOTES and RECEIVE CC/NRPN, and set INPUT
FROM to USB or MIDI+USB. Set track 1's receive channel to 1 and FX CONTROL CH to 9, or supply matching
`--channel` / `--fx-channel` values. FX reception is OFF in a fresh project. Keep these channels distinct
from the auto channel and other tracks' receive channels; disable conflicting assignments for this run.

Discover ports without sending notes or controls:

```sh
mise run regression:device --ports
```

Then use the exact MIDI port name and current audio input index, for example:

```sh
mise run regression:device --firmware out/Digitakt_custom.syx --os 1.54 \
  --midi-port 'Elektron Digitakt' --audio-device 1 --case wave-vco1-n57-w63
```

The adapter sends all fifteen [source controls](architecture.md#incoming-midi-controls), filter TYPE
OFF, the known AMP setup, neutral LFOs, zero sends/overdrive, centered pan, track level 100 and dry
compressor with zero makeup. It plays and releases notes while recording the USB stereo input.
Settings are sent without device readback; manual preparation and installed firmware are
operator-declared. A successful capture establishes acquisition, not correct audio or hardware fidelity.
Device waits follow recorded sample counts and fail if input stalls. The reported input latency is
allowed to pass after note-on before counting the settling window; it is retained in capture metadata.

## Repeating a held sound on a device

CC 9 requests full engine initialization at the next active audio block. For a repeatability
study, configure a sound once, hold its note until the stock AMP has settled, and record each
reset while the note remains held. Keep other tracks and inputs silent and stock processing
neutral. The reset repeats the engine's noise seed and all oscillator/output history; it does
not reset stock processing or synchronize later MIDI events to audio samples.

Compare repeated USB recordings after one common integer-sample offset per recording, retaining
relative oscillator phase and polarity. Keep the reset onset separate from the settled window,
report exact equality and residual error, and measure repeatability before choosing tolerances.
MIDI send times and host input-buffer positions do not establish the reset's exact audio sample.
Neither a phase fit per oscillator nor a candidate-derived gain correction establishes unchanged
output. This experiment qualifies only the tested sound, setup and capture route.

## Scripted device comparison

Use a prepared device running firmware with the CC 9 reset command. The same physical-device
prerequisites above apply: OB-1 selected on the chosen track, transport stopped, other tracks and
inputs silent, receive settings enabled and distinct track/FX channels. The task sends the neutral
stock setup and all source settings; it does not read them back, build firmware or flash the device.
The supplied image identifies the operator-declared installed build.

Inspect the predefined sequence without opening MIDI or audio:

```sh
mise run regression:device:scenario --list
```

Record a baseline before the refactor, then record the candidate after installing its firmware:

```sh
mise run regression:device:scenario --firmware out/Digitakt_custom.syx --os 1.54 \
  --midi-port 'Elektron Digitakt' --audio-device 0 --out out/regression/baseline

# Build and install the candidate, retaining the baseline recording.
mise run regression:device:scenario --firmware out/Digitakt_custom.syx --os 1.54 \
  --midi-port 'Elektron Digitakt' --audio-device 0 --out out/regression/candidate

mise run regression:device:diff out/regression/baseline out/regression/candidate \
  --out out/regression/comparison
```

Discover the current device index with `mise run regression:device --ports`; do not assume the
example index stays valid. `--channel` and `--fx-channel` default to 1 and 9. Use a disposable sound;
the task changes its live controls and leaves the last scenario settings in place without saving.

The 23-second scenario starts with a held A3 and one full reset. It adds/removes each sub, mutes
and restores a fundamental, changes notes and waveform families, exercises sync and cross modulation,
changes noise level, and releases/retriggers across a rest. Oscillator state is retained after the
initial reset. Event deadlines use absolute captured-frame targets; the log records host event
positions and send times. Cleanup releases every scenario note, including after acquisition failures.

Each new output directory contains `audio.wav`, `events.json` and `run.json`, including the exact
scenario, source revision/working-tree state, declared firmware hash, device and audio format.
Failed runs retain available audio and errors. Existing output directories are refused.

The diff verifies capture integrity and compatible setup, then finds one integer-sample offset
from the initial noise-bearing held sound. It never aligns later sections independently, fits gain,
resamples, or changes the recordings. `comparison.json` reports per-channel sample errors, exact
sample percentages, section RMS/peak/spectral measurements and host event-timing differences.
`difference.wav` contains candidate minus baseline, with any clipping counted in the report.
Listen alongside both originals; the diff recording can be much quieter than either input.

This is a manual review task: successful comparison returns zero even when the recordings differ.
Invalid/incomplete captures or unavailable alignment are errors. No tolerance is approved and no
baseline is updated automatically. A changed predefined scenario requires a new baseline.
Run the unchanged firmware twice first: MIDI arrival timing can change later oscillator phase,
even with the same host script. Relative phase can also change mixture levels and spectra
substantially; a numerical difference alone does not establish a DSP regression. Level/spectral
measurements complement the sample diff; event
logs describe host timing, not the device's exact processing sample. Keep the deterministic host
tests as the strict sample-equivalence check for DSP refactors.

## Evidence and failures

Runs go under ignored `out/regression/`, or a new directory supplied with `--out`. Existing output
directories are refused. `run.json` records the revision, working-tree state, declared firmware hash,
OS, durations, planned cases and capture completion/error. Each case retains its settings, WAV and
capture metadata; firmware/device cases also retain the input events. Audio comparisons are marked
`not run` throughout.

Acquisition checks detect empty/truncated recordings and wrong sample rates, simulator halts,
ineffective simulator setup and physical audio stream errors. Start/end frame positions identify
the sustained recording window; setup audio cannot compensate for missing sustained samples.
The requested duration must be present within one audio block (32 frames for host/emulator,
256 for USB capture). Failed recordings and their available events are retained. Silence is a valid recording. No
threshold checks decide whether an audible source is loud enough, tuned correctly or unclipped.
A disconnected or absent device is an acquisition error, not successful qualification.

The simulator's fresh state, host voice initialization and physical retained voice state differ.
Captured audio does not yet establish sound equivalence, hardware CPU capacity or analog fidelity.

## Compact measurements and assertions

First acquire a run, then measure its existing sustained recordings:

```sh
mise run regression:dsp --suite reference --out out/regression/candidate
mise run regression:measure out/regression/candidate --out out/candidate-measurements.json
mise run regression:assert out/regression/candidate --expectations tests/reference/recorded/assertions.json
```

The recorded-reference expectations retain all 315 cases; routine CI selects the 311-case
`reference` suite. The policy checks waveform pitch against standard
tuning (twelve-tone equal temperament, A4 = 440 Hz), independently of reference detuning, plus supported
harmonic balance and levels, isolated sources and middle-position ratios, noise, phase-aware mixtures and
modulation spectra below 4000 Hz. Direct reference band comparisons from 4000–24000 Hz
are retained as diagnostics because the reference filter and sample-rate-dependent processing
color them. They do not affect pass/fail. All seven raw spectral bands remain measured;
standard pitch, comparable levels, harmonic balance and independent signed-engine checks remain strict.
Noise uses strict 200–4000 Hz level/ratio checks and a strict isolated 20–200 Hz aggregate.
Its individual low bands remain diagnostic: 20–80 and 80–200 Hz for isolated noise,
20–80 Hz for noise-bearing mixtures. Remaining mixture bands propagate the existing
noise spectral budget through the noise contribution; tonal and full-band level bounds
are unchanged. A longer multi-history noise-spectrum check runs on the host, while
simulator captures keep their two-second analysis windows.
[Reference provenance and budgets](../tests/reference/recorded/README.md) describe
the output paths, calibration and exact coverage. Reference agreement is not physical OB-1 fidelity.

CI first runs the host mechanics and synthetic checker tests, then `mise run test:reference` acquires
the DSP reference suite and checks this policy. Remaining mismatches fail normally; CI can be red until
they are resolved. Reports retain every checked property, not just the first failing case. No audio
plugin or reference WAV is needed in CI. Firmware and device acquisitions use the same assertion
command on their completed capture directories; the declared route/OS and source hashes remain in
the report. The policy currently covers OS 1.54 only and performs no flashing or hardware acquisition.

Full catalogues contain the reference dependencies. For smaller selections, include the VCO1 full
saw at each played note when checking relative wave levels. Isolated mixer sources need `mix-6-0-0`,
and a middle-position case also needs its isolated ON counterpart. Missing dependencies are errors.
The five-case smoke selection already contains the needed wave anchor; its mixture checks use the
committed isolated-source measurements to allow coherent reinforcement and cancellation.

The internal measurement JSON has `format: 2`, `role`, `target`, `stock_os`, `source` and a `cases` object
keyed by catalogue ID. Measurement exports have `role: "measurements"`; they cannot be used directly
as expectations. Each case retains `settings`, `observation`, `channels`, `frames`, `wav_sha256` and
`metrics`. Source metadata records the capture manifest hash, revision, declared image hash and NumPy
version. Changing measurement semantics requires a format-version change, preventing old fixtures
from being interpreted as new measurements. Format 1 exports/absolute fixtures must be measured
again; the committed reference remains original format-1 evidence with its two documented frequency
corrections, imported explicitly by the version-2 reference policy. Keep original recordings locally.

Measurements cover each channel independently: `rms_dbfs`, linear full-scale `peak`, signed `dc`, and
`band_rms_dbfs` in the ordered bands 20–80, 80–200, 200–1000, 1000–4000, 4000–8000, 8000–16000 and
16000–24000 Hz. Digital zero is represented as −240 dBFS. Band levels integrate a one-sided Welch
power spectrum with a periodic Hann window, up to 8192 samples and 50% overlap, without detrending.
No independent-noise sample equality is assumed.

Isolated `wave-` cases also have `pitch_hz` and a 256-point, unit-RMS signed `cycle`. Frequency comes
from a zero-padded Hann spectrum's strongest peak within ±2 % of the known played note, with local
interpolation. Reject a boundary peak or a candidate more than 20 dB below the strongest component;
this keeps a stronger overtone from becoming an octave error without treating FFT leakage as a
fundamental. This estimator is for the isolated-wave catalogue, not arbitrary polyphonic/modulated recordings.
The cycle projects up to 64 harmonics below 0.475 times the sample rate. The fundamental fixes its
time origin. It does not invert polarity, reverse time or resample the recording. At least eight
cycles are required. Symmetric signals cannot distinguish a polarity flip from a half-cycle shift;
asymmetric anchors protect orientation. Whole-mixture levels and spectra are measured without
aligning individual oscillators or assuming their relative phases agree.

Analysis uses the beginning of each verified sustained window for the requested duration rounded
down to a 32-sample block. Target/OS, settings, observation point, channel count and analysis length
must match the fixture. Use separate fixtures for DSP, SSI and USB, whose output paths differ.

For a separate absolute fixture, review the source and measurements, set `role: "expectations"`, describe
their origin and tolerance rationale in `source.description`, and replace each case's `metrics`
with `limits`. For example, a mono level limit has this structure (illustrative values only):

```json
{"limits": {"rms_dbfs": {"min": [-34.1], "max": [-33.9]}}}
```

Bounds may contain `min`, `max` or both. Their dimensions match the measurement: one value per
channel, or one row of seven band values per channel. Missing expectations, empty assertions,
unknown metrics, wrong dimensions, invalid numbers and incompatible captures are errors, not skips.
A larger fixture may serve a smoke subset, but every selected case needs expectations.

A shape assertion additionally uses `cycle: {"values": [[...]], "max_rmse": ...}` with 256 signed
values per channel and a nonnegative normalized-cycle RMS-error limit. Shape assertions also require
independent `pitch_hz` and `rms_dbfs` limits, so phase/shape normalization cannot hide pitch or gain
changes. Set tolerances per property from justified requirements or repeatability measurements;
never automatically accept candidates, widen limits, or absorb an unexplained polarity difference.

Assertion reports go into a new `out/assertions/` directory, or a new directory supplied by `--out`.
They retain actual measurements and each checked property's bounds/result. Failed checks and invalid
inputs return nonzero. Reference high-frequency comparisons have `status: "diagnostic"`, actual values
and reference bounds; the console summarizes the diagnostic count. The same reporting retains
reference signed-cycle errors, uncalibrated absolute wave levels, intermediate saw-family level
differences and unsupported low-band reference targets. These entries do not fail a case or run.
Neither checking nor measuring rewrites captures or fixtures; existing outputs
are refused. CI retains generated regression audio and JSON reports on failure as temporary artifacts,
excluding firmware images. No baseline-generation step runs in CI.

The reference-policy fixture has `format: 2`, `role: "expectations"`, `source.description`, a relative
reference path and SHA-256, per-target observation/channel/OS/fixed-gain/polarity calibration, and explicit
per-property budgets. It reads the immutable measured case settings and metrics directly rather
than copying them into three target fixtures. Gain and level are checked independently of signed
unit-RMS shape. Source identities, settings and windows must match; malformed references, unknown
tolerances, missing anchors and digest mismatches are errors, never skips. Updates to measurements,
path calibration or budgets must be reviewed explicitly. No current render can update them.

For reference signed-cycle comparison, the required integer `cycle_polarity` is −1 for DSP and
device USB, +1 for simulator SSI. Retained asymmetric recordings establish the reference output's
falling-saw orientation, opposite to DSP/USB. The stock SSI output applies a negative main-mix
gain; the USB handoff retains the main mix polarity. The fixed convention appears in each report.
It is never selected by trying both signs. For −1, a half-cycle shift follows inversion to restore
the canonical cycle's fundamental phase origin. Raw measurements and recordings remain unchanged,
and a new inversion or time reversal remains visible in the diagnostic. Reference harmonic balance
uses unit-energy magnitudes below 4000 Hz and the existing 0.25 error budget; it does not demand
the unresolved path phase. Independent engine snapshots reject signed waveform/phase changes.
Generic absolute fixtures retain their strict cycle checks. The separate stock 5 Hz high-pass
response is not compensated.

Reference absolute waveform levels gate only at the calibrated A3 pitch, excluding intermediate
saw positions. Comparable same-note levels relative to the full saw remain strict at every register;
intermediate saw reference levels are diagnostic because their construction differs. Their pitch
and harmonic balance still gate. Pure SYNC replaces its quiet 20–80 Hz reference target with a strict
ceiling at the existing −60 dB floor relative to the fixed reference saw; 80–4000 Hz and full-band
levels remain strict. XMOD reference bands and levels are unchanged.

The control assignments and AMP behavior follow Elektron's *Digitakt User Manual*, OS 1.51,
chromatic mode section 8.5.1, AMP/filter sections 11.4–11.6 and MIDI Appendix B. The source-control mapping follows this project's
[architecture](architecture.md); the pinned firmware is the authority for setup readbacks.

## Firmware block-clock scenario

Build the diagnostic OB-1 mod with `mise run build:digitakt:scenario`, then include that mod
in place of the ordinary OB-1 mod when assembling the test firmware. Normal builds exclude the
scenario entirely; the shared sound engine has no scenario code.

For sample comparisons across DSP refactors, add `--firmware-scenario` to the existing
`regression:device:scenario` task. The installed image must support OB-1 CC 12. The capture prepares
a held stock AMP with muted sources, sends one CC 12, and records the complete 23-second scenario.
The device schedules pitch, source switches and gates at 1500 audio blocks per second; later host
MIDI timing cannot move its events. One full DSP reset starts each run. The capture cancels the
scenario with CC 9 and releases the held note on completion or failure.

This mode exercises the same parameter decoder, firmware pitch conversion and shared DSP while
keeping stock AMP/filter/FX processing fixed. Its note releases are DSP gates; stock envelope
retriggering and live MIDI dispatch are separate tests. It does not update saved controls.

The capture identifies the start from the first stereo sample above 64 native 24-bit sample steps
after the trigger, requiring a quiet preceding 250 ms and a full subsequent recording. A missing
onset or incomplete run fails acquisition. This is an alignment marker, not an audio acceptance
tolerance. Comparison still uses one global integer alignment and retains all subsequent differences.
Captures record their clock mode; comparing a firmware-clock run against a host-MIDI run is rejected.
No repeatability or regression pass threshold is automatically approved.
