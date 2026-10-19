# Sources and confidence

The goal is an informed model of the OB-1 oscillator section. No controlled physical OB-1 capture set
is included here. Printed topology, a calculation from component values, a hardware measurement
and a reference calibration are different kinds of evidence. The reference measurements
set this model's sound without claiming
independent physical-hardware validation.

## Where to find sources

Start with original service or repair manuals, schematics and owner's manuals, then component
datasheets and documented repair observations. These sources cover the original OB-1:

| Source | Relevant material | Limits |
|---|---|---|
| [Electrongate Oberheim archive](https://www.electrongate.com/obfiles/index.html) | Original drawings: pot board 1339, performance/noise board 1340, programmer 1341 and synthesizer board 1342. | Scanned drawings; some small labels are unclear. |
| [Synthesizer board 1342, sheet 1, rev. B, 4-2-79](https://www.electrongate.com/obfiles/OB-1-1342a.jpg) | Oscillators, shaping, sync, cross modulation and mixer. | Does not supply a complete calibrated set of waveform amplitudes or operating points. |
| [Synthesizer board 1342, sheet 3](https://www.electrongate.com/obfiles/OB-1-1342c.jpg) | Reconstructed control voltages and sample-and-hold channels. | Supply rails and resistor values alone do not establish every calibrated control endpoint. |
| [Performance board 1340](https://www.electrongate.com/obfiles/OB-1-1340.jpg) | Selected noise transistor and its amplifier/filter network. | No specified absolute noise amplitude. |
| [OB-1 documentation at SynthXL](https://www.synthxl.com/oberheim-ob-1/) and [owner's manual](https://www.synthxl.com/wp-content/uploads/2020/10/oberheim-ob1-user-manual.pdf) | Printed pages 2–3 describe oscillator controls, ranges, output switches and keyboard CV. | User-facing descriptions are not a complete circuit calibration specification. |
| [Tauntek OB-1 repair notes](http://www.tauntek.com/OB1.htm) | Waveform trimming and observations about a repaired unit. | Repair observations can describe a modified unit rather than every production revision. |
| [Tauntek programmer description](http://www.tauntek.com/progdesc.txt) | Original six-bit potentiometer conversion and storage behavior. | An author's reconstruction, explicitly subject to error. |

Preserve downloaded originals, crops and working notes locally under `research/<topic>/`, with that
material ignored by Git. Record URLs, titles, revisions, page/sheet locations and file hashes so a
reading can be checked against the same scan. Do not infer unreadable values from another synthesizer
or silently treat a repair modification as a factory design. Lasting conclusions belong in `docs/`
with their sources; the local archive is not required to understand this document.

## Evidence behind the model

| Topic | Evidence and reasoning | Confidence and remaining question |
|---|---|---|
| Saw-type family | Synthesizer sheet 1342/1 shows fixed and controlled waveform paths. The model scales one half of a ramp against the other. Pot board 1339 specifies linear WAVE pots. | The linear U8 summer feeds grounded-base Q10 through T4 and R48; its current sets the 3080 gain approximately linearly above the transistor threshold. Circuit topology does not calibrate endpoints, dead zones or output levels. The model uses the measured reference control curve, opposite-half orientation and waveform-dependent fundamental gain; these are reference-calibrated choices, not recovered circuit operating points. |
| Pulse range | The manual describes pulse shaping; Tauntek's repair notes trim fully clockwise pulse to a square. The model uses a provisional minimum of 6.7 %. | The model deliberately ends at square rather than continuing beyond it, following the documented clockwise square trim. The narrow endpoint is provisional: it is not established by a printed factory limit or physical measurement. |
| Cross-modulation route | Sheet 1342/1 routes VCO2's raw saw to VCO1's pitch summer through R4 and R5, each 47 kΩ. The key-CV input is 100 kΩ into the same summer. | Readable component values establish a relative sensitivity of `100/94 ≈ 1.06` times keyboard-CV sensitivity per volt. The tap precedes selectable waveshaping and mixing, so changing the audible VCO2 waveform or muting its mixer must not remove the required raw-ramp source. Absolute depth still needs saw amplitude. |
| Cross-modulation depth | The manual's keyboard-CV statement is 1/12 V per key. With that scaling, an assumed centered saw of about ±4.8 V gives approximately ±5 octaves. | The lower ramp endpoint is not established from a readable reset threshold. Centering and the ±5-octave estimate are unverified circuit inferences. The implemented depth instead matches the measured reference reset-rate ratio, about 2.776: ±3.921875 octaves. Zero-mean exponential modulation raises mean frequency without requiring a DC offset. |
| VCO2 tuning | The manual, printed p. 2, describes up to five octaves in semitone steps and refers VCO2 back to VCO1; programmer notes describe six-bit values. | Supports a stepped coarse range. The model's exact −32 to +31 mapping is not a recovered factory code-to-frequency table. |
| Mixer ratios | Sheet 1342/1 lists 47 kΩ/100 kΩ oscillator branches and 100 kΩ/220 kΩ noise branches. The manual calls the middle setting −3 dB. | Component values are readable, but the source coupling network loads the switched branches. Neither their bare resistor ratio nor the panel label establishes the loaded gain. The physical gate truth table remains unresolved. The implemented middle positions instead match the reference's explicit −3 dB oscillator setting and the corresponding noise amplitude ratio. |
| Sync and sub reset timing | Sheet 1342/1 routes the master reset into the slave core and clocks the 4013 sub flip-flop from oscillator resets. | The model uses immediate resets and merges numerically coincident events. It retains a strong square under octave sync; the reference's near-silent output does not establish a hardware target. Finite reset duration and near-integer sub timing are not physically calibrated. |
| Coupling | Sheet 1342/1 shows 47 kΩ, 0.1 µF and 100 kΩ oscillator coupling networks. | About 11 Hz is a circuit approximation. One shared high-pass does not reproduce every switched-source transient. |
| Noise | Performance sheet 1340 specifies a selected transistor and frequency-shaping network, without output amplitude. | The white output level and gentle rolloff match reference recordings in the 200–8000 Hz band. The reference filter/amplifier remains in that path; no physical transistor spectrum or absolute unit amplitude is inferred. |
| Analog variation | Temperature compensation is present, but its existence does not quantify residual drift, mismatch or reset behavior. | No controlled evidence establishes an audible variation model; these effects are omitted. |

## Circuit constraints on sync and cross modulation

The [synthesizer drawing, 1342/1, revision B, revised 4-2-79](https://www.electrongate.com/obfiles/OB-1-1342a.jpg)
establishes routing more firmly than it establishes calibrated sound. The
[original owner's manual, printed pp. 2–3](https://www.synthxl.com/wp-content/uploads/2020/10/oberheim-ob1-user-manual.pdf)
also describes VCO2 slaved to VCO1 and increasing keyboard control voltage with increasing pitch.

### Feedback source and direction

The labelled OSC 2 SAW node is U18's output, before the selectable saw/pulse shaping and mixer.
It reaches U4's inverting pitch summer through R4 and R5, both 47 kΩ. Q1, controlled by SW18,
shunts their junction to disable cross modulation. The KEY CV input reaches the same summer
through R13, 100 kΩ. There is no extra inversion or coupling capacitor in the saw input branch.
These are direct readings of the drawing: audible waveform selection and source muting do not
select or silence this modulation tap.

The feedback direction follows from the core and buffer polarities. The exponential converter
draws current from the C10 ramp node; Q17 restores that node toward the +15 V side during reset.
Q14 and the PNP emitter follower Q16 preserve the intervening falling ramp's direction. U18 is
an inverting amplifier, making OSC 2 SAW rise between resets and fall at reset. Since the saw and
KEY CV enter the same pitch summer with the same sign, a more positive raw saw moves VCO1 toward
higher frequency. This is a circuit inference supporting rising-ramp exponential feedback, not
a polarity choice inferred from the final audio waveform.

### Event sync rather than continuous phase rescaling

OSC 1 SYNC OUT reaches VCO2's Q19 reset/comparator network through R120; Q21, controlled by SW17,
shunts the incoming sync signal. The slave reset network acts on the C10/Q17 ramp core. This path
does not enter the VCO2 pitch summer U16 and contains no continuous master/slave frequency-ratio
calculation. In an ideal event model, the slave keeps its own charging current between resets;
changing the master frequency changes future reset times, without rescaling the stored slave
ramp voltage between those events. Immediate resets are still an approximation: pulse width,
thresholds, transistor recovery and coincident natural resets remain analog questions.

### What this does and does not establish

The supported topology is raw rising-saw feedback with reset-event sync. Selectable-waveform
feedback or continuous phase-ratio sync in a reference is insufficient reason to add either to
the hardware model. A reference disagreement does not by itself identify an engine defect.

The drawing does not specify a calibrated raw-saw swing or mean voltage, absolute exponential
depth, or complete reset timing. In particular, the feedback branch is DC-coupled: assuming a
centered saw is a separate model choice. Thus the topology and feedback sign do not establish
an exact combined-mode frequency or level. Those quantitative targets need a calibrated circuit
model with stated operating points or physical measurements; a single reference fit cannot
supply them. Output polarity, standard unmodulated tuning and audible output gain are separate
from this internal feedback sign.

## Mixer ambiguity and output level

With separate 47 kΩ and 100 kΩ paths, choosing the latter instead of the former gives
`20 log10(47/100) ≈ −6.6 dB`. If ON enables both branches and the middle setting enables only the
47 kΩ branch, the ratio instead becomes about −3.3 dB. That interpretation would also raise ON
relative to the current model. The analogous noise branches have the same unresolved gate question.

These are switched-resistor ratios only: the 47 kΩ source resistance, 0.1 µF coupling and
100 kΩ shunt also affect the level and frequency response when the branch load changes.
Settling the physical circuit requires tracing the actual switch-control connections and including
that loaded source network, or measuring a unit at both positions. The chosen model uses the
reference's explicit −3 dB steps while retaining the reduced overall output and checked headroom.

## Resolving open questions

Use an original calibrated specification where one exists. Otherwise show the circuit calculation
and every assumed operating point, or collect controlled measurements with the unit/revision and
settings recorded. Keep provisional model choices distinct from verified hardware behavior.
Keep remaining physical uncertainties explicit, including minimum pulse width, circuit operating
points, output-switch gate behavior and selected-transistor noise amplitude. Distinguish these
from the measured reference calibration used by the current sound model.

## Reference calibration

The calibration recordings use reference version 1.0 in REAPER 7.82 at 48 kHz, with dry mono
settings and fixed output gain. Dense saw-family controls were fitted at A3, then checked at six
other pitches and both VCOs without fitting again. Signed-cycle diagnostics retain a fixed polarity
and fundamental phase origin, without time reversal. Reference acceptance compares supported
harmonic balance; magnitude-only agreement does not establish waveform equivalence. Independent
signed-engine regressions preserve the chosen construction and its phase.
Relative waveform RMS uses the full saw as an anchor. Mixer measurements explicitly select −3 dB;
normalized 0.5 is a different setting. Cross-modulation uses mean reset-rate ratios and spectral
checks, not an ordinary fundamental estimator. Noise uses three pure-white repeats at each of
two levels, compared in the same 200–8000 Hz band relative to a full saw.

The reference's filter/amplifier remains in its audio path. Low/high-register coloration and some
modulation spectra therefore retain residual differences. The calibrated model does not reproduce
every aspect of the reference, including its variation features, beyond-square pulse travel or
near-silent synced square. Capture manifests, raw audio and analysis stay local; compact measured
coefficients and regression anchors preserve the chosen calibration in the source.

Retained saw recordings across both VCOs separate source level from common output coloration by
comparing harmonics at the same absolute frequencies. Three independent low-register saws give
nearly the same inferred source attenuation. Fourteen VCO1 measurements between about 220 Hz and
10.83 kHz define a fixed amplitude curve. Twenty-four withheld saw recordings, including VCO2,
agree within 0.074 dB before integer gain rounding; fourteen triangle/square checks agree within
0.143 dB. Fine-detuned VCO2 follows the frequency-dependent attenuation, supporting a per-oscillator
correction. This inference assumes a saw-like source and common linear output response; it does
not identify a physical oscillator's amplitude or prove the reference's internal implementation.

The source attenuation is about 0.27 dB at 440 Hz, 0.84 dB at 882 Hz, 2.09 dB at 1.77 kHz,
7.15 dB at 4.50 kHz and 47.24 dB at 10.83 kHz, relative to A3. The model interpolates in amplitude
and clamps beyond the measured range. Weak/non-tonal higher captures do not establish a silence
threshold. This scalar correction leaves the shared coupling approximation and raw XMOD depth
unchanged; their remaining reference differences require separate evidence and decisions.
