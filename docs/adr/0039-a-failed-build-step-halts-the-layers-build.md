# ADR-0039: A failed LayerBuildStep halts its Layer's build

**Status:** Accepted
**Date:** 2026-09-02
**Extends:** ADR-0014 (a Layer's Primitives are derived from its LayerBuildSteps)

## Context

`LayerBuildStep::execute()` returns `void` and has no error channel, so a
step that fails throws out through `Layer::rebuild()` — which is called from
more than forty places, including `Undo.cpp` and `Actions.cpp`. With
`PrimitiveField`, `DefinePrefabs` and `PrefabField` this was close to
theoretical: their arguments are authored through UI that validates them.

`RunScript` makes step failure ordinary. Its arguments are a Lua program,
and a half-written program is the normal state of a program being written:
syntax errors, a step or Prefab name that no longer exists, a nil index, or
a loop that never terminates. The editor must survive all of these with the
author's work intact, and the build must have a defined outcome rather than
an exception escaping into an undo.

## Decision

A step that fails is caught at the `execute()` boundary. The build **stops
there**: the Layer keeps everything the steps before it produced, and the
failed step together with every step after it contributes nothing. The
failure is recorded as state on the step — message, and for a script its
traceback and line — and the editor marks both the failed step and the
steps below it as not run.

A script that does not terminate is failed the same way, by an instruction
budget enforced through a Lua debug hook.

Recording the failure requires a `mutable` member, since `execute()` is
`const`. `PrefabField`'s `mutable` built-Primitive storage already
establishes that pattern.

## Considered alternatives

**Skip the failed step and continue.** The Layer stays closer to complete,
so one broken script does less visible damage. Rejected: a recipe is a
pipeline in which each step reads the accumulated result, so every later
step would then compute against input missing what the failed step should
have contributed. A `PrefabField` placing Prefabs into terrain a failed
`RunScript` never laid down produces a Layer that is *plausibly* wrong,
which is harder to notice and harder to diagnose than one that is obviously
broken.

**Let the exception propagate.** Rejected: it can escape mid-undo and leave
the editor half-restored, and it takes down Layers that have nothing to do
with the failure.

## Consequences

- A failure in an early step **blanks most of the Layer** until it is
  fixed. That is the intended reading — but only if the step list makes the
  cause unmistakable. Without the "not run" marking on the steps below, this
  reads as the editor having destroyed the level.
- A failed step and a step that legitimately produced nothing must be
  distinguishable in the model, not only in the UI. An empty result and a
  broken script are otherwise identical from outside.
- This governs every step type, not only `RunScript`. `PrefabField` and
  `DefinePrefabs` acquire a defined failure behaviour they did not have.
