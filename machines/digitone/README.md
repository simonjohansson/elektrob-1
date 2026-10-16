# Digitone wrapper scaffold

Digitone support is not implemented. The `build:digitone`, `build:deps:digitone`,
`firmware:digitone`, `emu:digitone`, `release:digitone` and `knobs:digitone` mise
tasks only print a message and return success. They do not build or write artifacts,
open an emulator or publish a release, and need no stock firmware or toolchain.
The unqualified device tasks dispatch to both the Digitakt and Digitone subtasks;
run a `:digitone` task directly to run only this scaffold's placeholders.

The future Digitone mk1 wrapper will call the shared engine in `../../dsp/`.
It will own:

- Mapping Digitone parameters to the shared voice's knob values.
- Converting notes and pitch controls to oscillator phase increments.
- Connecting voice initialization, rendering and lifetime to the device's callbacks.
- Matching output scaling and envelope handling to the Digitone integration.
- Defining the device's machine description, controls and UI.

Start from the pinned Elekloader [machines-dn1 integration](../../vendor/elekloader/mods/machines-dn1/README.md)
and [dn-sine-machine example](../../vendor/elekloader/examples/dn-sine-machine).
The Digitakt wrapper in `../digitakt/` is a reference for the OB-1 controls;
its firmware hooks, parameter slots and graphics belong to Digitakt.

This scaffold has no C wrapper or mod manifest. Parameter mappings, machine IDs,
firmware requirements and hardware behavior remain to be established for the port.
