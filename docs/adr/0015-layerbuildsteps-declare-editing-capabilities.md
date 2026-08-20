# ADR-0015: LayerBuildSteps declare whether their Primitives may be directly edited or added to

**Status:** Accepted
**Date:** 2026-08-20
**Extends:** ADR-0014 (a Layer's Primitives are derived from its LayerBuildSteps)

## Context

ADR-0014 made a Layer's Primitives derived: recomputed from scratch by
re-running the enabled steps in order, with the step list — not the
Primitives — being what a Layer serializes. It preserved the manual
Create/Edit Primitive UI by having it mutate the target Layer's
`PrimitiveField` step's embedded list, then trigger a rebuild.

That worked because `PrimitiveField` is the only step type, and its
"arguments" *are* its Primitives, so an in-place mutation of a derived
Primitive and a mutation of the step's arguments are the same act. Every
other step type breaks that identity. A step that synthesises its
Primitives holds arguments of some other shape entirely; an edit made to
one of its output Primitives lives only in the derived cache and is
recomputed away by the next rebuild — which any step toggle, layer
change, undo, or regeneration can trigger.

The editor has no way to know this. `Layer::findOwningField` answers
"which `PrimitiveField` owns this?", which is a question about a concrete
type rather than about a capability, and the editor's authoring paths do
not consult it at all before offering an edit. Adding Mesh mode, which
edits Primitive geometry far more intensively than anything before it,
made the gap worth closing rather than deferring.

## Decision

`LayerBuildStep` gains two capability predicates, which every step type
answers for itself:

- whether its Primitives may be **directly edited** in place, and
- whether it **accepts new Primitives**.

`PrimitiveField` answers yes to both. These are separate questions: a
future step type could reasonably absorb edits to what it produced
without accepting arbitrary new Primitives appended to it.

The editor asks the step. Nothing may inspect the concrete step type to
answer either question — `dynamic_cast<PrimitiveField*>` must not become
the de facto capability check, which is the shape this would otherwise
drift into as step types multiply.

The direct-editing predicate governs **both editor modes**, not only the
new one. Primitives belonging to a step that disallows direct editing
become inert and render faded in Primitive mode exactly as in Mesh mode.
The accepts-new-Primitives predicate gates creation: the Mesh mode draw
tool is unavailable when the currently-selected step answers no, and new
Primitives are added to the **currently-selected** step rather than
always to the Layer's first one.

With `PrimitiveField` as the only step type, this is behaviour-neutral
today. That is precisely the argument for doing it now: it is the
cheapest it will ever be, and the alternative ships the first procedural
step type with a known defect in the older, more heavily used path.

## Considered alternatives

**Accept transient edits.** Allow editing anything, and let a rebuild
discard what it discards. Rejected: an edit that visibly applies and then
evaporates is worse than one that was never offered, and with the world
regenerating when an edit commits, "eventually" would often mean
"immediately".

**Attempt write-back, refuse at commit.** Ask the step to absorb the
edit when the edit completes, and report failure if it cannot. Rejected
in favour of deciding eligibility up front: a Primitive the user can
select, drag and manipulate but never successfully commit is a worse
experience than one that was never eligible, and it wastes the work.

**Gate Mesh mode only.** Rejected — see above. A capability that one mode
honours and another quietly violates does not describe an invariant.

## Consequences

- Every new `LayerBuildStep` type must answer both predicates
  deliberately. There is no safe default that suits all step types, so
  the base class should make the choice explicit rather than inheritable
  by accident.
- The editor gains a class of "visible but not editable" Primitives, so
  every authoring surface needs a way to say *why* something is inert.
  Fading is the visual half; the panels carry the explanation.
- `Layer::findOwningField` remains for what it is actually for —
  locating the step that owns a Primitive for removal and replacement —
  and stops being used as a proxy for editability.
- Because the accepts-new-Primitives predicate targets the selected step
  rather than the first step, creating a Primitive while a step that
  refuses them is selected is now an unavailable action rather than a
  silent redirection to step 0.
