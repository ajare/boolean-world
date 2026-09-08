# Glossary — World geometry

Terms used by the World clip pipeline and its replacement. Where a term means
something different before and after the arrangement rewrite, both senses are
given.

## Authoring

**Primitive** — One authored 2D closed shape (`Rectangle`, `Regular`, `Circle`,
`Torus`, `Superformula`, `Mesh`). Carries geometry, a boolean **operation**, a
**fill rule**, a **priority**, a **layer**, and a **property set**. Procedural
Primitives tessellate directly to `vector<ComplexPolygon>`; a MeshPrimitive
derives that form from its containment tree.

**Ring** — A closed loop of authored vertices forming one boundary of a
Primitive in that Primitive's local space. It becomes a **Contour** during
World generation.

**Filled region** — A Ring together with the **Holes** it directly contains. A
filled region is a **Shell** at the root of a MeshPrimitive's containment tree
and an **Island** when directly contained by a Hole.

**Shell** — A root Filled region of a MeshPrimitive. Shell sibling interiors do
not overlap.

**Hole** — An empty Ring directly contained by a Shell or Island. A Hole may
contain Islands.

**Island** — A Filled region directly contained by a Hole. It may contain
further Holes without a semantic nesting-depth limit.

**Contour** — A closed sequence of **fixed-point vertices** forming one
boundary of a primitive. One primitive may contribute several contours,
resolved against each other by the primitive's own **fill rule**.

**ComplexPolygon** — `vector<ClosedPolygon>`; a shallow filled Ring followed by
its direct Hole Rings. For MeshPrimitive it is deterministic derived data, not
authoritative authored topology.

**Operation** — `Union`, `Intersection`, `Difference`, `XOR`. Applied to the
*accumulated result so far*, not to a named target. See **fold**.

**Fill rule** — `NonZero` or `EvenOdd`. Reduces a primitive's signed winding
number at a point to inside/outside. Per-primitive, applied *before* the fold.

**Priority** — `uint8_t` ordering value for the global fold. Values 0–248 are authored for ordinary build Primitives; 249–255 are reserved, in order, for a PrefabField's seven generated grid phases. A Prefab definition's Primitives may still use the full 0–255 range because those values establish relative order inside one generated content phase rather than becoming the instances' final fold priorities. Not a z-order: it determines meaning, not just overlap.

**Fold** — The evaluation model. Primitives are sorted by priority and combined
left-to-right: `((P0 op P1) op P2) op P3 …`, where each `opN` is `PN`'s own
operation. Order-dependent and non-local: inserting a primitive changes the
meaning of every primitive above it. Preserved exactly by the rewrite
(ADR-0001).

**Property set** (`PrimitivePropertySet`) — `floorZ`, `ceilingZ`, and floor /
ceiling / wall material indices and definitions. The renderable attributes of
a region.

**Elevation plane** — An affine height function over the World plane,
consisting of a base elevation and a two-dimensional gradient. A Primitive
supplies one independently for its floor and ceiling; zero gradient is the
existing horizontal surface. It evaluates to one elevation and one constant
up-facing normal at every World-plane position while the Arrangement remains
purely two-dimensional.

**World dependent resource** — A named Willpower Resource referenced by
authored World content and required before that World can be deserialized and
activated. The serialized list is the exact, sorted projection of all such
references, including disabled LayerBuildSteps and Prefab definitions.

## Geometry — after the rewrite

**Arrangement** — The planar subdivision induced by *all* primitive edges at
once. Its defining property: every **face** is wholly inside or wholly outside
every primitive, so membership is a property of the face rather than something
recomputed per boolean operation.

**Face** — A maximal connected region of the arrangement. Has one outer
boundary and zero or more explicit inner boundaries (**holes**). Carries a
**membership** set, a **solid** flag, and a **palette index**.

**Membership** — Which primitives contain this face, as a bitset indexed into
the *current generation's* primitive list (not the world's). Computed as a
signed winding number per primitive, reduced by that primitive's fill rule.

**Solid** — Whether a face is part of the world after the **fold** is evaluated
over its membership. The fold runs per-face, which is what makes one
arrangement equivalent to the old sequence of boolean operations.

**Border edge** — An arrangement edge whose two incident faces differ in
**solid**. The world outline. Always carries a wall.

**Step edge** — An edge between two solid faces differing in `floorZ` or
`ceilingZ`. Carries a wall spanning the difference. Never rendered before the
rewrite — see ADR-0004 on `is2Sided`.

**Step height** — `|face[0].floorZ - face[1].floorZ|` across a step edge.
Compared against the world's **step threshold** to decide whether the player is
blocked or steps up (ADR-0006).

**Palette** — Per-generation `vector<PrimitivePropertySet>`, copied at
generation time. Faces store a `uint16_t` index into it, so `WorldData` is
self-contained and never reaches back into live, animating primitives
(ADR-0005).

**Fixed-point vertex** — An exact point on the world geometry grid. The
canonical coordinate type for topology, contours, and arrangement output.

**Hydraulic cell** — One generated Arrangement triangle together with its
affine floor and ceiling functions and derived Liquid state. It is the unit
whose integrated capacity determines how much of a horizontal Pool it can hold
and whose wet portion is clipped to produce visible Liquid geometry. It is
derived from, and never adds points to, exact Arrangement topology.

**Hydraulic link** — A traversable shared edge between two Hydraulic cells,
including an artificial triangulation edge within one Arrangement face, or an
explicitly open edge from a cell to the exterior drain. A link exists only over
positive-clearance portions of the edge and becomes reachable at its Sill: the
lowest maximum-adjacent-floor elevation over those portions.

**Snap-rounding** — Forcing computed intersection points onto the integer grid,
so that all output topology is exactly representable and vertex identity is an
integer comparison rather than a float comparison (ADR-0003).

**Arris** — A dihedral line where exactly two generated world surfaces meet. A
Horizontal Arris joins an ArrangementWall to a floor or ceiling; a Vertical
Arris joins two ArrangementWalls. It is implied by generated geometry rather
than represented by an Arrangement edge of its own.

**Chip** — Subtractive, visual-only detail cut into an eligible Arris or
trihedral Corner after the boolean fold. It is derived into the World
snapshot's detail channel and never changes collision or spatial queries
(ADR-0027).

**Wedge** — Additive faceted detail controlled by World settings and surfaced
with the adjoining floor or ceiling Sub-material. Independent floor and ceiling
averages per unit distance control Edge Wedge frequency; a probability controls
each Corner Wedge candidate. Quality recursively tessellates each exposed
triangle around a deterministically displaced centroid; zero means no
subdivision. An Edge Wedge is centred on
one visible Border wall's horizontal Arris and has a convex, three-segment
centre line. A Corner Wedge joins independently sized points on the two
horizontal Arrises and shared vertical Arris of two connected visible Border
walls. Both share the post-fold detail channel. Floor Wedges raise floor
collision to their exposed facets; ceiling Wedges remain render-only
(ADR-0031).

## Geometry — before the rewrite (removed)

**ClippedPolygon** — A single contour plus an `isHole` flag. Holes are
associated with their parent *by list ordering* — "N polygons, and after each
polygon is M holes" — a convention invisible in the type and relied on by
`Triangulator::processPolygon` and `WorldData::pointInPolygon`. Replaced by
faces owning their inner boundaries explicitly.

**Intermediate clipping** — A run of primitives between two `Union`s, folded
into one path set by repeated `Clipper64::Execute` calls. The source of the
pipeline's quadratic behaviour. Gone.

**Arrangement clipping / template** — The XOR of all intermediate clippings,
used to cut the runs into per-primitive pieces via a further Difference and
Intersection per run. An expensive reconstruction of what the arrangement
gives directly. Gone.

**WorldVertexData** — Per-vertex pair of property sets (`properties[0]` =
"previous side", `properties[1]` = "next side"), propagated around contours by
`ClipperUtils::interpolatePathVertices` and guessed at where propagation failed.
An attempt to carry face-level truth on vertices. Removed (ADR-0004).

**Z-bitfield** — The `int64_t` z coordinate of each Clipper point, packed with
primitive index (14 bits), primitive vertex index (10), global vertex index
(20), operation (2), and three interpolation state bits. See `Defines.h`.
Removed with `WorldVertexData`.

**ZCallback** — Clipper2 hook allocating a new `WorldVertexData` per computed
intersection, on every pass. Removed.

**PolygonGraph** — Vertex/edge graph built from the border polygons, with up to
two primitive indices per edge and an `is2Sided()` test. Superseded by the
arrangement's native edge–face incidence.

## Gameplay

**Player feet elevation** — The simulated elevation of the player's feet. It
matches the authoritative floor sample while grounded, but remains distinct
while the player steps, falls, floats, or swims. Do not call it `floorZ`, which
confuses player state with the generated surface beneath it.

## Rendering

**Render scale** — The fraction of screen resolution at which the 3D world is drawn. `full`, `half`, and `quarter` select the available scales; the resulting world image is composited across the screen while the interface remains at native resolution.

**Water reflection technique** — The mutually exclusive method used to produce reflected radiance for Liquid interfaces: Screen-space or Planar. It changes the reflection source without changing Liquid reflectance, Liquid F0, absorption, or interface compositing.

**Planar reflection resolution** — The per-dimension fraction of the active 3D world target used by Planar water-reflection images: Full, Half, or Quarter. It is distinct from Render scale, which controls the 3D world itself.

## Pipeline

**Generation** — One full rebuild of world geometry from Primitives. In
Asynchronous mode, requests run on one worker and periodic starts use the
configured start interval; a newer queued request replaces an older one. In
Synchronous mode, one blocking Generation runs during every normal game
update. Switching modes never changes whether a completed result may Commit.

**Commit** — Publishing a completed generation as the active `WorldData`. Gated
so geometry does not visibly pop: a generation is held back while any primitive
it moved is on screen.

**Layer set** — The Layers a generation draws primitives from, held as a
256-bit mask indexed by stable Layer id. Chosen per generation; a primitive is
included when the Layer owning it is in the mask. Layers filter the fold's
input; they do not group or nest it, and priority ordering runs across the
whole selected set (ADR-0009, ADR-0013).

**Culling** *(removed)* — Broad phase (none / circle / box, via the primitive
acceleration grid) then narrow phase (none / circle / view cone), choosing
which primitives entered a generation by proximity to the player. Invalid under
a non-local fold: dropping a primitive changes the result everywhere, not just
where it sat. See ADR-0007.
