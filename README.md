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
| `mise run test` | builds the DSP (`machine/dsp.c`) for the host and runs `tests/` |
| `mise run render 440 [wave] [seconds]` | renders the DSP to `out/render-440-63.wav` (wave as WAV1: 0-63 triangle to saw, 64-127 pulse to square) |
| `mise run analyze FILE.wav` | prints f0, harmonic levels and the aliasing floor |
| `mise run bench` | counts the DSP's ColdFire instructions a block for 8 voices, in Unicorn |
| `mise run build` | builds core, machine-pages and the machine into `out/` |
| `mise run firmware` | patches the stock OS into `out/Digitakt_custom.syx` (version `$FW_VERSION`, `OB-1`) |
| `mise run emu` | adds that build to digiemu and opens its panel |

In the emulator: FUNC+SRC (shift-click FUNC to latch) opens the machine menu.

The toolchain is Homebrew's `m68k-elf` gcc/binutils, installed by mise's
`[bootstrap.packages]` into `/opt/homebrew`.

## CPU

`mise run bench` runs the DSP compiled for the Digitakt's ColdFire in Unicorn
and counts its instructions per 32-sample block, 8 voices:

| setting | instructions a block | a voice | 8 voices, of a block |
|---|---|---|---|
| default: one saw | 19,208 | 2,401 | +18 % |
| both VCOs and subs | 35,604 | 4,451 | +34 % |
| worst: + SYNC, XMOD, noise | 66,539 | 8,317 | +63 % |

The stock render alone takes about 84,000 instructions a block, 80 % of it
(Digi Mono's measurement on a unit), so the last column is on top of that:

- Every track set to OB-1 costs this, playing or not. Silent voices are not
  skipped yet.
- 8 tracks of the default sound about fit; 8 worst-case tracks would
  overload the unit. Plan on 2-4 heavy OB-1 tracks, as Digi Mono does.
- Not measured: the stock render's own savings on tracks it does not play
  from a sample, and the per-voice pitch and knob code in `machine/ob1.c`
  (a few dozen instructions).

digiemu's timed firmware check (`emu.fwcheck --script`) hung in its boot
stage twice with this build, so these numbers are the DSP alone.
