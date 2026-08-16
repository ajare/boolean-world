# Glossary — World geometry

Terms used by the World clip pipeline and its replacement. Where a term means
something different before and after the arrangement rewrite, both senses are
given.

## Authoring

**Primitive** — One authored 2D closed shape (`Rectangle`, `Regular`, `Circle`,
`Torus`, `Superformula`, `Mesh`). Carries geometry, a boolean **operation**, a
**fill rule**, a **priority**, a **layer**, and a **property set**. Tessellated
to a `vector<ComplexPolygon>` — up to
`BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX` (1024) vertices.

**Contour** — A closed sequence of **fixed-point vertices** forming one
boundary of a primitive. One primitive may contribute several contours,
resolved against each other by the primitive's own **fill rule**.

**ComplexPolygon** — `vector<ClosedPolygon>`; the authoring-side polygon and
hole structure converted into contours before arrangement construction.

**Operation** — `Union`, `Intersection`, `Difference`, `XOR`. Applied to the
*accumulated result so far*, not to a named target. See **fold**.

**Fill rule** — `NonZero` or `EvenOdd`. Reduces a primitive's signed winding
number at a point to inside/outside. Per-primitive, applied *before* the fold.

**Priority** — Authored `uint8_t`. Sets the order primitives are folded in.
Not a z-order: it determines meaning, not just overlap.

**Fold** — The evaluation model. Primitives are sorted by priority and combined
left-to-right: `((P0 op P1) op P2) op P3 …`, where each `opN` is `PN`'s own
operation. Order-dependent and non-local: inserting a primitive changes the
meaning of every primitive above it. Preserved exactly by the rewrite
(ADR-0001).

**Property set** (`PrimitivePropertySet`) — `floorZ`, `ceilingZ`, and floor /
ceiling / wall material indices and definitions. The renderable attributes of
a region.

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

**Snap-rounding** — Forcing computed intersection points onto the integer grid,
so that all output topology is exactly representable and vertex identity is an
integer comparison rather than a float comparison (ADR-0003).

## Geometry — before the rewrite (being removed)

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

## Pipeline

**Generation** — One full rebuild of world geometry from primitives. Runs on a
worker thread, not every frame; cadence of at least one second.

**Commit** — Publishing a completed generation as the active `WorldData`. Gated
so geometry does not visibly pop: a generation is held back while any primitive
it moved is on screen.

**Layer set** — The layers a generation draws primitives from, held as a
256-bit mask. Chosen per generation; a primitive is included when its layer is
in the mask or when its layer is `BW_LAYER_ALL`. Layers filter the fold's input;
they do not group or nest it, and priority ordering runs across the whole
selected set (ADR-0009).

**Culling** *(removed)* — Broad phase (none / circle / box, via the primitive
acceleration grid) then narrow phase (none / circle / view cone), choosing
which primitives entered a generation by proximity to the player. Invalid under
a non-local fold: dropping a primitive changes the result everywhere, not just
where it sat. See ADR-0007.
