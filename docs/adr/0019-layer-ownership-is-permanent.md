# ADR-0019: Layer ownership is permanent — nothing moves between Layers

**Status:** Accepted
**Date:** 2026-08-21
**Amends:** ADR-0014 (Primitives are derived from LayerBuildSteps)
**Relates to:** ADR-0013 (Layer is a first-class owning collection)

## Context

`World::movePrimitiveToLayer` and `World::moveTriggerLineToLayer` move an
authored object from one Layer to another, exposed in the editor as a Layer
picker on the Edit Primitive view. ADR-0014 explicitly blessed the Primitive
half: "Moving a Primitive to another Layer targets that Layer's first step,
which is guaranteed to be a `PrimitiveField` step."

That contradicts the rest of ADR-0014. Its central claim is that a Primitive
is not an independent object — it is an entry in some step's argument list,
and it exists only because that step produced it. Moving one between Layers
takes it out of one recipe and injects it into a different recipe's step 0.
It is the only operation in the codebase that treats a Primitive as having
an identity independent of the step that made it. `Layer::releasePrimitive`
and `PrimitiveField::releasePrimitive` — which hand a Primitive back to a
caller who then owns it — exist solely to serve it, and a derived object
that nothing owns independently should never need to be handed to anybody.

ADR-0017 makes the contradiction concrete rather than theoretical: with
`DefinePrefabs`, the operation lets a Primitive be dragged out of a Prefab
and into an unrelated Layer's build.

## Decision

Nothing moves between Layers. `movePrimitiveToLayer` and
`moveTriggerLineToLayer` are removed, along with the editor actions, the
Layer picker in the Edit Primitive view, `Layer::releasePrimitive`,
`PrimitiveField::releasePrimitive`, and their tests. A Layer's ownership of
a Primitive or a WorldTriggerLine is fixed for that object's lifetime.

WorldTriggerLines are included even though the ADR-0014 argument does not
reach them — a Layer owns them outright rather than deriving them from
steps, so moving one is not incoherent in the same way. They go because the
rule is worth stating once, about Layers, rather than as a property that
happens to hold for one of the two things a Layer owns.

**Nothing replaces it.** A Primitive authored on the wrong Layer is deleted
and recreated on the right one. This is a real loss of convenience and is
accepted as one; a copy-to-Layer path would be a new feature with its own
semantics, not a rename of this one.

## Consequences

- The paragraph in ADR-0014 sanctioning moves to another Layer's first step
  no longer describes the system.
- Removing this deletes the last caller of the release-ownership pattern,
  so `Layer` and `PrimitiveField` lose the ability to give up a Primitive
  without destroying it. Any future operation wanting that should be read
  as a proposal to reopen this decision.
- Best done before the ADR-0017 work: it is self-contained, depends on
  nothing in that change, and shrinks rather than grows it.
