# ADR-0001: Preserve the priority-ordered left-fold exactly

**Status:** Accepted
**Date:** 2026-08-16

## Context

World geometry is defined by combining primitives with boolean operations. The
evaluation model is a left-fold in **priority order**, not list order
(`World::sortPrimitiveIndicesByPriority`, `DefaultWorldDataGenerator.cpp:44`):

```
result = ((P0 op1 P1) op2 P2) op3 P3 …
```

where each `opN` is primitive `PN`'s own operation. Priority is an authored
`uint8_t`, so authors control the sequence.

The model is powerful but non-local. A `Difference` primitive subtracts from
everything accumulated below it, not from a named target; a single
high-priority `Intersection` can erase most of a world. Inserting a primitive
changes the meaning of every primitive above it.

The alternative considered was an explicit expression tree — groups with their
own operators, building on the existing `PrimitiveGroup` — which is more local
and more predictable to author.

## Decision

Preserve the priority-ordered left-fold exactly. Defer any change to the
authoring model to a separate decision, taken after the geometry engine is
trustworthy.

## Consequences

- Every existing world file produces the same solid region under the new
  engine, so old and new can be diffed directly. This is the main validation
  tool for the rewrite and the primary reason for the decision.
- The fold costs nothing in the new design. Under ADR-0002 it is a per-face
  loop over the face's membership set, not a sequence of geometric operations:

  ```
  inside = false
  for p in primitives sorted by priority:
      inside = apply(p.operation, inside, p ∈ face.membership)
  face.solid = inside
  ```

- The regression comparison covers the **solid region only**. Walls are
  deliberately different, because ADR-0004 fixes `is2Sided()` and step walls
  that never rendered will now appear. Wall output must be reviewed visually,
  not diffed.
- The non-locality of the authoring model is retained, including its hazards.
  Revisiting it stays open.
