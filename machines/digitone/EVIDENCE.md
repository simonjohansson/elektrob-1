# OB-1 Digitone evidence log (version 0.1a)

What was derived, what ran, and what remains unverified. No firmware
bytes below: only interface facts (ranges, addresses) and tool outputs.

## Knob map history

The map went through three cuts during development before the released
0.1a:

- First cut: MOD/MIX1/MIX2/NOIS on SYN1 A/B/C/E (compressed stock
  ranges), TUNE/FRQ2/WAV1/WAV2 on SYN2 A-D. Superseded: the MIX2 default
  zoned wrong (zone 7, sub at -3 dB, instead of the Digitakt default's
  zone 1, fund at -3 dB) — caught by remapping in the second cut.
- Second cut: all eight on SYN2 A-H (slots 27-34, all stock 0-127), in
  the Digitakt SRC order, with Digitakt-identical zones (MOD raw/32, MIX
  raw*9/128 through the OB-1 order table, NOIS raw*3/128). The SYN2
  page mirrors the Digitakt SRC grid (4x2, icon over centred label);
  SYN1 shows a pointer to SYN2.
- Third cut: no control change. Moved into `machines/digitone/` for the
  upstream repo restructure and re-based onto the current shared engine
  (`dsp/`): true -3 dB mixer positions, +-3.92 octave cross mod, the
  LCG noise and the folded output trim replace the earlier gains, so the
  block is now written unscaled exactly as the Digitakt machine writes
  it.
- 0.1a: the control set mirrors the Digitakt machine's fifteen controls
  over two pages instead of the compact eight. SYN2 A-H = FRQ1 FIN1 TYP1
  WAV1 FRQ2 FIN2 TYP2 WAV2 (all 0-127 slots); SYN1 A-H = FND1 OCT1 XMOD
  (blank) FND2 OCT2 SYNC NOIS, each on a stock SYN1 slot wide enough for
  its control (ALGO 0-7, RTOC 0-18, RTOA 0-35, HARM 37-89, DTUN/FDBK/MIX
  0-127). This splits the old TUNE/FRQ2 into independent coarse+fine per
  VCO and the old MOD/MIX pairs into XMOD, SYNC, FND and OCT separately.
  Page assignment is forced: OSC cannot sit on SYN1 because ALGO (0-7)
  is far too coarse for FRQ1's +-32 semitones. No second pages are used,
  so `DNM_PAGES2` (machines 0.2) is not needed. The decode is the
  Digitakt machine's (`knob`/`zone`/`family`/`fine`/`wave`), widened to
  each slot's own range. Re-pick the machine after flashing.

## Stock parameter records (derived, not assumed)

Dumped from the depacked main OS of the stock file (hash-identified by
elekloader as digitone-mk1 1.44) at the core-dn1 `PARAMS` address
0x4018d454:

| knob ids | slot | min | max | def | names |
| --- | --- | --- | --- | --- | --- |
| 107 (SYN1 A) | 17 | 0 | 7 | 0 | Algorithm/SYN/ALGO |
| 108 (SYN1 B) | 18 | 0 | 18 | 3 | Ratio C/SYN/RTOC |
| 109 (SYN1 C) | 19 | 0 | 35 | 3 | Ratio A/SYN/RTOA |
| 110 (SYN1 D) | 20 | 0 | 2.8125 | 0.46875 | Ratio B/SYN/RTOB |
| 111 (SYN1 E) | 21 | 37 | 89 | 63 | Harmonics/SYN/HARM |
| 112 (SYN1 F) | 22 | 0 | 127 | 0 | Detune/SYN/DTUN |
| 113 (SYN1 G) | 23 | 0 | 127 | 0 | Feedback/SYN/FDBK |
| 114 (SYN1 H) | 24 | 0 | 127 | 0 | Mix/SYN/MIX |
| 119-126 (SYN2 A-H) | 27-34 | 0 | 127 | varies | A/B Env/Level/SYN/... |

Notes: SYN1 H is unipolar 0-127 in firmware (`machines.h` saying
-63..63 is stale); HARM's raw range is 37-89 (centre 63). The knob map
was designed inside these ranges, so no custom parameter was minted.

Showing key (`KEYVAR_AT`): 0x41368ce4 — the active-track word + 4
(`FW_ACTIVE_TRACK` 0x41368ce0 per core-dn1's mod.json). Pattern/project
pointers come from core-dn1 exports (`fw_pattern`, `fw_project`), not raw
addresses.

## FUNC+turn reset (0.1a)

A knob's reset gesture is the stock "write the parameter's default". The
parameter is a borrowed stock SYN slot, so the default it wrote was the
stock record's — FM's, not OB-1's. Confirmed on hardware: FUNC+turn gave
FRQ1 -32.00 st and TYP1 PULS (record defaults ATKA/ATKB 0, ENDA/ENDB
127), and FIN1/FIN2 -25.0 cents (DECA/DECB 32). Nine of the fifteen knobs
differed (FND1, FRQ1/2, FIN1/2, TYP1/2, WAV1/2); the six MIX slots happen
to share FM's default. The exact stock reset routine was not located; the
values matching the record defaults exactly, and the switch defaults
matching on the Digitakt, are the evidence.

The Digitone machine layer has no reset/range hook (the Digitakt's
machine-pages overrides its parameter range/default lookup instead), so
the adapter intercepts the turn on `ev_enc` (`core_dn_enc` → `core_enc`
runs the handlers before ENCDISP), writes `ob1_defaults` with
`core_sound_set` (the call a machine switch uses) or, with a step held in
GRID/STEP record, that step's lock row, and returns 1 so the stock reset
does not run. Gated like the overlay's paint (OB-1 sound, SYN1/SYN2, no
screen over the pages, first page) so FM tracks, other pages and popups
keep stock behaviour. Hardware: on 2026-10-22 the 0.1a build was tested
on a Digitone and behaved as expected, including the FUNC+turn reset on
the fifteen knobs and of a held step's lock.

## MACHINES tile icon

The Digitakt machine's menu icon is an 11 x 7 sawtooth: a ramp rising
left to right, a vertical reset edge, then the next ramp. Digitone tiles
are 16 x 16, so the icon is redrawn at that size with the same motif
(1 px strokes, a 9 px ramp, the drop, a 4 px next ramp), not copied.
The host harness checks that it is present, thin and has clear margins.

## Host tests (0.1a, OB-1 engine 0.5)

`python3 tools/ob1_digitone_host_test.py` — ALL HOST TESTS PASSED (26
in-process + 10 fresh-process pair checks): pitch A4, FRQ1/FRQ2 octave,
FIN1/FIN2 +-50 cents (mean-period ratios 1.0292 / 0.9725, i.e. exactly
2^(+-1/24)), OCT1 sub octave (period doubles), default loudness
188547072 vs DNM_PEAK 134217728 (1.41x at the test's phase; the peak of
two saws depends on their relative phase, and fresh-voice renders range
from 1.18x for one saw to 2.02x for the VCO1 ON + VCO2 -3 dB default),
per-track fuse (2 each) with release-free, cross-track coexistence,
UI-tick reap, LFO routing (WAV slot moves, AMP slot ignored bit-exact),
extremes (max 354025472, min silent), descriptor sanity (blank slot 3
unnamed, the tile icon a thin saw), knob icon art alive, replay
determinism, wave-family/sync/xmod differences, mixer silence, noise.
`mise run test` also compiles the shared engine's own tests (`tests/*.c`).

The 0.5 engine's pitch-dependent source calibration and 3-tap output
smoothing move the adapter's levels: -0.4 dB at A4 for one saw and for
the switch default, -2.7 dB for one saw at A6, -4.4 dB with FRQ at +32
semitones and -5.7 dB at the extremes case (pitch 100, every knob at its
maximum). The Digitakt machine renders through the same path, so device
level matching holds; the change lowers the maximum output level and
never raises it.

## Build / lint / patch (0.1a on OS 1.44; upstream 86a18ab, elekloader 05d571c)

`mise run build:digitone` then `mise run firmware:digitone` (same
commands run by hand with `python3 -m elekloader.sdk.build`).

- ob1-dn 0.1a: `.run` 36855, `.bss` 784 bytes; 0 sites, 327
  relocations; imports `core_sound_set`, `dnm_phase_inc` + `fw_*`/core
  names (incl. `fw_textbox` for centred labels).
- lint: `OK: links as core 4.0 > machines 0.2 > ob1-dn 0.1a; RAM 52188
  of 1310720 bytes`.
- patch: `verified` + `WROTE` for 1.44 (sha256 cff2b3df…);
  `RAM 0x43ec0000-0x43eccbdc`.

## Hardware (0.1a, 2026-10-22)

The 0.1a build was tested on a Digitone and behaved as expected: sound,
both pages, turns, locks, transport, menus, the FUNC+turn reset of all
fifteen knobs and of a held step's lock, with FM tracks and popups
unaffected. The reversed page order (SYN1/MIX first) was confirmed there
too. Lock-table layout and key/brain offsets are reused by induction from
the proven Digitone reference on OS 1.44; the tested build's locks display
and reset as expected.

The 1.44-only rebuild with the redrawn MACHINES tile icon was flashed on
the same unit afterwards and works; the icon matches the Digitakt
machine's.

## Emulator screenshots (0.1a)

`syn1-mix-page.png` and `syn2-osc-page.png` were captured in digiemu.
They show the icon-over-label composition on both pages, `fw_textbox`
centring, the blank MIX cell and the page order (MIX on SYN1, OSC on
SYN2). They do not cover turns, holds or toasts, and are not a byte
comparison against stock.

## Pending (not claimed)

- Emulator: fwcheck against stock; turns, holds and toasts (the
  screenshots above cover the static pages only).
- `OB1_TRACK_VOICES` (2/track) is a conservative default pending the
  fwcheck render margin (needs 10%+) and hardware voice-count testing.
  The 0.5 engine's continuous muted oscillators raise the cost of sparse
  mixes (up to +27% on the Digitakt for one audible saw), so the margin
  check must include those settings.
