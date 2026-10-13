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
| `mise run render 440 [shape] [seconds]` | renders the DSP to `out/render-440-0.wav` |
| `mise run analyze FILE.wav` | prints f0, harmonic levels and the aliasing floor |
| `mise run build` | builds core, machine-pages and the machine into `out/` |
| `mise run firmware` | patches the stock OS into `out/Digitakt_custom.syx` (version `$FW_VERSION`, `OB-1`) |
| `mise run emu` | adds that build to digiemu and opens its panel |

In the emulator: FUNC+SRC (shift-click FUNC to latch) opens the machine menu.

The toolchain is Homebrew's `m68k-elf` gcc/binutils, installed by mise's
`[bootstrap.packages]` into `/opt/homebrew`.
