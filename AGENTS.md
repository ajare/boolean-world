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

Register every new executable test with `add_test(NAME ... COMMAND <target>)`
and run registered tests through CTest rather than invoking their executables
directly. The repository's `add_test` wrapper automatically:

- stages the target's CMake-known transitive runtime DLLs;
- runs Windows tests beneath the dependency-free `boolean_world_test_launcher`,
  which suppresses loader, system-error, and crash dialog boxes before the test
  image is loaded; and
- applies the finite `BW_TEST_TIMEOUT_SECONDS` timeout.

Do not bypass this wrapper with `_add_test`. If a test uses DLLs loaded at
runtime that CMake cannot discover (currently the DLLs behind static vendor
import stubs), also call `bw_deploy_vendor_dlls(<target>)`. Other manually loaded
DLLs must likewise have an explicit build dependency and staging rule.

Treat Windows loader statuses such as `0xC0000135` (DLL not found), `0xC0000139`
(entry point not found), `0xC000007B` (invalid image), and `0xC0000142` (DLL
initialization failed) as test infrastructure failures: fix the target's CMake
dependency or staging declarations, then rebuild and rerun the test. Never work
around them with an untracked machine-wide `PATH` change or a one-off manual DLL
copy.
