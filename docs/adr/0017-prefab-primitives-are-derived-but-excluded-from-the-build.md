# ADR-0017: Prefab Primitives are derived Primitives excluded from the build

**Status:** Accepted
**Date:** 2026-08-21
**Extends:** ADR-0014 (Primitives are derived from LayerBuildSteps), ADR-0015 (steps declare editing capabilities)

## Context

The `DefinePrefabs` step defines Prefabs — named collections of Primitives
authored as a unit — for later steps to place. It places nothing itself.

Read literally, "defines but does not place" means its Primitives never
enter the Layer at all and live only in the step's own storage. That
reading is expensive. Every authoring surface in the editor is written
against `Layer::getPrimitives()` and addresses Primitives *by index into
that collection*: hover, rubber-band selection, Select All, the ghost, the
`wp::geometry::Mesh` proxy of ADR-0016, step fading, and every undoable
action. Primitives held privately by a step are invisible to all of it, so
"editing a Prefab is the same as editing Primitives in a `PrimitiveField`
step" would be true of the user's experience and false of the code — a
second selection space, a second render path, and a second owner of the
mesh proxy.

The obstacle to the other reading is that Primitives in the Layer reach the
boolean fold. The editor installs a `PrimitiveFilter`, but nothing else
does: `Document` is its only caller, so a `.world` loaded by the game
applies no filter at all. Exclusion from the build cannot rest on it.

## Decision

A Prefab's Primitives are **ordinary derived Primitives**.
`DefinePrefabs::execute()` appends the currently-selected Prefab's
Primitives to the Layer exactly as any step appends its output, and they
are selectable, editable, mesh-editable and undoable like any others.
"Does not place anything" means *contributes no world geometry*, not *has
no derived Primitives*.

Four rules keep that safe.

**Selection is a lifetime rule, not a filter.** Which Prefab is being
edited is ephemeral editor focus, like `Layer::mActiveStepIndex` and
`World::mActiveLayerIndex`: never serialized, and unselected after
construction, copy, and load. Outside an authoring session nothing is
selected, so `execute()` emits nothing and the fold is untouched *by
construction* rather than by a filter the app does not install. The
editor's `PrimitiveFilter` then excludes prefab Primitives
unconditionally — including while their own step is active — so they never
contribute geometry to the world preview either.

**The `execute()` contract narrows.** A step no longer reads the raw
derived collection; it reads only the Primitives that participate in the
build. `Layer` already keeps `mPrimitiveSteps` in lockstep with
`mPrimitives`, so it can answer which step produced a Primitive without a
flag on the Primitive itself. A later step therefore *cannot* observe
prefab Primitives, rather than being trusted not to look.

**Primitive storage is virtualized onto `LayerBuildStep`.** `Layer`
currently reaches storage by concrete type — `dynamic_cast<PrimitiveField*>`
in `addPrimitive`, and in `findOwningField`, through which
`releasePrimitive`, `removePrimitive`, `removePrimitives` and
`replacePrimitive` all route. With a second step type owning Primitives,
every one of those operations would fail on a prefab Primitive. The base
class gains the storage operations `Layer` needs — adopt, release, replace,
and "do I own this?" — `PrimitiveField` implements them over its list,
`DefinePrefabs` over the selected Prefab's, and `findOwningField` becomes
`findOwningStep`. ADR-0015 already ruled this `dynamic_cast` out as a
capability check in the editor; this removes the last of it from `core`.

**The capability predicates become state-dependent.** `DefinePrefabs`
answers both of ADR-0015's predicates with "is a Prefab selected?". With
none selected there is nothing to edit and nowhere for a new Primitive to
go, so both are honestly no, and the editor's existing gates — Create
Primitive, and the Mesh draw tool's unavailability reason — report "select
a Prefab first" as an unavailable action rather than throwing at the
storage call. This extends ADR-0015 deliberately: the predicates were
written as questions every step *type* answers, and they remain questions
the step answers about itself, now from its own state.

**Rendering is a rule of its own.** Prefab Primitives render only while
their `DefinePrefabs` step is the active step, regardless of
`showAllStepPrimitives`. A Prefab is authored against a tile guide centred
on the origin (ADR-0018) and so bears no spatial relationship to the level
content around it; the existing fade rule exists to give context about a
neighbouring step's output, and a Prefab drawn faded across the origin
during unrelated work is noise, not context.

## Considered alternatives

**Prefab Primitives live only in the step.** Faithful to "defines but does
not place", and no risk of leaking into the build. Rejected: it duplicates
the editor's entire Primitive authoring stack against a second collection,
which is the cost ADR-0016 already paid once for a second representation of
geometry and does not want to pay again for a second representation of
ownership.

**Mark prefab Primitives so build-time consumers skip them.** Rejected:
an opt-out every future step author must remember to honour is the shape of
defect ADR-0015 exists to prevent. Narrowing what `execute()` can see makes
the guarantee structural instead.

**Keep the fold exclusion in the editor's `PrimitiveFilter` alone.**
Rejected on a fact: the app installs no filter, so a saved world would fold
prefab geometry into the level.

**Add a second `dynamic_cast` for `DefinePrefabs`.** Smallest diff.
Rejected: it fails the way ADR-0015 described — silently, at the point
where an authoring gesture does not take — and costs another branch in five
call sites per future step type.

**Give each Prefab its own `PrimitiveField` so the existing `dynamic_cast`
finds one.** Zero `Layer` churn, and "a Prefab is a named PrimitiveField"
is nearly true. Rejected: `getOwningStepIndex` and every visibility rule
hanging off it still need the real owning step, so it buys a small diff by
making the ownership graph lie.

## Consequences

- Undo must capture and restore the selected Prefab — as `UndoData`
  already does for `activeMeshPrimitive` — and must record which step as
  well as which Prefab, since a Layer may hold several `DefinePrefabs`
  steps. Without it every undo would empty the prefab view. (`Layer`'s
  active step index is *not* captured today and already resets to 0 on
  undo; that is pre-existing and untouched here.)
- Opening a file lands on a `DefinePrefabs` step with no Prefab selected
  and nothing visible, until one is picked from the panel. This is the
  lifetime rule working, not a missing default.
- `showAllStepPrimitives` gains an exception, so the rule documented on
  `editor::primitiveVisibleForActiveStep` is no longer uniform across step
  types.
- Creating a step now needs a type picker driven off the `LayerBuildStep`
  `Registry`, which must therefore be able to enumerate what it holds.
  `Actions.cpp` naming a concrete step type would be the same coupling in
  the opposite direction.
