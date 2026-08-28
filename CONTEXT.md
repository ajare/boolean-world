# Boolean World Geometry

Boolean World Geometry turns authored two-dimensional shapes into the regions and surfaces of a three-dimensional world.

## Language

**Primitive**:
An authored closed shape that contributes an operation, fill rule, step-local priority, and regional properties to a world. Owned by exactly one Layer. Its priority orders it among Primitives produced by the same LayerBuildStep; Layer and LayerBuildStep order take precedence.
_Avoid_: Path primitive, clip shape

**Property-transparent Primitive**:
A structural Primitive that participates fully in the boolean fold but can never supply properties to a generated surface. PrefabField Replace squares are property-transparent: they erase earlier geometry so replacement content can be folded in, but any exposed wall retains the surviving solid region's properties. Authored Primitives are property-contributing by default.
_Avoid_: material-less Primitive (an authored Primitive with a missing material is still property-contributing), invisible Primitive (property transparency does not affect geometry or visibility)

**Layer**:
A named, owned collection of Primitives and WorldTriggerLines within a World. A generation selects a set of Layers and folds them in World order; the World's active Layer is the one currently focused for authoring. Ownership is permanent: neither a Primitive nor a WorldTriggerLine ever moves between Layers.
_Avoid_: Layer tag, layer id (as a primitive attribute)

**Contour**:
A closed sequence of fixed-point vertices forming one boundary of a primitive. A primitive may contribute multiple contours whose combined interior is determined by its fill rule. The generation-side form of a Ring.
_Avoid_: Clipper path, Clipper polygon

**Ring**:
A closed loop of authored vertices forming one boundary of a Primitive's shape, in that Primitive's own local space. Becomes a Contour when the World's geometry is generated. The editor's Polygon sub-mode — so labelled because Vertex/Edge/Polygon is the familiar sub-object triad — selects Rings.
_Avoid_: Contour (which is the fixed-point, generation-side form), loop, path, outline (the editor's Outline panel is unrelated)

**Filled region**:
A Ring together with the Holes it directly contains. A filled region is a Shell at the root of a MeshPrimitive's containment hierarchy and an Island when directly contained by a Hole.
_Avoid_: positive polygon, solid Ring

**Shell**:
A filled region at the root of a MeshPrimitive's containment hierarchy.
_Avoid_: outer polygon, exterior Ring

**Hole**:
An empty Ring directly contained by a Shell or Island. A Hole may contain Islands.
_Avoid_: negative polygon, Difference Ring

**Island**:
A filled Ring directly contained by a Hole. An Island may itself contain Holes, acting as their containing filled region without becoming a root Shell.
_Avoid_: inner shell, nested outer polygon

**World plane**:
The canonical authored coordinate system: +X is right and +Y is up. The editor, game view, controls, and map all preserve this orientation; elevation is a separate axis.
_Avoid_: Treating the game view as a mirrored coordinate system

**Fixed-point vertex**:
An exact point on the world geometry grid. It is the canonical coordinate type for topology and arrangement output.
_Avoid_: Clipper point, floating-point topology vertex

**Arrangement**:
The planar subdivision induced by all selected primitive contours. Its faces are classified by primitive membership and the priority-ordered fold.
_Avoid_: Clip result

**LayerBuildStep**:
One step in a Layer's ordered, serialized recipe for producing its Primitives. Each step's `execute()` reads the Layer as built so far and may only add new Primitives to it; a Layer's Primitives are always derived by re-running its enabled steps in order, never authored or stored independently. The same recipe order is the major order of the boolean fold, with Primitive priority ordering only the output inside one step. A step's type is fixed once created — changing it means deleting the step and adding a new one, never an in-place type change. The first step of a Layer is always a PrimitiveField step and its type cannot be changed (it can only be disabled, never deleted). Deliberately not called "LayerGenerationStep" — "Generation" already names the unrelated boolean-fold pipeline that turns selected Layers' Primitives into world geometry (see `docs/glossary.md`).
_Avoid_: LayerGenerationStep, generation step

**PrimitiveField (step)**:
The basic LayerBuildStep: an embedded, literal list of Primitive definitions that it adds verbatim. Unrelated to the existing Voronoi/Lloyd-relaxed `PrimitiveFieldLayout`/"Generate Primitive Field…" placement feature, which the name coincidentally echoes.
_Avoid_: conflating with the Voronoi Primitive Field placement feature

**Prefab**:
A named, stably-identified collection of Primitives authored as a unit so that a PrefabField can place and reference it. Holds Primitives only — never WorldTriggerLines, unlike the removed `addPrefabInstance` copy-paste grouping that once bore this name (see Prefab instance, its unrelated successor). Each Prefab chooses its own standard tile size from 32×32, 64×64, 128×128, and 256×256; newly created Prefabs default to 64×64. Its DefinePrefabs step supplies the shared PrefabTilingType. A Prefab's Primitives never contribute world geometry directly; they exist to be authored and, later, referenced by Prefab instances. A Prefab's pivot — the point its Primitives orbit when it is rotated — is the origin, not the centre of its contents.
_Avoid_: group, template

**DefinePrefabs (step)**:
The LayerBuildStep that owns a set of Prefabs and supplies their shared PrefabTilingType, but not their individual tile sizes. It defines rather than places: outside an authoring session it contributes nothing to its Layer at all. Which Prefab is being edited is ephemeral editor focus — never serialized, and unselected after construction, copy, or load — mirroring a Layer's active step and a World's active Layer.
_Avoid_: PlacePrefabs (the rejected name for PrefabField)

**Tiling guide**:
The single wireframe polygon a DefinePrefabs step draws for its selected Prefab, centred on the origin using the step's shared PrefabTilingType and that Prefab's tile size. It is the frame a Prefab is authored against — the visible form of the Prefab's pivot — and is unrelated to the editor's snapping grid. No selected Prefab means no tiling guide.
_Avoid_: grid, prefab grid

**Tile**:
One cell of one of the infinite, size-specific grids a PrefabField lays out over the whole World, identified by its grid size plus integer coordinates; coordinates alone are ambiguous across grids. Its size must match its Prefab's tile size. All size-specific grids share the origin as a grid intersection, so Tile `(x,y)` is the half-open region `[x·size,(x+1)·size) × [y·size,(y+1)·size)` and the four standard grids nest exactly. A Tile holds at most one Prefab instance of its size; most Tiles hold none. Distinct from the tiling guide, which is one polygon a DefinePrefabs step draws around a Prefab's origin — the Tile grids are PrefabField's, not DefinePrefabs'.
_Avoid_: cell, grid square

**PrefabField (step)**:
The LayerBuildStep that places Prefab instances across four nested Tile grids, one for each standard Prefab tile size, referencing exactly one DefinePrefabs step on the same Layer for its shared tiling type and available Prefabs. It applies seven phases from largest to smallest — 256 content, then a Difference-square and content phase for each of 128, 64, and 32 — so smaller-grid Prefab instances overrule larger-grid output within that field. Those phases belong only to that PrefabField's place in its Layer recipe: earlier steps feed it and later steps fold after it, allowing multiple ordered PrefabFields. Within a content phase, a Prefab's authored Primitive priorities retain their relative order. Unlike DefinePrefabs, its Primitives always contribute to the main boolean fold, the same as any ordinary step's. Deleting a DefinePrefabs step or a Prefab that some PrefabField still references is refused, never silently unbound.
_Avoid_: PlacePrefabs (rejected in favour of the PrimitiveField-echoing name)

**Tile mode**:
How an occupied Tile on the 32×32, 64×64, or 128×128 grid composes into the global fold: Add or Replace. Add applies the Prefab's Primitives normally. Replace first contributes an exact Tile-sized Difference square, clearing all geometry accumulated below it, then applies the Prefab's Primitives. The mode defaults to Replace and exists only while the Tile is occupied. The 256×256 grid has no Tile mode; its occupants are always Add.
_Avoid_: blend mode, cell operation

**Wall collision override**:
A per-edge tri-state on a MeshPrimitive's Ring: Unset, Collides, or Doesn't collide. It is editable in the editor's Edge sub-mode only for an edge used by exactly one polygon in that Primitive's own mesh topology (Willpower's `External` edge connectivity). Unset delegates to generated collision: Border walls collide and Step walls do not unless their floor step is too tall or their clearance is insufficient. Collides additionally blocks a wall; Doesn't collide can open a Border but cannot bypass a Step wall's physical height or clearance constraints. When collinear authored edges contribute to one generated edge, Doesn't collide dominates Collides and Unset contributes nothing. An override never creates a wall where the fold produces none.
_Avoid_: wall property, collision flag (ambiguous with the runtime collision system), border flag (border here is local mesh-topology "one polygon", not the Arrangement's cross-primitive Border edge — the two usually but not always coincide)

**Wall visibility override**:
The same per-edge, External-only mechanism as Wall collision override, for a second independent flag: whether that edge's ArrangementWall renders. Also defaults on. Unlike collision, resolving it needs no world-level parameter (there is no threshold to fall back to), so it is settled directly when ArrangementWalls are built rather than deferred to ArrangementWorldData; like collision, it never creates a wall where the fold produces none — it can only hide a wall that already exists.
_Avoid_: render flag, hidden flag

**Wall normal-map override**:
A per-External-edge choice that is Unset, Disabled, or an image normal map, inherited by the uncut ArrangementWall surface that edge contributes to; Chip facets expose new surfaces and do not inherit it. Higher-precedence explicit choices dominate lower contributors; it varies wall-surface detail independently of the wall's Sub-material and never changes geometry or collision.
_Avoid_: Sub-material normal map, Primitive normal map, wall texture

**Player proxy**:
A position and facing angle stored on the Document, representing where the in-game player currently would be. Independent of any Primitive or Layer; used to render the editor's player-view overlay and to seed a flythrough's starting pose.
_Avoid_: player start, spawn point

**Player Torch**:
The permanently equipped point light carried by the player. It supplies the World's direct illumination and casts shadows in every direction; in player-view previews, the Player proxy stands in for the player carrying it.
_Avoid_: light source (too broad), shadow light (an implementation input rather than the game concept)

**Prefab instance**:
One Tile's occupant: a reference to a same-sized Prefab plus a rotation and, where applicable, a Tile mode; it is not a copy — editing the Prefab's Primitives changes every instance of it. All Replace squares on one grid are applied before any Prefab instances on that grid. Rotation is one of the referenced DefinePrefabs step's PrefabTilingType's allowed angles (four for Square: 0/90/180/270), not an arbitrary orientation. Reuses the name of the removed Primitive+TriggerLine clipboard grouping (`addPrefabInstance`), now fully gone from the codebase — the two are unrelated, and this is the concept the name refers to going forward.
_Avoid_: prefab copy, instance (ambiguous alone — this codebase also has C++ class instances, animation-transform instances, etc.)

**ProcMaterial**:
A Resource holding a curated catalog of Sub-materials, one Technique schema per Technique it uses, and the pair of 3D and 2D program references those Sub-materials render through. Replaces the compiled material registry and Game.yaml's `Materials:` override section as the sole source of procedural material data. Purely data — it names, bounds, and dispatches, but never loads a program itself.
_Avoid_: material registry, material resource (ambiguous with willpower's own `MaterialResource`, which ProcMaterial deliberately does not subclass — see ADR-0023)

**Technique**:
One of the fixed procedural shader algorithms (Marble, Stone, Slate, …) a Sub-material selects by `material_index`. Its dispatch is a hand-maintained switch inside the 3D and 2D fragment shaders and cannot become data-driven without a shader-codegen effort; only a Technique's name, parameter bounds, and defaults move into ProcMaterial data. A Technique is never authored or assigned directly — only through a Sub-material.
_Avoid_: material (too broad — see Sub-material), shader, material index (the field name, not the concept)

**Sub-material**:
A named, fully-parameterized instance of one Technique, defined inside a ProcMaterial resource: fixed parameter values, a fixed base colour, an Embossing block, and a stable string id unique across every ProcMaterial resource. A wall, floor, or ceiling is assigned a Sub-material by that id alone — never a Technique directly, and never with per-instance parameter overrides.
_Avoid_: material, material definition (the retired per-Primitive params+colour struct), procedural material

**Embossing**:
The tiling relief a Sub-material lays over whatever surface it is applied to — a square, hexagon, running-bond, modular-opus or Voronoi pattern, plus the tile size, groove depth and per-tile depth variation that shape it. Evaluated in the plane of the surface itself, so a wall tiles across and up its own face rather than through a ground-plane projection, and applied as a normal-map perturbation only: it never changes geometry or collision. Authored per Sub-material and bounded by its own limits rather than by a Technique schema, because the same shader code evaluates it whatever the Technique. Was once a global floor-only render option.
_Avoid_: floor pattern (it is not floor-only and not a render option), bump map, displacement (nothing is displaced)

**Technique schema**:
The parameter names, count, and min/max/default bounds for one Technique, authored once inside a ProcMaterial resource and shared by every Sub-material that selects that Technique. Bounds a Sub-material's authored values; never itself assigned to a wall, floor, or ceiling.
_Avoid_: material params, param definition

**Arris**:
A dihedral edge where exactly two generated world surfaces meet. A Horizontal Arris joins an ArrangementWall to a floor or ceiling along the wall's top or bottom rim. A Vertical Arris joins two ArrangementWalls along their shared upright boundary. An Arris is never a point where three or more surfaces meet. A Vertical Arris is eligible according to the angle on the canonical front side shared by its walls. Border wall normals face toward their polygon. The wall rays and canonical normals distinguish the front-side reflex angle from the smaller unsigned angle; the front-side angle must be between 225° and 315°, inclusive. Purely implied by the generated geometry: no arrangement vertex, edge, or face records it, and nothing is authored against it. Distinct from both senses of "edge" already in use: an Arrangement's 2D edge between two faces, and a MeshPrimitive Ring's authored edge that carries wall collision and visibility overrides.
_Avoid_: edge (ambiguous — taken twice already), corner (implies a point rather than a dihedral edge), seam, rim

**Corner**:
A trihedral vertex where exactly three generated world surfaces meet: two Step walls and the horizontal surface at a FloorStep's top or CeilingStep's bottom. The two walls' shared canonical-front-side angle must be between 225° and 315°, inclusive. A Corner is a point, unlike an Arris, and is purely implied by the generated geometry rather than authored.
_Avoid_: vertex (an Arrangement vertex is only a 2D topology point and need not be a Corner), polygon corner, trihedral Arris

**Chip**:
A material cut into an eligible Arris or Corner. An Arris Chip is a tapered wedge removed deepest near its centre and tapering to nothing at both ends. A Corner Chip truncates its Corner with one independently distanced point on each of the three incident edges and a triangular cut face joining them. Derived after the boolean fold from the Arrangement alone — never authored, and never written to a World file, which holds no record of any Chip. Purely visual: collision, floor height, face containment and surface picking all continue to see the unchipped world. The governing Sub-material controls the relevant probabilities and size ranges, supplies each new facet's material, and lists the Arris Chip types suitable for that material; every Arris Chip independently selects one listed type. The available types are Tapered, Prismatic Notch, Pyramidal Divot, Multi-facet Spall, Stepped Fracture, V-shaped Notch, and Trapezoidal Spall. A Horizontal Arris and a Corner are governed by their Step wall; a Vertical Arris is eligible only when its two wall faces meet along their shared upright edge, their shared canonical-front-side angle is between 225° and 315° inclusive, and they share the same Sub-material, which then governs both. A Vertical Chip's deepest point lies on the material-side bisector opposite the sum of the two wall normals, keeping the gouge centred on the shared edge while sending it into the corner material. Reach is an Arris Chip's total extent along the Arris; width is half its reach and may never exceed 2 world units. Eligible Horizontal Arrises are a FloorStep's convex top and a CeilingStep's convex bottom.
_Avoid_: crack (a fissure removes no material — a separate, deferred idea, closer to Embossing than to geometry), damage, wear, chamfer (names the shape, not the feature)

**Wedge**:
Additive geometric detail governed by the World and surfaced with the adjoining horizontal surface's Sub-material. An Edge Wedge is centred beneath the ceiling Arris or above the floor Arris of one visible Border wall; its two sides meet along a subtly convex, three-segment centre line that arches inward toward the wall. A Corner Wedge fills the trihedral Corner where a floor or ceiling meets two connected visible Border walls, joining one independently sized point on each horizontal Arris to one point on their shared vertical Arris. Both are derived after the boolean fold in the same detail channel as Chips. World-level floor and ceiling averages per unit distance control Edge Wedge frequency; a World-level probability independently controls each Corner Wedge candidate. World-level quality recursively replaces every exposed triangle with three triangles meeting at a deterministically displaced centroid; quality zero retains the base geometry. A floor Wedge raises floor collision to its exposed facets, while a ceiling Wedge remains render-only; neither changes wall collision, face containment, surface picking, the editor's 2D geometry, or authored geometry.
_Avoid_: Chip (subtractive detail), Arrangement geometry, authored Primitive

**Water level**:
An authored, per-Primitive scalar defaulting to zero, meaningful only on a Primitive whose operation is Union — other operations store it but it has no effect. A Primitive's total water volume is this value times its own raw area, as though the Primitive existed alone; a Union Primitive later carved away by other operations loses the corresponding share of that volume.
_Avoid_: water depth (the derived per-face quantity), fill level

**Water depth**:
The finished depth of standing water in one Arrangement face, derived after the fold from Wet component equilibrium and clamped between zero and that face's own floor-to-ceiling clearance. Zero means dry.
_Avoid_: water level (the authored per-Primitive quantity), water height, water elevation

**Water-adjacency**:
The relation between two non-solid Arrangement faces across which water can equilibrate: both faces must be non-solid and their shared edge's wall clearance must be nonzero. The Arrangement's outer, unbounded face is water-adjacent to every face bordering it, with an effective floor of negative infinity, acting as a permanent drain.
_Avoid_: face adjacency (two faces sharing an edge are not water-adjacent when the wall between them has zero clearance)

**Wet component**:
A maximal set of Arrangement faces connected by water-adjacency that share one common equilibrium water surface elevation. Two Wet components merge into one the moment rising water would connect them across a saddle point; a face unreachable from any seed water stays dry regardless of its own floor height.
_Avoid_: lake, basin, pond
