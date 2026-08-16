# ADR-0007: Remove culling from generation

**Status:** Accepted
**Date:** 2026-08-16

## Context

`WorldDataGenerator::getPrimitives` culls before folding: a broad phase (none /
circle / box, via the primitive acceleration grid) then a narrow phase (none /
circle / view cone).

In practice both the game (`app/src/Map.cpp:65`) and the editor
(`editor/src/Document.cpp:255`) select `BroadPhaseCulling::Circle` with
`NarrowPhaseCulling::None`. The circle has radius `mViewerViewDistance`,
default 256, in a world 8192 across — so most primitives are dropped from every
generation.

This is invalid under the fold of ADR-0001, which is non-local: dropping a
primitive changes the meaning of every primitive above it in priority order.
Take `P0`, a large room with `Union`, and `P1`, a small distant shape with
`Intersection`. Folded, the result is that small distant region. Cull `P1` for
distance and the result is the whole of `P0` — and the difference is not
confined to where `P1` was. It is everywhere, including on screen.

It survives today only because worlds are authored as mostly-local `Union`s
with local `Difference`s. It fails whenever an author uses `Intersection` or a
wide-reaching `Difference` — the very style the current engine invites, since
operations have no named target.

At the target scale (~123 primitives, cadence of at least one second, off
thread) culling buys nothing.

## Decision

Remove culling. Fold every primitive in the generation's selected layer set
(ADR-0009), every generation.

Removes `BroadPhaseCulling`, `NarrowPhaseCulling`, `primitiveInView`,
`getViewVertices`, and the culling half of `getPrimitives`.

**Retained**, and explicitly not part of this decision:

- `DynamicWorldDataGenerator::preparePrimitives:135`, which updates vertex
  positions only for primitives that are *not* visible. This is the game's
  central constraint — the world changes only where the player is not looking —
  and it is sound in a way culling is not. It keeps every primitive in the
  fold and merely uses older geometry for visible ones, so the result is a
  complete, valid world in which some shapes sit at an earlier position.
  Culling, by contrast, removes primitives from the fold and so corrupts the
  result everywhere.
- `canCommit`'s visibility gate, which holds a finished generation back while
  any primitive it moved is on screen. Together with the above, geometry
  changes are invisible as they happen.

## Consequences

- Generation's *primitive set* becomes independent of player position. A view
  dependency remains via `preparePrimitives` (retained above), so generation is
  not fully deterministic in play. It becomes fully deterministic under
  `setAlwaysUpdateVertices(true)`, which is what the comparison harness uses —
  and which `profiler/src/Main.cpp:24` and `experiments/src/Main.cpp` already
  set. That is enough to make the ADR-0008 regression diff meaningful; without
  removing culling it would not be, because the primitive set itself would
  differ between runs.
- A whole class of position-dependent, hard-to-reproduce geometry bugs is
  eliminated rather than fixed.
- Cost grows from "primitives near the player" to "all primitives in the
  selected layers". At ~123 primitives this is immaterial; ADR-0002 removes far
  more than culling ever saved.
- Culling can return if worlds grow. If it does, it must be made semantically
  sound first — for example by culling only primitives provably unable to
  affect the visible region, which requires reasoning about the fold rather
  than about distance.
