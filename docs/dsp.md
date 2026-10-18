# DSP

This describes the oscillator model and its numeric constraints. Hardware evidence and confidence
are recorded separately in [Sources and confidence](references.md).

## Numeric formats and timing

Fixed-point values represent fractions with integers. For example, Q15 uses 32768 to represent 1.0.
Intermediate waveform values can exceed that nominal range; stored samples use 32-bit integers.

| Quantity | Representation |
|---|---|
| Oscillator phase | Unsigned 32-bit cycle; wrapping at 2³² is a reset. |
| Phase increment | `2³² × frequency / 48000`, capped at 2³⁰, or 12 kHz. |
| Waveform amplitude and saw-shaping gain | Q15. |
| Event position within a sample | Q16; 65536 represents one sample. |
| Mixer gains | Q12; 4096 represents unity gain. |
| Output samples | Signed Q31; nominal range −1 to just below +1. |

The voice processes 32 samples at a time. Waveshape and base pitch are held for that block.
Cross modulation changes VCO1's increment every sample. A scaled reciprocal estimates event positions
without a divide at every crossing. The 12 kHz limit also bounds products used in edge corrections;
it is a numerical and sound-model constraint, not a measured analog frequency limit.

## Oscillators and waveshapes

Both VCOs start from a rising ramp. For a ramp `s` in approximately [−1, +1], the saw-type family is
`min(s, 0) + g × max(s, 0)`. The fixed half follows the reset; the other half scales from inverted
triangle to saw. The mean is removed. A measured 64-position control curve selects shaping gain
and fundamental level, matching the reference's intermediate cycles and relative loudness. The
triangle's raw half swing is compensated at mixing; sub-octave gain is independent of waveshape.
All level coefficients include the output trim and are computed as constants, with no per-sample
waveform normalization. The full saw uses an equivalent path that omits half scaling and the absent
middle corner.

Pulse type compares phase with a width threshold. Width moves linearly from about 6.7 % to 50 % of a
cycle. Removing its mean avoids a width-dependent DC offset, but does not make every width equally
loud. The lower endpoint is provisional, not a recovered factory specification.

Each sub-octave is a square made by toggling a flip-flop on the parent oscillator's reset. Hard-sync
resets also clock that flip-flop. Its pitch and phase therefore follow reset events rather than an
independent oscillator at half the nominal frequency.

## Band-limiting

A discontinuity creates harmonics above the sample rate's Nyquist limit of 24 kHz, which fold into
audible frequencies as aliasing. Two-sample polynomial corrections smooth jumps (polyBLEP) and changes
of slope (polyBLAMP). Corrections affect both the preceding and current sample, so oscillator and
sub-octave output are delayed by one sample.

These corrections reduce aliasing; they are not an ideal low-pass filter. The spectral checks focus
on aliases below 10 kHz at a 1.9 kHz fundamental. Suppression is weaker near Nyquist. Under sync, an
extra edge crossed by the restarted ramp within the same sample is not corrected; very narrow pulses
near the 12 kHz limit can expose this case.

## Sync and cross modulation

Sync makes VCO1 the master: its reset restarts VCO2 at the corresponding fraction of a sample.
Natural resets very close to a sync reset are merged within about 1/1024 of a sample so rounding
does not introduce extra sub-octave flips at whole-number frequency ratios.

Cross modulation uses VCO2's raw ramp, before its selectable waveshape and mixer, to change VCO1's
pitch exponentially. For normalized ramp `x` in [−1, +1], the frequency factor is `2^(3.921875x)`, capped by
the 12 kHz oscillator limit. Changing VCO2's audible waveshape does not change this modulation source.
The exponential is approximated with a table and interpolation. Both sync and cross modulation can
operate together, so the two oscillators must be advanced as an interacting pair.

Both VCOs and their sub-octave flip-flops advance on every rendered block, even with their mixer
outputs OFF. Selected sync and cross modulation also remain active regardless of output levels.
This preserves phase, reset timing and band-limiting history when a source returns to the mix.

## Mixer, noise and headroom

There are five sources: each VCO's fundamental and sub-octave, plus white noise. Middle positions
are approximately −3 dB relative to ON for both oscillator and noise sources. Oscillator calibration
uses the reference's explicit −3 dB label; the noise middle position uses the corresponding amplitude
ratio. Normalized knob position 0.5 is a different level. Integer coefficients approximate these
ratios. The original physical switch truth table remains a separate hardware question.

Noise uses the high 16 bits of a full-period 32-bit linear congruential sequence with distinct
seeds per track. A noise-only one-pole filter uses coefficient 11/16 to match the measured white
output's gentle high-frequency rolloff. Its state is held when noise is OFF; oscillator-only paths
omit the filter work. Noise at ON is about 20.41 dB below a full saw in the 200–8000 Hz comparison
band. This is a match to the reference output path, which includes its filter/amplifier, rather than
identification of physical noise-source coloration. There is no added pink-noise control.

After summing, a one-pole high-pass removes DC at about 11 Hz. Its low-pass coefficient is
`47/32768 ≈ 2π × 11/48000`. A single filter after the sum approximates separate coupling networks;
their transients need not agree when source levels change.

The voice applies a fixed −10 dB trim to the source gains before mixing, followed by the mixer's
quarter scale. A single full saw at ON peaks around −22 dBFS. The trim and measured waveform compensation are folded into constant
source gains, so they add no per-sample or per-block multiplication. Sources remain additive; there
is no source-count normalization or automatic limiter. Saturation remains a final numeric guard.
The level adjustment follows a particular device listening/USB comparison, not universal loudness
calibration. Preserve or reduce maximum loudness when changing gains, and compare levels as well
as timbre.

No drift, component mismatch, finite analog reset time or transistor-level noise generation is
modeled.
