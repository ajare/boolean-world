# ADR-0014: A Layer's Primitives are derived from its LayerBuildSteps, never stored independently

**Status:** Accepted (amended by ADR-0019: Layer ownership is permanent)
**Date:** 2026-08-19
**Relates to:** ADR-0013 (Layer owns its Primitives and WorldTriggerLines)

## Context

Authoring wants Layers that can be populated procedurally, not just by
placing Primitives one at a time. The editor should let a Layer carry an
ordered list of `LayerBuildStep`s — each a serializable, factory-constructed
object whose `execute()` reads the Layer as built so far and adds new
Primitives to it — with steps addable, removable, reorderable, and
individually disableable so their effect can be previewed.

ADR-0013 made `Layer` the owner of a `Primitives` collection, populated by
direct CRUD (`addPrimitive`, `removePrimitive`, ...). Steps could have been
layered on top of that as a purely optional convenience: primitives placed
by hand alongside primitives placed by steps, coexisting in the same
collection. We rejected that. If hand-placed and step-generated primitives
coexist, disabling a step can no longer cleanly remove "its" primitives —
there is no way to tell which primitives in the collection came from which
source, or from none. Reproducibility (a Layer's content follows
deterministically from its step list) and coexistence with freeform manual
placement are mutually exclusive; we chose reproducibility.

## Decision

A Layer's Primitives are always **derived**: computed by running its
enabled `LayerBuildStep`s in order, never authored or stored independently
of them. The step list — not the resulting Primitives — is what a Layer
serializes. The Primitives collection ADR-0013 introduced becomes a cache
rebuilt from the step list, not ground truth.

Every Layer's first step is a `PrimitiveField` step and cannot be deleted,
though it can be disabled; no other step type can occupy that position. A
step's type is fixed for its lifetime — changing a step's type means
deleting it and adding a new one, never an in-place conversion. New steps
may only be inserted at index ≥ 1; index 0 is permanently reserved for the
Layer's first step.

The existing manual Create/Edit Primitive editor UI is preserved from the
user's point of view, but now operates by mutating the target Layer's
`PrimitiveField` step's embedded primitive list, then triggering a rebuild.
Moving a Primitive to another Layer targets that Layer's first step, which
is guaranteed to be a `PrimitiveField` step. (ADR-0019 removes moving between
Layers entirely; this sentence no longer describes the system.)

Breaking the save format is accepted; no migration path is provided for
`.world`/`.layer` files saved before this change.

## Consequences

- There is no way to hand-place a Primitive that survives independently of
  some step's argument list — every Primitive in a Layer traces back to
  exactly one step.
- Disabling a step is always a full Layer rebuild (re-run enabled steps
  from scratch), not a toggle over cached per-step output.
- The `Layer::instantiatePrimitive` / `World::instantiatePrimitive`
  duplicated factory pattern is replaced by a shared `Registry<T>`, also
  used for the new `LayerBuildStep` factory.
