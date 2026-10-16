# Sources and confidence

The goal is an informed model of the OB-1 oscillator section. No controlled physical OB-1 capture set
is included here. Printed topology, a calculation from component values, a hardware measurement
and a choice made by ear are different kinds of evidence.

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
| Saw-type family | Synthesizer sheet 1342/1 shows fixed and controlled waveform paths. The model scales one half of a ramp against the other. Pot board 1339 specifies linear WAVE pots. | The topology supports the family; a linear pot does not prove the complete OTA/transistor gain law. The linear taper is a model choice pending controlled waveform sweeps. |
| Pulse range | The manual describes pulse shaping; Tauntek's repair notes trim fully clockwise pulse to a square. The model uses a provisional minimum of 6.7 %. | Square trim has documentary support. The narrow endpoint is not established by a printed factory limit or physical measurement. |
| Cross-modulation route | Sheet 1342/1 routes VCO2's raw saw to VCO1's pitch summer through R4 and R5, each 47 kΩ. The key-CV input is 100 kΩ into the same summer. | Readable component values establish a relative sensitivity of `100/94 ≈ 1.06` times keyboard-CV sensitivity per volt. Absolute depth still needs saw amplitude. |
| Cross-modulation depth | The manual's keyboard-CV statement is 1/12 V per key. With that scaling, an assumed centered saw of about ±4.8 V gives approximately ±5 octaves. | The lower ramp endpoint is not established from a readable reset threshold. Centering is an inference; ±5 octaves is not a measured hardware depth. |
| VCO2 tuning | The manual, printed p. 2, describes up to five octaves in semitone steps and refers VCO2 back to VCO1; programmer notes describe six-bit values. | Supports a stepped coarse range. The model's exact −32 to +31 mapping is not a recovered factory code-to-frequency table. |
| Mixer ratios | Sheet 1342/1 lists 47 kΩ/100 kΩ oscillator branches and 100 kΩ/220 kΩ noise branches. The manual calls the middle setting −3 dB. | Component values are readable. Which gates the panel enables together remains unresolved; the modeled −6.6 dB oscillator step is not established as hardware behavior. |
| Coupling | Sheet 1342/1 shows 47 kΩ, 0.1 µF and 100 kΩ oscillator coupling networks. | About 11 Hz is a circuit approximation. One shared high-pass does not reproduce every switched-source transient. |
| Noise | Performance sheet 1340 specifies a selected transistor and frequency-shaping network, without output amplitude. | White noise and a level about 14.5 dB below a full saw are model choices; the level was set by ear, not calibrated from a unit. |
| Analog variation | Temperature compensation is present, but its existence does not quantify residual drift, mismatch or reset behavior. | No controlled evidence establishes an audible variation model; these effects are omitted. |

## Mixer ambiguity and output level

With separate 47 kΩ and 100 kΩ paths, choosing the latter instead of the former gives
`20 log10(47/100) ≈ −6.6 dB`. If ON enables both branches and the middle setting enables only the
47 kΩ branch, the ratio instead becomes about −3.3 dB. That interpretation would also raise ON
relative to the current model. The analogous noise branches have the same unresolved gate question.

Settling it requires tracing the actual switch-control connections or measuring a unit at both
positions. Changing the ratios must include output headroom and noise-level calibration, rather
than silently making the instrument louder.

## Resolving open questions

Use an original calibrated specification where one exists. Otherwise show the circuit calculation
and every assumed operating point, or collect controlled measurements with the unit/revision and
settings recorded. Keep provisional model choices distinct from verified hardware behavior.
Keep unresolved values explicit, including saw taper, minimum pulse width, absolute
cross-modulation depth, output-switch gate behavior and noise color/level.
