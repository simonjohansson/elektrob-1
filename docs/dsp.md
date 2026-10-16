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
`max(s, 0) + g × min(s, 0)`. As `g` moves linearly from −1 to +1, the shape moves from a folded ramp
(triangle) to a saw. The mean is removed. The triangle's peak-to-peak amplitude is half the saw's;
the model does not normalize their levels across the control's travel.

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
pitch exponentially. For normalized ramp `x` in [−1, +1], the frequency factor is `2^(5x)`, capped by
the 12 kHz oscillator limit. Changing VCO2's audible waveshape does not change this modulation source.
The exponential is approximated with a table and interpolation. Both sync and cross modulation can
operate together, so the two oscillators must be advanced as an interacting pair.

An inaudible modulator can still be required: VCO2 must run for audible VCO1 cross modulation, and
VCO1 must run for audible VCO2 sync. When no audible destination needs a routing, that routing is
skipped. Oscillator and noise state are retained while skipped, rather than advanced through silence.

## Mixer, noise and headroom

There are five sources: each VCO's fundamental and sub-octave, plus white noise. Relative to oscillator
ON, oscillator −3DB uses 47/100 gain. Noise ON uses 47/100 and noise −3DB uses 47/220, followed by a
separate source-level factor of about −8 dB. Integer coefficients approximate these ratios.

The −3DB text reproduces the panel label. The modeled oscillator attenuation is about −6.6 dB, and
the noise switch's middle position is about −6.8 dB below its ON position. The physical switch logic
is unresolved; the resistor values alone do not establish which gates are on together.

Noise is a deterministic 32-bit xorshift sequence with a nonzero seed, with distinct seeds per device
track. Its level was set by ear: noise ON is about 14.5 dB below a full saw in RMS level. This is not
a hardware noise calibration.

After summing, a one-pole high-pass removes DC at about 11 Hz. Its low-pass coefficient is
`47/32768 ≈ 2π × 11/48000`. A single filter after the sum approximates separate coupling networks;
their transients need not agree when source levels change.

Output is scaled by one quarter, making a single full saw at ON peak around −12 dBFS. That leaves
room for the two fundamentals and their subs; it is not a guarantee against every combination's
overshoot or added noise. Output saturates at full scale rather than wrapping. Preserve or reduce
maximum loudness when changing gains, and compare levels as well as timbre.

No drift, component mismatch, finite analog reset time or transistor-level noise coloration is
modeled. See [Architecture](architecture.md#timing-and-state) for the approximation during silent time.
