# Reference measurements

Measured output from the reference, version 1.0, in REAPER 7.82/macOS arm64, captured on
18 October 2026. The 315 settings match the regression catalogue: 60 isolated waves,
243 mixer/noise combinations and 12 modulation cases. These are reference
measurements with `role: "measurements"`, separate from the active expectations in
`assertions.json`.
Raw recordings remain local; their SHA-256 hashes identify the measured audio.

## Capture and measurement

The enabled demo instance uses 48 kHz stereo output, 2x oversampling and maximum
internal buffer size 32. Offline host block size was not independently established.
Effects and Vintage are off, the voice is mono, noise is white, filter cutoff is
maximum with zero resonance, and the VCA has zero attack/release and full sustain.
The filter/amplifier remains in the audio path; there is no pre-filter output tap.
The complete reference parameter baseline is retained in `capture-settings.json`.

Each five-second float32 recording contains a velocity-100 note from seconds one
through four. Analysis uses frames 96000–191999, after one second of held-note
settling. Output Gain stays at normalized 0.5, displayed −13.98 dB; track and master
gain are unity. No per-case gain fitting, resampling, sign inversion or time
reversal is applied. VCO2 fine tuning is adjusted once to relative unison at A3;
the measured correction and verification are retained in the source metadata.

Saw morph uses wave/63; pulse morph uses 0.93 × (wave−64)/63, ending at the
previously measured square setting. Mixer values are 0, 10^(−3/20) and 1; the native
middle-position display reads −3.01 dB. Per-case reference control settings accompany
the numeric measurements. All requested post-render parameter readbacks matched.

RMS, peak, DC and seven spectral bands use the regression measurement method.
Isolated waves include a fundamental estimate and signed 256-point unit-RMS cycle,
with up to 64 harmonics below 0.475 times the sample rate. The fundamental fixes
the cycle's phase origin. Shapes, pitch and absolute level remain separate metrics.

For the two A1 narrow-pulse endpoints, the original strongest-peak estimator selected
the second harmonic. Their pitch and cycles are corrected using the fundamental
within 2 percent of the known note. Source metadata retains both frequency values
and the method; no raw audio was changed. Measurement format 2 now uses the known
note for isolated-wave estimation. All other recordings retain the original export.

## Using the evidence

The assertion policy pins the measurement SHA-256 and declares fixed gains for DSP,
simulator SSI and device USB. It never refits gain from the run being tested. The
absolute saw anchor is checked separately, so relative levels cannot hide an overall
gain change. DSP uses the left reference channel; stereo paths check each channel.
Only OS 1.54 firmware/device observations are covered by this policy.

Signed cycles use a fixed `cycle_polarity` calibration when compared with the reference output:

| Capture boundary | Comparison polarity |
|---|---:|
| Shared DSP output | −1 |
| Simulator SSI output | +1 |
| Device USB output | −1 |

Retained asymmetric waves from both VCOs at A1/A3/A5 establish the reference's falling-saw
orientation, opposite to DSP and USB. Repeated reference captures and filter, oversampling and
sample-rate controls retain it. A read-only stock-firmware trace confirms that SSI applies a
negative main-output gain while the USB handoff preserves the main mix polarity. This establishes
the measured output conventions, not internal reference-oscillator or physical Oberheim polarity.

For −1, inversion of an already canonicalized cycle includes a half-cycle shift to restore its
fundamental phase origin. Only the signed-cycle comparison uses this transform; raw recordings,
exported measurements and all other checks retain their original values. Reports record the fixed
convention. A new inversion or time reversal still fails. The separate stock 5 Hz high-pass response
is not compensated.

Supported reference differences are ordinary failures. There is no automatic sign selection,
time reversal, pitch correction, expected-failure list or threshold widening. Register-dependent levels,
noise levels and low-frequency waveform/modulation differences can fail. Passing acquisition does not
mean passing reference assertions, and passing levels does not establish matching
modulation spectra.

The limits are engineering comparison budgets, not statistical confidence intervals:

| Property | Budget and purpose |
|---|---|
| Absolute A3 saw anchor | ±0.1 dB around the frozen output-path calibration. |
| Isolated-wave pitch | ±1 cent from twelve-tone equal temperament, A4 = 440 Hz. Reference detuning is not a tuning target. |
| Wave absolute/relative RMS | ±0.2 dB; retains the previous relative-level budget. |
| Signed unit-RMS cycle | RMSE ≤0.25 after fixed output-path polarity calibration. |
| Isolated FUND/sub/noise RMS | ±0.2 dB, both absolute and relative to the isolated VCO1 saw. |
| Source middle/ON ratio | ±0.05 dB around the measured ratio. Broader shape/pitch mixer mechanics remain independently tested. |
| Noise 200–4000 Hz level and middle/ON ratio | ±0.25 dB, allowing finite noise-window variation. |
| Noise/modulation spectral bands below 4000 Hz | ±1 dB, after the fixed path calibration. |
| Modulation RMS | ±0.5 dB. Spectral checks remain independent. |
| Mixed-source bounds | ±0.5 dB around phase-aware energy bounds, allowing finite-window variation. |
| All sources OFF | No more than 6 dB above the calibrated reference background RMS. |

For spectral checks, each band is expressed relative to the fixed reference A3 saw
and clamped at −60 dB before comparison. This prevents tiny background differences
from dominating while still catching a new band above that floor. Mixtures use
the reference's isolated sources at the selected levels: coherent tonal RMS lies
between `max(0, 2*largest − sum)` and `sum`; noise adds in power. The same bounds
apply to spectral-band energy. A single arbitrary unison phase is not an oracle
for whole-mixture RMS. This bounds check complements the existing engine additivity
and continuity tests; it does not identify every possible mixture defect.

Direct reference comparisons in 4000–8000, 8000–16000 and 16000–24000 Hz are
diagnostics. The fully open reference filter remains active, and changing oversampling
or output sample rate changes these bands across tonal and noise sources. They cannot
establish an isolated oscillator defect. Modulation, noise and phase-aware mixture
reports retain their actual values and reference bounds with `status: "diagnostic"`;
those entries do not affect case/run status. All seven raw bands remain in measurement
exports for every case. No recorded values, fixed gains or budgets are refitted.

Band assertions below 4000 Hz remain strict. The dedicated noise band-level check uses
200–4000 Hz so it does not gate an excluded band indirectly. Full-band RMS, signed cycles,
standard pitch, mixer ratios and silence remain checked; levels and cycles are not tests
of isolated frequency regions. Independent engine aliasing, band-limiting and headroom
checks, and separate absolute spectral fixtures, retain their existing behavior.

The gains are measured once from the unchanged engine's A3 full-saw recordings and
recorded with capture hashes. They compensate the declared output paths, including
the chosen reference master gain. Filter/coupling or modulation differences are
not calibrated away. Absolute waveform levels and independent spectral checks can
therefore expose those differences even when some relative levels agree.

`repeatability.json` records per-channel minima, maxima and spans across the
catalogue capture and three additional captures of ten selected cases. These
same-instance observations do not cover different hosts, instances or
long-term drift. Noise-only repeats are circular shifts of the same recorded
sequence; their stable RMS does not establish independent-noise repeatability.
Free-running relative oscillator phase changes mixture loudness substantially,
so one mixture recording is insufficient for a tight fixed RMS expectation.

These measurements establish reference behaviour, not physical Oberheim fidelity.
