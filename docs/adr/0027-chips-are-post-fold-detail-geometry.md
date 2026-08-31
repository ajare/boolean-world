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
exactly two. A later extension adopts **Corner** precisely for the trihedral
vertex where two Step walls and their horizontal face meet.

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
  wall geometry cannot bake that choice in. This mirroring applies only to the
coplanar wall remainder, however: a Chip facet is an outward-facing surface in
its own right, and flipping it when the player crosses the wall would turn its
upward normal downward.

## Decision

An Arris Chip selects one material-approved profile: Tapered, Prismatic Notch,
Pyramidal Divot, Multi-facet Spall, Stepped Fracture, V-shaped Notch, or
Trapezoidal Spall. Every profile stays inside the Chip's selected reach, so the
existing maximum-reach placement rule prevents differently shaped Chips from
overlapping. Tapered has one peak across its reach; Prismatic Notch has a
constant-depth span and end caps; Pyramidal Divot converges on one deepest
point; Multi-facet Spall adds independently varied intermediate facets;
Stepped Fracture changes depth in discrete plateaus; V-shaped Notch carries a
deep longitudinal crease; and Trapezoidal Spall uses a broad footprint on one
surface and a narrower one on the other. A Corner Chip instead truncates a
trihedral Corner: one point is placed independently on each of its three
incident edges and those points form one triangular cut face. Each triangle carries its own flat
geometric face normal; all three of its rendered vertices receive that same
normal, so the facets are not smooth-shaded. An eligible Arris may carry
multiple randomly positioned, non-overlapping Chips; a Corner carries at most
one.

Eligible **Horizontal Arrises** are convex: a `FloorStep`'s top and a
`CeilingStep`'s bottom. Eligible **Vertical Arrises** are the overlapping upright edge shared
by two walls whose angle on their shared canonical front side is between 225°
and 315°, inclusive. `OrientArrangementWall` remains the authority for each
wall's normal; in particular, a `Border` wall faces toward its polygon. The
sign of each normal against the other wall's ray selects the minor or major
sector. This is necessary because an unsigned normal angle reports 90° for
both a 90° corner and the 270° front-side corner at `world-test-1.yaml`'s world
XZ coordinate `(96, 96)`. Collinear walls do not form an Arris. Both walls must
use the same Sub-material, so
there is one unambiguous generation configuration and facet material; a
mixed-material corner is skipped. A wall whose ADR-0022 visibility override is
off participates in neither kind of Arris.

An eligible **Corner** is the endpoint shared by exactly two same-kind Step
walls and the same bitten horizontal face: the top of two `FloorStep` walls or
the bottom of two `CeilingStep` walls. The walls must satisfy the same
225°–315° front-side-angle rule. The Step wall's Sub-material governs the
Corner; the horizontal face's Sub-material does not override it.

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
A Vertical Chip removes one triangular footprint from each incident wall. Its
deepest point lies opposite the normalized sum of the two canonical wall
normals. Canonical normals point out of wall material (a Border's points toward
its polygon), so this material-side bisector centres the gouge on the shared
edge and sends it inward rather than making it protrude.
Four tapered facets connect that point to the two wall-footprint points and the
two reach endpoints on the shared edge. A Corner Chip removes one triangle
from each of its three incident surfaces and adds the triangular face joining
its three edge points. A wall bitten on any combination of its four Arrises or
Corners is suppressed and re-earcut once from the combined boundary.

Chip generation is authored per Sub-material: minimum eligible Arris length,
minimum/maximum depth, minimum/maximum reach, minimum centre spacing, an Arris
probability, and a non-empty list of allowed Arris Chip types, plus
minimum/maximum Corner distance and a separate Corner probability. Reach is
total length along the Arris and width is half-reach. The
maximum width may not exceed 2 world units or the minimum eligible Arris
length, and minimum spacing is at least maximum reach plus 0.1, structurally
preventing overlap.
Depth, reach, and Corner distance default to ranges of 1–3, minimum Arris
length to 2, spacing to 3.1, and both probabilities to zero. The governing
Sub-material is the **wall's**:
`palette[wall.paletteIndex].wallMaterialId`, which
`BuildArrangementWalls` already resolves to exactly one entry per wall (the
lower face for a `FloorStep`, the higher for a `CeilingStep`), so no tie-break
rule is needed. That same Sub-material supplies the new facet's own material,
which means Chip triangles batch into a mesh bucket that already exists and the
feature adds no buckets anywhere in the world.

Those parameters are **pre-resolved into a parallel array on `ArrangementResult`**,
fed from a transient field on `arr::ArrangementPrimitive`. They are explicitly
not stored on `PrimitivePropertySet`, which is `Serializable` and would write
them into the World file. Resolution runs at snapshot time through a
`setChipParametersResolver` callback on `WorldDataGenerator`, mirroring the
existing `setPrimitiveFilter`/`refreshPrimitiveFilter` pattern;
`ArrangementWorldDataGenerator` carries the same hook for `Preview3D`.
`snapshotGenerationInput` runs on the calling thread rather than the worker, so
that callback is main-thread and may consult the editor's `ProcMaterialLibrary`
directly.

For each eligible Arris, its length and minimum spacing determine a maximum
slot count. Independent probability trials choose a count up to that maximum;
that many centres are then placed irregularly while preserving minimum spacing
and room for maximum-width Chips. Each Chip draws depth and reach independently
from the authored ranges and independently selects one entry from the governing
Sub-material's type list. All profiles remain bounded by that reach. All draws
derive from a pure hash of the Arris's
fixed-point endpoints — never a sequence, an index, or a frame counter — so
regeneration and unrelated edge renumbering cannot make Chips crawl. A Corner
uses the same geometry-derived scheme, with independent draws for its three
edge distances.

Chips are clamped to fit rather than skipped. Horizontal depth and reach shrink
to satisfy the Arris's length, the distance to the nearest other horizontal
face boundary, and wall height. A Vertical Arris uses the walls' overlapping
height as its length and clamps depth to both walls' horizontal lengths. A
Corner is skipped when its configured minimum distance exceeds any incident
edge; otherwise each random maximum is clamped to that edge's length. A Chip is
dropped below a minimum resulting size.

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
every visual tweak and gives no per-material variation. A World property would add unrelated global authoring state and serialize it
into the World file, which the brief rules out. The Sub-material was chosen because "how much
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
- `Border` walls still have no eligible Horizontal Arris, but pairs of them can
  form eligible Vertical Arrises at concave corners. This makes Chips visible
  even in worlds without floor or ceiling steps.
- Every renderer surface emission gains a suppression check, and every
  generation gains a pass over all walls with a face-boundary distance query per
  candidate Chip. That runs every five seconds in game, and on every undoable
  action in the editor.
- "One flat facet" above is one facet in the sense that matters — flat rather
  than rounded, and cut at a single angle — but it is two triangles, and this
  is forced rather than chosen. The wedge a Chip removes is a tetrahedron:
  two corners on the Arris, one on the horizontal face and one down the wall.
  A single plane cutting a dihedral edge meets that edge in exactly one
  point, so it can taper to nothing at one end only and would need an end cap
  at the other; a Horizontal Chip's cut surface therefore hinges at its
  deepest cross-section. A Vertical Chip instead has four facets meeting at
  its deepest bisector point. Implemented in `BuildChipDetail` (#278).
- The 45° bevel remains fixed in code. Count, placement, depth and reach now
  vary deterministically from each Arris's endpoints under the governing
  Sub-material's constraints.
- An unrelated edit that splits an Arris into two arrangement edges gives each
  resulting Arris its own endpoint-derived count and placements. This follows
  from endpoint seeding and is accepted.
