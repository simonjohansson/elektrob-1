## Development

Enable the Ponytail plugin for all development. If it is not installed, read and follow https://raw.githubusercontent.com/DietrichGebert/ponytail/refs/heads/main/AGENTS.md before starting development.

Keep code comments and README files free of issue references. Commit messages may link to issues, but always use the full issue URL, never shorthand such as `#1`.

## Project orientation

OB-1 shares an oscillator engine across device integrations. Digitakt mk1 is implemented; Digitone is a scaffold whose placeholder tasks succeed without producing a working port.

Read [Architecture](docs/architecture.md) for signal flow and integration boundaries, [DSP](docs/dsp.md) for numeric and audio constraints, and [Sources and confidence](docs/references.md) before changing hardware-derived assumptions. Use [Development and verification](docs/development.md) for setup, commands and checks appropriate to the change.

- Manage language runtimes with mise. `mise run test` runs host DSP checks without stock firmware or the cross-toolchain; it does not verify device integration, persistence, CPU headroom or analog fidelity.
- Preserve machine ID 69 and saved parameter meanings. Changes to those compatibility boundaries need explicit authorization.
- Preserve or reduce maximum output level when changing the sound engine, and verify headroom and level matching. Keep printed specifications, circuit assumptions, model choices and hardware measurements distinct.
- Stock firmware and raw research remain local and ignored. Keep device-specific firmware assumptions out of the shared sound engine and leave dependency submodules unchanged unless the task requires them.

## GitHub issues

Use the `gh` CLI for all issue operations, including opening, updating, and closing issues. When working on a fork, ensure issues are enabled on the fork before creating issues.

Before implementing a feature or fix, record a plan in a GitHub issue covering the intended outcome, approach, and verification. Create sub-issues from that plan as needed.

Exception: If the user explicitly says `YOLO` for the current task, skip planning and issue creation for a small fix or narrowly scoped change.

Every issue body must contain these two sections:

- `## Human Readable`: Keep it short and easy to understand, with minimal jargon and technical detail.
- `## Machine Readable`: Include as much or as little technical detail and jargon as needed.

## Plan and research documents

Keep short-lived feature plans and research notes uncommitted and out of PRs and merges.

Exception: If another issue needs these documents as a reference, commit and push them on a separate reference branch and link to them from the issue. Keep those documents out of the feature PR and merge.

Research documents may be committed and merged when they provide lasting, low-level technical reference, such as how a resistor value affects oscillator behavior. Keep feature-specific debugging narratives, such as a page event causing a feature to fail, in issues or temporary notes instead.

## Research sources and storage

- Start with manufacturer service and repair manuals, schematics, owner's manuals and component datasheets. Look on manufacturer support sites and established synthesizer documentation archives; use documented repair notes as supporting evidence. Distinguish original specifications from observations about a particular unit.
- Store downloaded sources, inspection crops and working notes under `research/<topic>/`, keeping this material local and gitignored. Record source URLs, document titles and revisions, and relevant pages or sheets alongside the material.
- Store lasting technical references in `docs/<topic>.md`, following the policy above. Cite the source documents and relevant pages or sheets; distinguish source readings, calculations and measurements, and state uncertainty rather than filling gaps with assumptions.
- Write documentation around concepts, architecture, signal flow, constraints and design rationale. Keep it independent of implementation filenames, function names and the current code layout; avoid descriptions such as "we do this in file X" that become stale as the implementation changes.
