# Performance baseline

The 17 October 2026 baseline starts at revision `145adfc8b78a7715643d51006721ff29f5c8de8e`.
Measurements use the same compiler, dependencies and workloads before and after the corrections.
Firmware, source snapshots, executable benchmarks, stimulus scripts and raw reports are retained
locally under `capture-local/performance-20261017/`; they are not distributed with the firmware.

## Sound-engine instructions

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

## Complete-firmware simulator

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

The current host checks include 240 one-second maximum-mix conditions across six waveform settings,
five pitches, all four modulation modes, aligned sources and mixed-shape fifths with noise ON.
The corrected implementation's highest peak in that matrix is −4.24 dBFS. All four previously clipping
campaign cases were rerendered without rail hits. This is bounded test coverage, not a proof for every
possible control transition or physical device workload.
