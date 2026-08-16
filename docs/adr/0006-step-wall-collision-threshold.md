# ADR-0006: Step walls collide above a world-level height threshold

**Status:** Accepted
**Date:** 2026-08-16

## Context

ADR-0004 makes step walls real: edges between two solid faces with differing
`floorZ` or `ceilingZ` now render geometry. Previously `is2Sided()` was always
false, so no such wall existed — the player walked across a floor-height
discontinuity and `getContainingTriangle` simply reported a different height,
teleporting them vertically.

Collision today is border-only. `circleIntersectsBorder` (`WorldData.cpp:334`)
tests the border polygons; interior edges block nothing.

Three models were considered: walls are visual only and collision stays
border-only; all walls collide; or a step collides only above a height
threshold.

## Decision

A step edge collides when its **step height** —
`|face[0].floorZ - face[1].floorZ|` — exceeds a **step threshold**. Below the
threshold the player steps up onto it.

The threshold is a world-level property. Existing worlds default it to
infinity, reproducing today's border-only collision.

## Consequences

- The arrangement gives the step height per edge exactly, so the test is
  essentially free.
- Existing worlds are unaffected until an author opts in by lowering the
  threshold, which keeps the ADR-0001 regression comparison honest during the
  port.
- "All walls collide" was rejected: every minor step would become an obstacle
  and traversal would need every world re-authored. "Visual only" was rejected
  because it renders waist-high walls the player strolls through.
- Collision structures change with it. Wall proximity gets a grid built over
  **wall edges only** — a much smaller set than all border edges — which
  removes two unaccelerated per-frame functions: `pointInPolygon`
  (`WorldData.cpp:248`) walks every border polygon linearly, and
  `getNearestBorderDistance` (`WorldData.cpp:357`) walks every polygon and
  every edge.
- Point location stays triangle-based: rendering needs a triangulation anyway,
  each triangle carries its face index, and the existing triangle grid answers
  "which face am I in" in O(1).
- A threshold introduces the usual stepping-up questions — whether the step is
  instant or animated, and how it interacts with falling. Those are gameplay
  decisions, not geometry, and are out of scope here.
