# ADR-0040: Build scripts are deterministic by construction

**Status:** Accepted
**Date:** 2026-09-02
**Extends:** ADR-0014 (a Layer's Primitives are derived from its LayerBuildSteps)

## Context

ADR-0014 makes a Layer's Primitives a pure function of its recipe: they are
recomputed from scratch by re-running the enabled steps in order, and the
step list, not the Primitives, is what a Layer serializes. Every consumer
rests on that — the fold, cached `WorldData`, undo, and the assumption that
the world shipped in the game is the world that was authored.

A Lua build step can break it trivially. Lua 5.4 seeds `math.random` from
the OS, so a scatter script places its content somewhere new on every
rebuild — meaning on every undo, every reload, and once more on the
player's machine. `os` and `io` let a script depend on the clock or the
filesystem. `require` lets it depend on a file the World does not name.
`pairs` iterates hash tables in an order derived from addresses.

None of these fail loudly. They produce a Layer that is subtly different
each time it is built, which surfaces much later as a world file that
drifts for no visible reason.

## Decision

Determinism is enforced by the environment a build script runs in, not by
asking script authors to be careful.

Each execution gets a **fresh environment** containing only base functions
less those that load code or drive the collector, plus `table`, `string`,
`math`, a `print` routed to the host log, and `dprint` routed to an optional
host debug sink which is a no-op by default. `io`, `os`, `debug`, `package`
and `require` are absent. A separate `include(name)` operation may
execute only a LuaScript in the root script's manifest-declared transitive
resource dependencies. It returns that script's table, caches the table only
for the current execution, and runs the included chunk in the same Restricted
environment and instruction budget. The library set is a parameter of the
runtime's execute call rather than a global policy, so a future
non-deterministic client — gameplay scripting — can ask for a different set
without weakening this one.

Randomness is kept, and made reproducible: every `RunScript` step
serializes a **seed**, re-applied at the start of every execution. A
scatter therefore reproduces exactly, and changing the arrangement is an
authored edit — a reroll — rather than a side effect of rebuilding.

Authored Build variables enter the environment only through execution-local,
immutable tables. `world.vars` contains the World scope, `layer.vars` applies
same-typed Layer overrides, and `step.vars` applies the RunScript's
resource-declared same-typed overrides. Each narrower scope inherits the
unshadowed values above it. Their typed values are serialized with their
owning World, Layer, or `RunScript` step, so a later resource-default change
cannot make the game rebuild a saved World differently from the editor that
saved it. Iterating any `vars` table with `pairs()` is lexical by name, and
Build environments omit `rawset` so scripts cannot bypass immutability.

Because the environment is rebuilt per execution, nothing a script leaves
in a global survives to the next rebuild, and two steps cannot share state
through one.

## Considered alternatives

**Expose `math.randomseed` and let authors call it.** Rejected: a script
that forgets is non-deterministic in a way nobody notices, and the failure
appears as a world that changed rather than as an error.

**Ship the full standard library and document the responsibility.**
Rejected for the same reason: the cost of a mistake is paid by whoever
opens the world next, not by whoever wrote the script.

**Discover a script's resource references by executing it during
serialization.** Rejected — `collectDependentResourceNames()` is called from
`serializeImpl`, so saving a World would run every script in it, and a
script whose references depend on its seed or on prior geometry would
declare different resources on different saves. A `RunScript` step instead
serializes an authored list of the resources its script names.

## Consequences

- **No `require`.** Shared helper code instead uses resource-backed
  `include(name)`. The manifest relationship makes the dependency available
  before World deserialization, and execution-local caching prevents module
  state from leaking between rebuilds.
- The environment cannot be used to cache work between rebuilds. Given how
  often `rebuild()` runs, a build step must be a pure function of the recipe
  anyway.
- A resource a script names only in its source is invisible to static
  collection, so `RunScript` carries an authored extra-resources list. That
  list can drift from what the script actually references; the drift is
  detectable by running the script and comparing, and it fails in the editor
  at load rather than in the field.
- The regression test that matters is a single assertion: rebuild the same
  Layer twice and get identical Primitives. Every decision here exists to
  make it true, and it catches all of them at once.
