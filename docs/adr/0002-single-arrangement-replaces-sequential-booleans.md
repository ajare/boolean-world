# ADR-0002: One planar arrangement replaces the sequence of boolean operations

**Status:** Accepted
**Date:** 2026-08-16

## Context

`Clipper::clip` (`Clipper.cpp:345`) computes world geometry as a sequence of
Clipper2 boolean operations. For `N` primitives forming `R` runs (a run being
the primitives between two `Union`s):

| Step | `Clipper64::Execute` calls |
| --- | --- |
| `generateIntermediateClippings` — fold each run | N |
| Union the runs → border | R − 1 |
| XOR the runs → arrangement template | R − 1 |
| `calculateCombinedPolygons` — Difference *and* Intersection of each run against the template | 2R |

That is roughly `N + 4R` boolean operations. The fold accumulates a growing
path set and re-intersects it each step, so it is quadratic in accumulated
size. Worse, the same geometric intersections are recomputed on every pass,
each time allocating fresh vertex records.

The XOR-template-then-cut steps exist only to recover *which primitive owns
which region* — information the sequence of boolean operations destroys and
then has to reconstruct.

## Decision

Compute a single planar **arrangement** of all primitive edges, then classify
each face.

Every face of the arrangement is, by construction, wholly inside or wholly
outside every primitive. So each face carries a **membership** set, and the
boolean result becomes a predicate evaluated per face rather than a sequence of
geometric operations. This is the same left-fold as ADR-0001, evaluated
pointwise.

Membership is computed as a signed winding number per primitive, reduced by
that primitive's fill rule (primitives may be multi-contour and
self-intersecting). Windings are found for one face directly, then propagated
by BFS over the face adjacency graph: crossing an edge changes the winding of
only the primitives owning that edge, usually one. O(F + E) with small deltas.

Construction uses a grid broad phase — `wp::AccelerationGrid` already exists —
plus exact integer pair intersection, rather than a Bentley–Ottmann sweep. At
the target scale (ADR-0003) the asymptotic win is irrelevant and the sweep's
degeneracy handling (many edges through one point, collinear overlaps) is a
large source of risk.

Adjacent faces resolving to the same primitive are **not** merged. It would
reduce triangle count, but it is not needed for correctness — a wall is emitted
only where incident faces differ — and it complicates the model. Revisit if
triangle counts bite.

## Consequences

Every output currently computed by a separate pass becomes a read off one
structure:

| Output | Before | After |
| --- | --- | --- |
| Border polygons | Union of runs (R−1 clips) | Edges where `solid` differs |
| Arrangement polygons | XOR template + 2R clips | The faces themselves |
| Per-polygon primitive | Inferred from a run's first primitive | Highest-priority member of the face |
| Edge/face adjacency | Rebuilt from border polygons | Native to the arrangement |
| Vertex properties | Interpolated, propagated, guessed | Face-level; no interpolation |
| Triangulation dedup | Float-centroid `std::map` per triangle | Faces are disjoint; nothing to dedupe |

- Walls stop being a special case. An edge between solid and empty is a border
  wall; an edge between two solid faces with differing heights is a step wall.
- `Clipper::clip`, `ClipperUtils`' traversal and interpolation helpers, and the
  dead `SplitTouchingPolygon` workaround are all removed.
- Clipper2 remains a dependency for **path offsetting** (`PathPolygon.cpp`
  uses `InflatePaths`) and for the independent `floored` application. This
  decision replaces Clipper2 as the boolean engine of the World clip pipeline
  only.
- New risk concentrates in one place: the arrangement builder. It must be
  correct under degeneracy (coincident vertices, collinear overlapping edges,
  edges meeting at a point). ADR-0003 covers the numeric approach; this code
  needs direct unit testing rather than being validated only end-to-end.
