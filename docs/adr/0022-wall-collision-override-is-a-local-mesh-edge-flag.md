# ADR-0022: Wall collision override is a local mesh-edge flag, not an arrangement concept

**Status:** Superseded by ADR-0028
**Date:** 2026-08-25

## Context

ADR-0006 makes wall collision fully geometry-computed: Border edges always
block, Step edges block above a height threshold, author intent plays no
part. We want authors to override that per wall.

"Wall" has no authored identity anywhere today. `ArrangementEdge` and
`ArrangementWall` (`Arrangement.h`) carry no back-reference to the Primitive
or Ring edge that produced them, and `Ring`/`ClosedPolygon` is a plain vertex
list with no per-edge storage. Attaching an override therefore means
inventing a new authored layer, and where it lives is the real decision.

Every Primitive kind but Mesh regenerates its whole vertex list from
parameters on every edit (a Circle at default resolution has 64 edges, up to
1024 at max), with no index stability across a regeneration — there is
nothing to hang a persistent per-edge flag on. MeshPrimitive is the only kind
with authored, individually-addressed edges.

Even scoped to Mesh, "is this edge a wall" is not locally knowable: Border vs.
Step vs. no-wall is a property of the *fold* — the boolean combination of
every Primitive across every selected Layer, computed on a worker thread on
roughly a one-second cadence. A single authored edge can resolve to Border
along part of its length and nothing along another, if a different Primitive
cuts across it. The Mesh Edge sub-mode UI needs an answer while the user is
still editing, with no generation to consult.

## Decision

The override is a `collides` flag stored per edge on a MeshPrimitive's mesh,
editable only when `willpower::geometry::Edge::getConnectivity()` is
`External` — the edge is used by exactly one polygon in the Primitive's own
topology, i.e. it sits on the silhouette of the authored shape. `Internal`,
`Orphaned`, and `Invalid` edges are fixed `false` and not editable. This is a
purely local, instantly-computable proxy for "this is a wall edge" that needs
no generation data — it usually, but not always, agrees with the
Arrangement's own cross-primitive Border classification.

`External` edges default `collides = true`; everything else defaults
`false`, including on load for files predating this feature, since the
default is derived from connectivity rather than stored state.

Where the edge — or any sub-segment it is split into by the fold intersecting
another Primitive — still produces an `ArrangementWall`, `collides` replaces
the Border-always-blocks and clearance rules for that segment. It does not
override physical traversability: a `FloorStep` above the player's maximum
step height blocks traversal from its lower face to its higher face, but the
height limit never blocks traversal downward. A clipped shared boundary
contributed by coincident Mesh edges from two different Primitives is internal
to their combined authored geometry and has authored collision fixed off, even
when both source flags are `true`; the directional maximum-step rule still
applies to it. The flag never
synthesizes a wall where the fold produces none: if a Union with another
Primitive erases the wall geometrically, the flag has no effect there.

## Consequences

- Scope is MeshPrimitive only. Rectangle, Circle, Torus, and Superformula
  Primitives keep today's fully-computed collision, permanently, unless a
  later feature gives them stable authored edge identity too.
- "Border" now has two related but distinct meanings in this codebase: the
  Arrangement's cross-primitive, fold-level Border edge (`docs/glossary.md`),
  and this feature's local, single-polygon Mesh edge. `CONTEXT.md`'s **Wall
  collision override** entry names this explicitly to head off confusion.
- Wall synthesis from a flat, fold-erased edge was considered and rejected —
  it would mean generating new render/collision geometry from a boolean
  flag, a materially bigger change than toggling collision on walls the fold
  already produces. It remains available as a future, separately-designed
  feature if wanted.
- Structural mesh edits need an explicit inheritance rule since no edge
  identity survives them: splitting an edge copies its flag to both halves;
  deleting/merging keeps the surviving edge's own prior value and discards
  the removed edge's, with no combine logic.
