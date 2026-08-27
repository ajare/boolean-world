# ADR-0027: Chips are post-fold detail geometry in a side channel on the world snapshot

**Status:** Accepted
**Date:** 2026-08-27
**Related:** ADR-0002 (one planar arrangement replaces sequential booleans), ADR-0005 (`WorldData` is an immutable snapshot with a property palette), ADR-0022 (wall collision override is a local mesh-edge flag), ADR-0025 (the editor's 3D preview renders through the real render pipeline)

## Context

World geometry is generated in three stages: `BuildArrangement` folds the
selected Primitives into a planar arrangement, `BuildArrangementTriangles`
earcuts every solid face, and `BuildArrangementWalls` emits one wall per edge
where solidity or floor/ceiling height differs. `ArrangementWorldData` bundles
the three and publishes them as an immutable snapshot.

Nothing in that pipeline names the seam where a wall meets the horizontal
surface it abuts. A `FloorStep` wall's `maxZ` happens to equal the upper face's
`floorZ`; that the two therefore meet along a line is arithmetic nobody records.
`CONTEXT.md` now calls that seam an **Arris** and the damage cut into it a
**Chip** — deliberately not "edge", which already means both an arrangement edge
and a MeshPrimitive Ring edge, and deliberately not "corner", which implies a
vertex joining three or more polygons rather than a dihedral edge joining
exactly two.

Chips must be derived from the fold's output rather than authored: a World file
holds no record of any Chip. That leaves the question of where the geometry
lives, and it is not a free choice — the three existing outputs are all
index-based. An `ArrangementTriangle` is three *arrangement vertex* indices; an
`ArrangementWall` is one edge index plus a `minZ`/`maxZ` pair. Neither can
express a vertex that is not in the arrangement, which every Chip introduces.

Two further constraints emerged from the existing renderer, and they are what
actually shape the contract:

- `WorldRenderer::updateHorizontalDataProvider` emits **two** triangles from
  each `ArrangementTriangle` — a floor at `floorZ` and a ceiling at `ceilingZ`.
  A Chip on a `FloorStep` top arris affects only the floor copy, so suppressing
  the triangle would punch a matching hole in the ceiling.
- `WorldRenderer::updateWallDataProvider` re-picks, **every frame**, whether a
  wall shows its authored material or the reserved white back-face material,
  from the player's position. Generation has no player position, so replacement
  wall geometry cannot bake that choice in.

## Decision

A Chip is a tapered chamfer cut into a convex Arris: one flat facet, at its
deepest in the Arris's centre and tapering to nothing at both ends, so it needs
no end caps and degenerates gracefully to nothing when clamped. The bevel is
45° — depth bites equally into the horizontal face and down the wall. One Chip
per eligible Arris, at its centre.

Only **convex** Arrises are eligible: a `FloorStep`'s top and a `CeilingStep`'s
bottom. A concave Arris is an inside corner, and chipping one would carve into
solid material rather than remove a wedge from it — a different and much larger
feature. `Border` walls have two concave Arrises and therefore never chip. A
wall whose ADR-0022 visibility override is off is skipped entirely, rather than
having its floor bitten to expose a facet nothing draws.

The geometry lives in a **detail channel**: a new member of
`ArrangementWorldData`, built in its constructor alongside `mTriangles` and
`mWalls`, and so on the generation worker thread at no main-thread cost. The
channel carries suppression keys of the form `{FloorOfFace | CeilingOfFace |
Wall, index}` together with replacement triangles tagged with the same source,
carrying position, normal and UV explicitly in arrangement space with Z up.
Renderers skip a suppressed surface and emit its replacements in its place —
which leaves the floor/ceiling rule and the per-frame `facesPlayer` rule exactly
where they already live, in the renderer. Horizontal replacements are produced
by subtracting the Chip's footprint from the face's boundary polygon and
re-running earcut for that face, rather than by clipping individual triangles.

The Chip's two dimensions — depth into the material, and reach along the Arris —
are authored per Sub-material, bounded by its own limits in the manner of
Embossing rather than by a Technique schema. The governing Sub-material is the
**wall's**: `palette[wall.paletteIndex].wallMaterialId`, which
`BuildArrangementWalls` already resolves to exactly one entry per wall (the
lower face for a `FloorStep`, the higher for a `CeilingStep`), so no tie-break
rule is needed. That same Sub-material supplies the new facet's own material,
which means Chip triangles batch into a mesh bucket that already exists and the
feature adds no buckets anywhere in the world.

Those sizes are **pre-resolved into a parallel array on `ArrangementResult`**,
fed from a transient field on `arr::ArrangementPrimitive`. They are explicitly
not stored on `PrimitivePropertySet`, which is `Serializable` and would write
them into the World file. Resolution runs at snapshot time through a
`setChipSizeResolver` callback on `WorldDataGenerator`, mirroring the existing
`setPrimitiveFilter`/`refreshPrimitiveFilter` pattern;
`ArrangementWorldDataGenerator` carries the same hook for `Preview3D`.
`snapshotGenerationInput` runs on the calling thread rather than the worker, so
that callback is main-thread and may consult the editor's `ProcMaterialLibrary`
directly.

Chips are clamped to fit rather than skipped: depth and reach shrink to satisfy
all three of the Arris's length, the distance from the Arris to the nearest
other boundary of the horizontal face, and the wall's height. A Chip is dropped
below a minimum size.

Any future per-Chip variation must derive from a pure hash of the Arris's
fixed-point endpoints — never a sequence, an index, or a frame counter.
Arrangement edges renumber whenever anything in the world changes, and the game
regenerates on a five-second schedule, so an index- or sequence-seeded Chip
would visibly crawl.

Chips are visual only. Collision, `getFloorHeight`, `getContainingFaceIndex`,
`pointInTriangle`, surface picking and the editor's 2D viewport all continue to
read the unchipped `mTriangles`/`mWalls`.

## Considered alternatives

**Splice the Chip into the arrangement as a real `ArrangementFace`** — the
literal reading of the original brief: expand a vertex on the arrangement edge
and cut it into the faces that use it. Attractive because chip walls and floors
would fall out of the existing builders with no new rendering code at all.
Rejected for three reasons. It requires hand-maintaining invariants
(`edge.face[]` adjacency, membership bitsets, boundary edge lists, the
containment hierarchy) that `BuildArrangement` maintains by construction, on a
structure ADR-0005 makes immutable. It can only produce prismatic chips —
vertical sides and a flat bottom — because a face has one `floorZ`. And it
pollutes collision, floor height and picking, so the visual-only property would
have to be engineered back in rather than being the default.

One detail is worth recording precisely, because the brief's description does
not survive contact with the geometry: in the planar arrangement the two faces
either side of the relevant edge are the **upper and lower floor regions**, not
the wall and the floor. A cut straddling that edge would *raise* the floor on
the lower side — adding material where a Chip must remove it. The approach works
only as a single-sided cut into the upper face with a lowered synthetic palette
entry.

**Mutate `mTriangles` and `mWalls` in place.** One geometry array, so renderers
need no new code path. Rejected because both types would have to grow to carry
non-arrangement vertices, and because collision reads `mWalls` — the visual-only
separation would again have to be built rather than inherited.

**Cut Chips in `WorldRenderer` while filling vertex buffers.** Since ADR-0025
the game and the editor preview share that code, so this would still be a single
implementation, and `core` would be untouched. Rejected because it puts polygon
clipping and re-triangulation in the render library, where covering it requires
a GL context, and because the same logic would have to be written twice — once
in the horizontal provider and once in the wall provider.

**Chip size as a compile-time constant, or as a World property.** A constant
needs no plumbing at all and was the obvious first cut, but forces a rebuild for
every visual tweak and gives no per-material variation. A World property would
follow `mStepThreshold`'s established pattern, but is serialized into the World
file, which the brief rules out. The Sub-material was chosen because "how much
does this material spall" is the same kind of fact as Embossing, which is
already authored there — accepting, as the cost, `core`'s first dependency on
material data.

## Consequences

- `core` gains a dependency on material data for the first time, though only as
  pre-resolved floats behind a callback the caller supplies. `core` never sees a
  ProcMaterial catalog, and no fourth representation of that data is created.
- `arr::ArrangementResult` gains a per-palette parallel array and
  `arr::ArrangementPrimitive` a transient field. Neither type is serialized, so
  "no Chip in the World file" holds by construction rather than by discipline.
- `Preview3D` builds its own snapshot through `ArrangementWorldDataGenerator`,
  which is not a `WorldDataGenerator` and does not inherit the hook. If it is
  not given a resolver, the game will show Chips while the editor preview shows
  none — precisely the game/editor divergence ADR-0025 exists to prevent.
- ADR-0005's snapshot now carries render-only geometry alongside the geometry
  its collision and containment queries read. The snapshot remains immutable and
  the two sets remain distinct, but `ArrangementWorldData` is no longer purely a
  query structure.
- Because `Border` walls have no convex Arris, and are the majority of walls in
  most worlds, the feature is far less visible than its description suggests.
  This is correct — the base of a wall has no wedge to lose — but it is worth
  seeing on real geometry before building further on it.
- Every renderer surface emission gains a suppression check, and every
  generation gains a pass over all walls with a face-boundary distance query per
  candidate Chip. That runs every five seconds in game, and on every undoable
  action in the editor.
- The 45° bevel and the one-Chip-per-Arris-at-centre placement are fixed in
  code. Both are additive to change: a bevel-angle field, or a count and density
  field, extends the Sub-material block without disturbing anything here.
- An unrelated edit that splits an Arris into two arrangement edges yields two
  Chips where there was one, each centred on its own edge. This follows from
  seeding on edge endpoints, and is accepted.
