# AGENTS.md

Agent configuration for boolean-world.

## Agent skills

### Issue tracker

Issues live as GitHub issues in `ajare/boolean-world`, managed with the `gh`
CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary — the five canonical roles, each label string equal to its
name. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` and `docs/adr/` at the repo root. See
`docs/agents/domain.md`.

## Test execution

All new tests and test launchers must be safe for unattended, non-interactive
execution. They must never open dialog boxes or otherwise wait for user input.
In particular, handle missing DLLs and other startup failures without Windows
error UI: ensure runtime dependencies are available and/or suppress system error
dialogs in the process that launches the test, then report failure through the
exit status and captured logs.
