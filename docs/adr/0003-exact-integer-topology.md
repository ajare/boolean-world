# ADR-0003: Exact integer topology, fixed-point coordinates, snap-rounding

**Status:** Accepted
**Date:** 2026-08-16

## Context

The world is 8192 units across, centred on the origin, so coordinates span
±4096. The former `BW_CLIPPER_SCALE` was 1000, giving a grid quantum of 0.001
world units and an integer range of ±4.1e6.

Downstream types are `float`: `wp::Vector2` holds `float x, y`, so
`ClippedPolygon` and `PolygonGraphVertex` are float. At ±4096 a float ULP is
about 4.9e-4 against the 1e-3 quantum — roughly two ULP of margin. It holds,
but there is no room to spare.

`PolygonGraph` currently keys vertex identity on those floats
(`PolygonGraphVertex::operator==` compares `x` and `y` directly, and its hash
is built from them), which puts vertex merging — a topological decision — at
the mercy of two ULP.

## Decision

- **Topology keys on exact integers, never floats.** Vertex identity, edge
  identity and face adjacency are decided on `int64` grid coordinates. Float
  appears only when writing render output.
- **Keep fixed-point `int64` at scale 1000.** Intersection determinants peak
  around 2^47 against `int64`'s 2^63, so there is ample headroom. Going finer
  is wasted: float output cannot carry it.
- **Snap-round computed intersection points to the grid.** All output vertices
  are exactly representable, and the arrangement stays topologically
  consistent with what consumers receive.
- **Use exact predicates** for orientation and segment intersection tests.
  Operands are bounded well inside `int64`, so exact integer arithmetic
  suffices — no adaptive floating-point predicates needed.

## Consequences

- The vertex-collapse failure class disappears: two distinct grid points can no
  longer merge, and two identical points can no longer fail to merge.
- Snap-rounding can move an intersection by up to half a quantum (0.0005
  units), which can in principle create new incidences — a snapped vertex
  landing on another edge. The arrangement builder must therefore treat
  snapping as part of construction and re-check affected edges, not as a
  post-process.
- Snapping can also produce zero-length edges and zero-area faces. These must
  be removed during construction, before classification, or they will reach the
  triangulator.
- Consumers keep receiving float coordinates, unchanged. The two-ULP margin at
  the world edge remains, but it now affects only rendered positions, never
  topology.
- The scale and coordinate conversion contract survived as native fixed-point
  arrangement utilities; the Clipper-named macros and the z-packing macros in
  `Defines.h` were subsequently removed (ADR-0004, ADR-0010).
