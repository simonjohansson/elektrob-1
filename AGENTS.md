## Development

Enable the Ponytail plugin for all development. If it is not installed, read and follow https://raw.githubusercontent.com/DietrichGebert/ponytail/refs/heads/main/AGENTS.md before starting development.

Keep code comments and README files free of issue references. Commit messages may link to issues, but always use the full issue URL, never shorthand such as `#1`.

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
