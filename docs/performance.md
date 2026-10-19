# Performance baseline

The 17 October 2026 baseline starts at revision `145adfc8b78a7715643d51006721ff29f5c8de8e`.
Measurements use the same compiler, dependencies and workloads before and after the corrections.
Firmware, source snapshots, executable benchmarks, stimulus scripts and raw reports are retained
locally under `capture-local/performance-20261017/`; they are not distributed with the firmware.
The initial-correction sections record firmware `0ff0295d…`. The later reference calibration below
records the reference-calibrated sound model against both that correction and the original baseline.
The later pitch-gain section measures the additional source attenuation separately.

## Initial correction: sound-engine instructions

`mise run bench` counts ColdFire instructions for eight voices, 32 samples per block, subtracting
four warm-up blocks from twenty blocks. The compiler is m68k-elf GCC 16.2.0 with the configured
`-mcpu=54455 -O2` options. The first case is one saw, not the default two-source sound. The selected
heavy case is not an exhaustive worst-case search.

| Eight-voice workload | Baseline instructions/block | Corrected instructions/block |
|---|---:|---:|
| One saw, other sources off | 15,554 | 15,538 |
| Both VCOs and subs | 24,090 | 24,059 |
| Selected SYNC, XMOD and noise | 39,982 | 39,923 |
| Muted VCO2, BOTH and noise | 38,783 | 38,339 |
| Muted VCO2, XMOD and noise | 38,783 | 38,629 |

The last two cases extend the same benchmark with VCO1 fundamental/sub ON, VCO2 OFF, noise ON,
and the heavy case's waveforms and pitches. They are preserved with the local measurement scripts.
The old muted BOTH case incorrectly skipped sync, so its before/after sound is intentionally different.
Counts can decrease despite restoring work because sync changes the oscillator's event sequence.

The sound-engine benchmark excludes wrapper parameter reads and pitch lookup. An isolated ordinary
pitch lookup increased from 13 to 14 instructions; the cold octave-extension path costs 30–42
instructions for the measured high notes, instead of the old incorrect constant-pitch plateau.
Whole-firmware measurements below include the wrapper. Instruction counts are not CPU cycles or
hardware voice-capacity measurements. The stock-load percentages printed by the benchmark use a
historical estimate and must not be substituted for measured firmware timing.

## Initial correction: complete-firmware simulator

Both OS 1.54 firmware images booted from fresh state before the same controlled load. Each case
imposes eight sustained voices at the actual guest render handoff, while the firmware's DSP,
filters, effects, scheduler and interrupts continue executing. The 100 ms cases each cover 149
render periods. The stock filter/AMP/FX state is the same fresh-boot state; this is not a comprehensive
sequencer, LFO or UI stress test.

| Eight-voice workload | Baseline modeled busy | Corrected modeled busy | Baseline worst margin | Corrected worst margin |
|---|---:|---:|---:|---:|
| one-saw | 75.26% | 75.23% | 24.68% | 24.95% |
| both-vcos-subs | 81.75% | 81.72% | 16.94% | 17.41% |
| sync-xmod-noise | 98.38% | 97.50% | 1.52% | 2.01% |

There were no missed render deadlines or strict violations during these loaded cases. The heavy
case improves, but its remaining 2.01% worst-case margin is thin and below the simulator checker's
10% reserve criterion. Eight heavy voices are not qualified for hardware use by this result.

| Fixed 100 ms run | Baseline total instructions | Corrected total instructions | Change |
|---|---:|---:|---:|
| one-saw | 17,205,913 | 17,209,815 | +0.023% |
| both-vcos-subs | 17,916,982 | 17,921,145 | +0.023% |
| sync-xmod-noise | 19,588,354 | 19,663,860 | +0.385% |

These fixed-duration totals include idle and control work; they are not instructions per audio block.
Their small increases coexist with lower modeled render cycles. In a scoped heavy-case count,
73,695 of the 75,506 extra instructions are idle work. Active instructions rise by 0.0093%
(19,453,620 to 19,455,431), despite the lower cycle cost. The isolated DSP instruction counts
and full-firmware modeled CPU/deadline measurements improve; not every instruction total decreases.

A one-second sustained heavy-case follow-up covers 1,499 render periods per image. Modeled busy
falls from 98.77% to 97.89%, and worst deadline margin improves from 1.37% to 1.96%, with zero
misses or strict render faults. Active instructions rise from 195,362,531 to 195,380,684 (+0.0093%);
idle instructions rise from 1,026,455 to 1,760,683. The longer run confirms improved cycle cost,
but also confirms that the reserve remains below 2% in this workload.

Timing uses the emulator's MCF54418 instruction model at 250 MHz, assumed branch prediction and
zero-wait memory. Cache, SDRAM and physical bus behavior are not hardware measurements. Both fresh
boot checks encounter the same existing strict read of unmapped address zero at PC `0x40068f4c`;
the complete firmware checker therefore does not pass cleanly, despite successful boot/UI/audio.
This pre-existing diagnostic is separate from the zero-fault loaded render cases.

The matched 4.15-second fresh UI/control script has modeled busy load 63.36% → 63.39%
and worst deadline margin 35.82% → 35.74%. Total instructions fall from 666,706,347 to
666,603,636. These small control-path changes are recorded separately from the loaded render
improvements; the measurements do not establish zero regression in every counter or workload.

The corrected firmware's raw simulated oscillator output rises through nominal MIDI 84/85/96/120
at approximately 1046.21/1108.26/2092.84/8371.25 Hz and reaches the 12 kHz ceiling at MIDI 127.
The baseline remains near 1046.21 Hz at MIDI 96. Lower lookup values are unchanged. This verifies
the device pitch-path correction, not physical analog tuning.

Firmware SHA-256 values:

- Baseline OS 1.54: `9f27375ff89977c7fa8b013cbe79b49ed1c7f79154724de04d75df3fa4509c37`.
- Corrected OS 1.54: `0ff0295dc583478fd7325af118f8321cef7ed5d97cb583da064644944d6aa0c5`.
- Corrected OS 1.53: `a6d10445d0f20e72d082eda69e1c3e6a51363df87d6ea2c3667fe1b408378ac7`.

Both supported OS builds link and patch with the original non-main sections preserved. Complete
simulator timing above is OS 1.54 only; physical playback, USB levels and device timing remain unverified.

## Repeating a comparison

Keep the baseline revision, firmware hashes, toolchain, dependency revisions, voice count, pitches,
source levels, waveform settings and modulation modes with every result. Run the host regressions
before comparing instructions. For complete firmware, repeat identical simulator stimuli on the
baseline and candidate firmware; report modeled cycles, render deadlines, strict faults and audio,
not only host wall-clock time. Include high pitches and muted modulation sources when their behavior
changes. A necessary correctness change may alter event density; explain that separately from an
unchanged-workload regression. Preserve output headroom when evaluating sound changes.

The host checks include 240 one-second maximum-mix conditions across six waveform settings,
five pitches, all four modulation modes, aligned sources and mixed-shape fifths with noise ON.
The first correction's highest peak in that matrix was −4.24 dBFS. All four previously clipping
campaign cases were rerendered without rail hits. This is bounded test coverage, not a proof for every
possible control transition or physical device workload.

## Reference calibration

The calibration target is the measured reference oscillator output. The same 64-position
saw curve and fundamental-level coefficients are used across pitches and VCOs. Compact reference
cycle/level fixtures, middle-position checks and noise checks run with the normal host tests.
Physical circuit uncertainties remain separate from this selected reference target.

| Calibration check | Previous model | Reference-calibrated model |
|---|---:|---:|
| Median normalized saw-family cycle RMSE, 70 pitch/VCO cases | 0.4841 | 0.1143 |
| Median absolute relative waveform-level error | 0.424 dB | 0.039 dB |
| Maximum relative waveform-level error in those cases | 6.428 dB | 0.429 dB |
| Checked A3 waveform-position level error | Up to 5.986 dB | At most 0.052 dB |
| Middle oscillator attenuation | −6.56 dB | −3.00 dB |
| White noise 200–8000 Hz / full-saw RMS | −19.383 dB | −20.420 dB (reference −20.408 dB) |
| White-noise spectral tilt, comparison band | +0.003 dB/octave | −0.516 dB/octave (reference −0.468) |
| Slow exponential-modulation mean-rate ratio | About 4.59× | About 2.776× |

Cycle comparisons allow phase and polarity, with no time reversal or per-pitch refitting. The
largest remaining level residual occurs at the upper test pitch. Reference filter/amplifier
coloration remains in the recordings. Noise calibration matches level and broad color in the
stated band, with about 1.28 dB binwise PSD fit residual; it does not reproduce every spectral bin.
The XMOD depth is ±3.921875 octaves; 16 held-out cases have median normalized spectral-band RMSE
0.07177 → 0.05465, with 11 improving. Higher-register modulation spectra retain differences.

At that calibration, waveform gain and output trim were folded into constant mixer coefficients. Full saws omit half
scaling and nonexistent middle-corner corrections; the paired optimization matches 4,096,000
frozen generic outputs and oscillator states exactly, with 12 persisted golden checks. A full-period
32-bit generator supplied the high 16 noise bits, followed by the fitted noise-only filter. Eight
track seeds were checked for distribution, spectral flatness, lag and cross-track correlation.
Those historical measurements predate the shared output smoother. The current engine retains the
generator and gain but applies only shared smoothing, avoiding duplicate noise attenuation.

| Eight-voice DSP workload | Original baseline | Initial correction | Reference calibration |
|---|---:|---:|---:|
| One saw | 15,554 | 15,538 | 13,817 |
| Both VCOs/subs | 24,090 | 24,059 | 20,494 |
| Selected SYNC/XMOD/noise | 39,982 | 39,923 | 39,381 |

All host suites and AddressSanitizer/UBSan checks pass. The 240 maximum-mix cases peak at
−4.28 dBFS, slightly below the initial correction's −4.24 dBFS. The final waveform and noise
calibration adds no increase in the checked maximum level. Both supported OS images build/link/patch;
raw measurements, source hashes and simulator stimuli are retained locally.

The final OS 1.54 image repeats the same fresh boot/control script and controlled eight-voice
workloads as both comparison images. No workload settings, dependency revision or timing model
were changed. The modeled 100 ms results are:

| Workload | Original busy | Initial correction busy | Reference calibration busy | Original worst margin | Reference calibration worst margin |
|---|---:|---:|---:|---:|---:|
| One saw | 75.26% | 75.23% | 73.85% | 24.68% | 27.08% |
| Both VCOs/subs | 81.75% | 81.72% | 79.07% | 16.94% | 21.50% |
| SYNC/XMOD/noise | 98.38% | 97.50% | 91.93% | 1.52% | 8.43% |

The one-second heavy run completes 1,499 render periods with no missed deadlines or strict render
faults. Modeled busy decreases from 98.77% originally and 97.89% after the initial correction to
92.28%. Worst render cycles decrease from 164,399 / 163,425 to 152,959; margin improves from
1.37% / 1.96% to 8.23%. Active instructions decrease from 195,362,531 / 195,380,684 to 182,641,329,
and total instructions from 196,388,986 / 197,141,367 to 189,077,187. Even with more idle work,
both active and total instruction counts improve. The margin is still below the checker's 10%
recommended reserve and does not certify physical eight-voice capacity.

Fresh UI/control busy load is 63.25%, versus 63.36% originally and 63.39% before calibration.
Its total instructions are 666,018,051 and worst render margin is 36.05%; both improve relative to
each comparison image. The same pre-existing strict boot read of address zero persists, so the
complete checker still reports failure for that diagnostic. No new strict violation or render miss
occurs. MIDI 127 retains the verified 12 kHz ceiling. Timing remains a simulator estimate, using
the same zero-wait memory and branch-prediction assumptions described above.

Final firmware SHA-256 values:

- OS 1.54: `4e8c9a14547a7a031145a099761fc3c97ba3330f9586320403551bfb1d68857c`.
- OS 1.53: `c6c3a484b8ea2aba9233135dc8f3b4d6138d01988e151db18654fc43138599dd`.

OS 1.53 is build/link/patch checked; complete simulator timing is OS 1.54 only. Physical playback,
USB level matching and hardware CPU timing remain unverified.

## Continuous muted oscillators

The 18 October 2026 correction makes mixer settings affect output gains only. Both oscillators,
their sub-octave flip-flops and selected modulation paths continue advancing throughout an active
voice. The matched baseline is revision `3398672`, whose
sound engine is identical to the previously flashed `cde5e99` build.

The linked Digitakt renderer was measured with eight voices at MIDI 60, four warm-up blocks and
sixteen measured blocks, using m68k-elf GCC 16.2.0, `-mcpu=54455 -O2`, the same dependency revisions,
and OS 1.54. These counts include parameter decoding and pitch lookup. The corrected OS 1.53
renderer gives the same counts.

| Eight-voice workload | Before instructions/block | After instructions/block | Change |
|---|---:|---:|---:|
| One audible full saw, other outputs OFF | 18,152 | 23,065 | +4,913 (+27.1%) |
| Default two saws | 24,777 | 24,649 | −128 (−0.5%) |
| Both sources/subs, SYNC, XMOD and noise | 43,715 | 43,627 | −88 (−0.2%) |

The increase is largest when one or both VCO outputs are OFF. A separate engine-only comparison
at fixed C4/G4-area pitches measures +35.9% for one audible full saw, +45.0% for one audible
triangle, and +78.7% for noise alone with both VCO outputs OFF. These percentages exclude the integration
and stock track processing. Selected routing continues even if both VCO outputs are muted. Removing
branches slightly reduces instruction counts when the same work was already running.

A matched complete-firmware simulation on OS 1.54 imposes eight sustained voices while stock track
processing, scheduling and interrupts continue. Each case covers 100 ms and 149 render periods,
with the same fresh-boot track state. Timing uses the emulator's 250 MHz MCF54418 cycle model.

| Eight-voice workload | Before modeled busy | After modeled busy | Before worst margin | After worst margin |
|---|---:|---:|---:|---:|
| One audible full saw | 76.80% | 80.19% | 24.10% | 20.39% |
| Default two saws | 81.90% | 81.97% | 18.64% | 18.59% |
| Selected heavy sound | 97.82% | 97.88% | 2.17% | 2.11% |

All six runs have zero late renders and zero strict violations. The heavy case still has only about
2% modeled reserve; this does not qualify eight heavy voices for hardware. Instruction counts and
modeled cycles are distinct: fewer instructions do not guarantee less modeled busy time. These short
runs are comparisons, not physical CPU measurements, cache/memory timing validation or safe voice limits.

The 240-case maximum-mix headroom suite retains its −4.28 dBFS peak. Six matched static renders
(one VCO1, one VCO2, default, all sources, heavy and noise-only) are sample-identical before/after.
Repeated OFF/−3 dB transitions at equal oscillator pitches preserve phase and sub-octave alignment;
steady mixed levels remain within 0.02 dB across the tested mute durations. Intentional detuning can
still produce beating and changing cancellation as the oscillator phases evolve.

## Two-instance device timing

Physical Digitakt mk1 measurements on 18 October 2026 compare revision `3398672` before the
continuous-oscillator correction with `0d2dfc0` after it, on OS 1.54. Both diagnostic builds use
Core 4.0, machine-pages 1.2, m68k-elf GCC 16.2.0 with `-mcpu=54455 -O2`, and
[DigiHealth's timer diagnostics](https://github.com/irpina/digihealth/blob/b2c3cf16eaaf0ed638214858a487c955b5efd5cc/tools/digiusb.py).
FAST AUDIO and the SYSTEM INFO overlay are OFF in both builds.

Two OB-1 tracks hold MIDI note 57 at velocity 100, with full saws at unison. Six stock-machine tracks
are idle and the transport is stopped. Each case collects twenty timer snapshots, about 30,000
renders. Decoded source settings and live filter, AMP and other stock parameter values match across
each pair. The default mixes VCO1 ON and VCO2 at −3 dB. The heavy case enables both fundamentals,
both subs, noise, sync and cross modulation, all ON.

| Two-track workload | Mean render before | Mean render after | Peak margin before | Peak margin after |
|---|---:|---:|---:|---:|
| VCO1 alone | 82.49% | 83.25% | 11.10% | 8.88% |
| Default two saws | 83.91% | 83.78% | 9.09% | 9.22% |
| Both VCOs/subs, sync, cross modulation and noise | 89.20% | 89.05% | 4.42% | 4.47% |

Mean render load is render time divided by the measurement window. Peak margin is the fraction of
the 32-sample block budget remaining after the longest observed render. Keeping muted oscillators
running adds about 0.76 percentage points of average render load in the VCO1-only case. The other
two workloads are essentially unchanged; the small decreases do not establish a general speedup.

These are observed margins with identical diagnostic overhead, whose absolute cost was not measured
separately. Busy sample tracks, sequencer activity, locks, LFOs and active UI work were not exercised.
The roughly 4.5% heavy-case reserve is limited; these measurements do not establish a safe voice
count or an exhaustive worst case.

Diagnostic firmware SHA-256 values:

- Before, OBP0: `ab4f93962d1ea586ead4f4ec07b6d3249dcfda25118f2792df91b4ebd51d792a`.
- After, OBP1: `1b90152d9bf7880d0ccfec6d0808c174c6c184bb0a9c3cbe23da9ea27d5b2c92`.

## FAST AUDIO on the device

The same physical Digitakt mk1, running the same OBP1 firmware above, was measured with DigiHealth's
FAST AUDIO OFF and ON. This isolates the setting from firmware layout and source changes. Both
OB-1 tracks use full saws at unison with fundamentals, subs, noise, sync and cross modulation all ON.
Six stock tracks are idle, transport is stopped and track 2's mixer page stays visible. Each mode
collects twenty timer snapshots, about 30,000 renders, with the SYSTEM INFO overlay OFF.

Source values and live filter, AMP and other stock parameters match exactly across the captures.
Every snapshot confirms the requested FAST AUDIO state, with no FAST AUDIO watchdog fault.

| FAST AUDIO | Mean render | Peak render | Peak margin | Mean CPU |
|---|---:|---:|---:|---:|
| OFF | 89.10% | 95.77% | 4.23% | 92.66% |
| ON | 80.16% | 85.39% | 14.61% | 83.32% |

Average render load falls by 8.93 percentage points and the observed peak margin increases by
10.39 points. Mean CPU includes control work as well as the render, using DigiHealth's idle-time
accounting. The reduction is substantial for this workload, so including
[DigiHealth](https://github.com/irpina/digihealth#install) and enabling FAST AUDIO is recommended.

All mixer sources and both modulation modes ON provide a demanding comparison, not an exhaustive
worst case: other pitches, shapes, locks, busy sample tracks and sequencer/UI activity can change
the cost. Audio equivalence was not re-measured in this timing test.

## Pitch-dependent oscillator gain

The 18 October 2026 gain correction is measured against revision `3226151`, using the same
GCC 16.2.0 options, eight voices and 32-sample blocks. A fixed amplitude curve attenuates audible
oscillator fundamentals at block rate; oscillator evolution, modulation taps and audio-rate
processing are unchanged. The existing three benchmark workloads give:

| Eight-voice workload | Before instructions/block | After instructions/block | Before modeled cycles/block | After modeled cycles/block |
|---|---:|---:|---:|---:|
| One saw | 18,782 | 19,006 | 23,968.69 | 24,696.69 |
| Both VCOs/subs | 20,366 | 20,782 | 27,064.69 | 28,325.31 |
| Selected SYNC/XMOD/noise | 39,292.88 | 39,708.88 | 53,607.00 | 54,819.38 |

Instruction increases are 1.19%, 2.04% and 1.06%; modeled cycle increases are 3.04%, 4.66%
and 2.26%. An additional 32-workload matrix spans about 55 Hz to 12 kHz, all modulation modes,
both fundamentals/subs and noise. Its largest instruction increase is 3.74% and largest modeled
cycle increase is 4.15%, both near 7.19 kHz without modulation. The cycle model includes branch
prediction and address stalls with zero-wait memory; every decoded block matches the emulator's
reported length. These are isolated DSP costs, excluding stock processing and wrapper work.
They do not establish complete-firmware deadlines or physical CPU reserve for the changed image.

The existing host limits pass unchanged. An expanded 1,008-case maximum-mix matrix covers six
shapes, 21 pitches including the gain anchors, all modulation modes and aligned/mixed sources.
Its peak is exactly −4.280151 dBFS before and after, with zero rail samples. Oscillator and noise
state remain identical; 2,048 changing-pitch/shape/modulation blocks with only subs/noise audible
also produce identical output. The new 216-case gain check covers both VCOs, triangle/saw/square,
independent pitches and every modulation mode; the uncorrected engine fails it.

Over 12.58 million integer gain evaluations are monotonic and attenuation-only, with less than
0.614 Q12 gain units of error against floating-point interpolation and no undefined arithmetic.
The quiet upper range retains up to about 1.04 dB of relative coefficient rounding error.
Thirty-two eight-voice pitch/modulation workloads produce identical final output samples and
complete voice states on the host and emulated ColdFire after 32 blocks each.
Host suites, voice sanitizers and the available OS 1.54 build pass. A fresh 315-case host catalogue
against the unchanged retained reference improves from 257 to 269 passes, with no lost passes;
46 strict failures remain. Recordings and assertion limits are unchanged. No new device test
or complete-firmware timing run accompanies this correction.
