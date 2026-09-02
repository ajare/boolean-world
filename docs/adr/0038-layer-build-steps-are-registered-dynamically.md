# ADR-0038: LayerBuildStep types are registered dynamically, not compiled into core

**Status:** Accepted
**Date:** 2026-09-02
**Extends:** ADR-0014 (a Layer's Primitives are derived from its LayerBuildSteps)

## Context

`LayerBuildStep::registry()` was a `static const Registry<LayerBuildStep>`
built from a map literal in `LayerBuildStep.cpp`, naming `DefinePrefabs`,
`PrefabField` and `PrimitiveField` directly. Every step type therefore had
to be a `core` type, because deserialization goes through that registry and
nothing outside `core` could enter it.

`World::collectDependentResourceNames()` had the same shape by hand: a
`dynamic_cast` ladder over the step types `core` knows, so that a step
owning a resource reference could contribute it to the serialized
`dependentResources` header ADR-0033 requires.

Embedding Lua makes that closure untenable. The Lua runtime must not be a
dependency of `core`: `core` is linked into `core-dll` for the Python
tooling and into every core test, and none of those want a scripting
language. But a script-running step is still a step, and it must
deserialize, and it names a Lua script resource that must appear in the
dependency header or the World will not load.

## Decision

The step registry becomes dynamic and factory-based. A step type is
registered at run time by name with a creating factory, and `core` names no
concrete step type in its registry. `core-lua` registers `RunScript`; the
editor and the game each call that registration explicitly during startup —
not through a static initialiser, which the linker discards from a static
library when nothing else references the translation unit.

Because the factory is a closure, it is also the injection point for a step
type's host services. `RunScript` receives its `ScriptRuntime` this way,
rather than reaching for a singleton.

`collectDependentResourceNames` becomes a virtual on `LayerBuildStep`.
`World` calls it on every step and the `dynamic_cast` ladder dissolves into
the step subclasses that own the data.

## Considered alternatives

**Define `RunScript` in `core` behind an abstract executor** implemented by
`core-lua`. Keeps the registry closed and lets every host load every World.
Rejected because a host that never registered an implementation would
deserialize the step cleanly and then silently contribute nothing — a World
that looks loaded and is wrong, rather than one that refuses to load.

**Put Lua in `core`.** Rejected: it forces a scripting runtime into
`core-dll` and the Python tooling, and into every core test binary.

**Register a separate resource-collector callback** rather than a virtual.
Rejected because it separates the knowledge of a step's resources from the
step, which is the only thing that has it.

## Consequences

- A World containing a step type the host never registered **fails to
  deserialize**. This is deliberate, and the failure must name the missing
  type, not report a generic registry miss — a game built without `core-lua`
  cannot open a World containing a `RunScript` step, and must say so.
- Registration order becomes load-bearing at startup. Every host that
  deserializes Worlds must register the same set of step types, or Worlds
  become host-specific.
- Adding a step type that references a resource no longer means remembering
  to edit a `dynamic_cast` ladder in `World.cpp`. Omitting the override now
  means a step that declares no resources, rather than a step silently
  skipped by a collector that never heard of it.
