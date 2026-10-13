# ob1

A custom SRC machine for the Digitakt mk1 (OS 1.53), built as an
[elekloader](https://github.com/irpina/elekloader) mod and tested in
[digiemu](https://github.com/irpina/digiemu). `machine/` starts as elekloader's
`examples/sine-machine`.

## Setup

Put `Digitakt_OS1.53.syx` in `firmware/`, then:

```sh
mise run setup
```

## Tasks

| task | does |
|---|---|
| `mise run test` | builds the DSP (`machine/dsp.c`, `machine/voice.c`) for the host and runs `tests/` |
| `mise run render OUT.wav [key=value...]` | renders the machine's voice: `note=57 seconds=4`, the knobs `wav1 wav2 frq2 mix1 mix2 mod nois`, `sweep=KNOB:FROM:TO` |
| `mise run analyze FILE.wav` | prints f0, harmonic levels and the aliasing floor |
| `mise run protocol` | renders the machine's side of the comparison protocol (#15, T1-T8) into `out/ours/` |
| `mise run compare DIR` | measures captures named as in the protocol against `out/ours/`, test by test |
| `mise run bench` | counts the DSP's ColdFire instructions a block for 8 voices, in Unicorn |
| `mise run build` | builds core, machine-pages and the machine into `out/` |
| `mise run firmware` | patches the stock OS into `out/Digitakt_custom.syx` (version `$FW_VERSION`, `OB-1`) |
| `mise run emu` | adds that build to digiemu and opens its panel |

In the emulator: FUNC+SRC (shift-click FUNC to latch) opens the machine menu.

The toolchain is Homebrew's `m68k-elf` gcc/binutils, installed by mise's
`[bootstrap.packages]` into `/opt/homebrew`.

## CPU

`mise run bench` runs the DSP compiled for the Digitakt's ColdFire in Unicorn
and counts its instructions per 32-sample block, 8 voices (`-v` in
`tools/bench.py` splits them by function):

| setting | instructions a block | a voice | 8 voices, of a block |
|---|---|---|---|
| default: one saw | 15,028 | 1,879 | +14 % |
| both VCOs and subs | 23,614 | 2,952 | +22 % |
| worst: + SYNC, XMOD, noise | 41,502 | 5,188 | +40 % |

The stock render alone takes about 84,000 instructions a block, 80 % of it
(Digi Mono's measurement on a unit), so the last column is on top of that:

- Only sounding voices cost this: a voice whose AMP envelope is at 0, with no
  trig starting, is not rendered. The envelope's level runs about as the
  cube of the output, so a long release keeps a voice going for seconds.
- 8 sounding voices of the default sound about fit; 4 sounding voices of
  both VCOs fit (+11 %); 8 sounding worst-case voices would overload the
  unit.
- Not measured: the stock render's own savings on tracks it does not play
  from a sample, and the per-voice pitch and knob code in `machine/ob1.c`
  (a few dozen instructions).

The C has been tuned once: the per-sample step inlined and compiled per
waveshape, the oscillator state kept out of memory, and the mixer, noise,
high-pass and clip in one loop. From the first measurement that took 38 %
off the worst case (66,539) and 22 % off the default (19,208). Going further
needs hand-written assembly or FAST AUDIO's SRAM (`.fast`, which needs
digihealth).

digiemu's timed firmware check (`emu.fwcheck --script`) hung in its boot
stage twice with this build, so these numbers are the DSP alone.
