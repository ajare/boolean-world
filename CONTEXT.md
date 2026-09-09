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

**Elevation span**:
A Primitive's authored description of one floor or ceiling: a direction angle, a lower elevation, and an upper elevation. Zero degrees points along the Primitive's local +Y axis and angles increase counter-clockwise. The angle orients a bounding box fitted around every Ring; elevation interpolates from the box's negative-direction edge to its positive-direction edge and stays constant across the perpendicular axis. Editing the Primitive refits the box while retaining the three authored values. A zero-length box uses the lower elevation throughout.
_Avoid_: gradient (the derived rate of elevation change), slope (ambiguous between direction and steepness), OBB settings (the box is derived rather than separately authored)

**Elevation plane**:
The affine height function derived from an Elevation span for generation. A Primitive supplies one independently for its floor and ceiling; equal lower and upper elevations produce a horizontal surface. The plane determines elevation and one constant up-facing normal at every World-plane position while the Arrangement remains purely two-dimensional.
_Avoid_: Elevation span (the authored values from which the plane is derived), slope, height (a sampled scalar, not the function)

**Surface frame**:
The stable orthonormal coordinates of one generated surface, derived from its unperturbed geometric up-vector. U is World X projected into the surface (with a World Z fallback near vertical), V completes the right-handed frame, and both coordinates are anchored at the World origin. Two-dimensional procedural materials, their normal perturbations, and Embossing use this frame so physical scale and orientation remain continuous across Arrangement fragments of one Elevation plane.
_Avoid_: UV coordinates (the frame exists before material scaling), tangent frame (which may mean a per-triangle or normal-mapped basis), face-local coordinates (the origin is World-stable rather than face-local)

**Fixed-point vertex**:
An exact point on the world geometry grid. It is the canonical coordinate type for topology and arrangement output.
_Avoid_: Clipper point, floating-point topology vertex

**Arrangement**:
The planar subdivision induced by all selected primitive contours. Its faces are classified by primitive membership and the priority-ordered fold.
_Avoid_: Clip result

**LayerBuildStep**:
One step in a Layer's ordered, serialized recipe for producing its Primitives. Each step's `execute()` reads the Layer as built so far and may only add new Primitives to it; a Layer's Primitives are always derived by re-running its enabled steps in order, never authored or stored independently. The same recipe order is the major order of the boolean fold, with Primitive priority ordering only the output inside one step. A step's type is fixed once created — changing it means deleting the step and adding a new one, never an in-place type change. An authored Primitive may be re-homed from one step into another of the *same* type, keeping the same Primitive rather than a copy: the destination must accept new Primitives, the source must permit direct editing of its output, and both must be enabled. Moving one changes where it folds, because recipe order outranks Primitive priority. The first step of a Layer is always a PrimitiveField step and its type cannot be changed (it can only be disabled, never deleted). Deliberately not called "LayerGenerationStep" — "Generation" already names the unrelated boolean-fold pipeline that turns selected Layers' Primitives into world geometry (see `docs/glossary.md`).
_Avoid_: LayerGenerationStep, generation step

**TileMap (step)**:
A data-only LayerBuildStep holding a finite square Map from `(0,0)` to `(size,size)`, divided into binary TileMap cells for later steps to query. Its Map size is 64, 128, or 256 World units and its cell size is 2, 4, 8, 16, or 32 World units.
_Avoid_: Tile field, grid step

**TileMap cell**:
One zero-based binary location in a TileMap, increasing rightward and upward from the Map's lower-left corner; `0` is unset and `1` is set. Distinct from a PrefabField Tile, which belongs to an infinite size-specific grid and may hold a Prefab instance.
_Avoid_: Tile, grid square

**PrimitiveField (step)**:
The basic LayerBuildStep: an embedded, literal list of Primitive definitions that it adds verbatim. Unrelated to the existing Voronoi/Lloyd-relaxed `PrimitiveFieldLayout`/"Generate Primitive Field…" placement feature, which the name coincidentally echoes.
_Avoid_: conflating with the Voronoi Primitive Field placement feature

**Prefab**:
A named, stably-identified collection of Primitives authored as a unit so that a PrefabField can place and reference it. Holds Primitives only — never WorldTriggerLines, unlike the removed `addPrefabInstance` copy-paste grouping that once bore this name (see Prefab instance, its unrelated successor). Each Prefab chooses its own standard tile size from 32×32, 64×64, 128×128, and 256×256; newly created Prefabs default to 64×64. A Prefab also carries a set of lowercase tags, each made only from ASCII letters, digits, underscores, and hyphens, for case-insensitive all-tag lookup by build scripts. Its DefinePrefabs step supplies the shared PrefabTilingType. A Prefab's Primitives never contribute world geometry directly; they exist to be authored and, later, referenced by Prefab instances. A Prefab's pivot — the point its Primitives orbit when it is rotated — is the origin, not the centre of its contents.
_Avoid_: group, template

**Prefab vertex metadata**:
String key/value annotations on a Prefab's authored MeshPrimitive vertices, used by build scripts as named points carrying extra meaning beyond their position. Metadata belongs to the welded Vertex: every Ring occurrence of that Vertex shares it. Moving a Vertex preserves its metadata, removing it removes the metadata, and splitting an Edge creates an unannotated Vertex. Keys are non-empty and unique per Vertex; values may be empty. Metadata does not affect geometry or the boolean fold.
_Avoid_: Prefab metadata (which is ambiguous with Prefab tags), vertex tag (tags classify whole Prefabs)

**Prefab edge metadata**:
String key/value annotations on a Prefab's authored MeshPrimitive edges, used by build scripts as named spans carrying extra meaning beyond their endpoints. Metadata belongs to the welded Edge: every Ring occurrence of that Edge shares it. Moving an Edge or either endpoint preserves its metadata, removing it removes the metadata, and splitting it copies its metadata to both resulting Edges. Keys are non-empty and unique per Edge; values may be empty. Metadata does not affect geometry or the boolean fold.
_Avoid_: Prefab metadata (which is ambiguous with Prefab tags), edge tag (tags classify whole Prefabs)

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

**RunScript (step)**:
The LayerBuildStep that runs a Lua script to produce its Primitives. It is a placing step, never a defining one: it reads the build-participating Primitives of preceding enabled steps, may look up other steps and their Prefabs by name, and appends new or instanced Primitives, but it never mutates what an earlier step produced and never defines a Prefab. A script addresses a Prefab placement by integer Tile coordinates; the Prefab's own tile size selects the grid, and its rotation is restricted to 0°, 90°, 180°, or 270°, so scripted placement cannot create an off-grid or arbitrarily rotated Prefab instance. Its authored state is the name of its Lua script, a seed, an extra-resources list, Step build variable choices, and its own step name; the script text and Step build variable declarations live outside the World, in the Lua script it names. Named for what it does, after DefinePrefabs, rather than for a spread of content laid over the Layer as PrimitiveField and PrefabField are.
_Avoid_: ScriptField, LuaStep, script primitive

**ScriptRuntime**:
The single object that holds a Lua state, compiles scripts given as text, caches the compiled chunks by name, and executes them. One exists per host, owned by the editor or the game and handed to each RunScript step when that step type is registered. It resolves nothing itself: script text and Step build variable declarations are handed to it as values, so it never consults the resource system. It runs a chunk either synchronously to completion or as a coroutine advanced over successive frames, and each synchronous execution gets a fresh environment, so nothing a script leaves behind survives into the next rebuild.
_Avoid_: script manager, script system, Lua VM (which is the state it owns, not the class)

**Lua script**:
A World dependent resource holding the text of one Lua program and optionally declaring its Step build variables, authored outside the editor and loaded by name like any other Resource. A RunScript step names one. Because its text and declarations are a resource rather than World content, editing them is not part of a World's undo history, and reloading it recompiles the cached chunk and rebuilds every Layer whose steps name it.
_Avoid_: script asset, embedded script, script file (the path is an implementation detail of the Resource)

**Build variable**:
A named, typed, immutable input to a RunScript, holding an integer, float, boolean, or string. World build variables form `world.vars`; a Layer's declarations override same-typed World declarations in `layer.vars`; and a RunScript's resource-declared Step build variables override same-typed effective Layer declarations in `step.vars`. Each narrower table inherits unshadowed values from its parent while the parent tables remain directly accessible. Names are case-sensitive Lua identifiers excluding keywords, override types must match, and deterministic `pairs()` iteration is lexical by name. A Step declaration fixes its string choices or numeric range and provides the default; the RunScript retains its chosen value, which takes precedence over that resource default.
_Avoid_: global variable (the values are scoped), Script parameter, script constant, resource option

**Lua include**:
A Lua script's access to a helper Lua script that the root script declares through its resource dependencies. The helper returns one table, shared by repeated includes only within the current execution; a rebuild executes it afresh. It is named by canonical qualified resource name, never by file path, and runs inside the same Restricted environment as its root.
_Avoid_: require (Lua's unrestricted process-wide module system), header (an include returns an explicit table rather than inserting declarations), module file

**Restricted environment**:
The set of Lua values a build script may see: base functions less those that load code or drive the collector, plus table, string, math, a logged print, its immutable `world.vars`, `layer.vars`, and `step.vars` tables, and resource-backed Lua include. It excludes everything that could make a build depend on something outside the recipe — the filesystem, the clock, and Lua's standard module loader — so that re-running a Layer's steps always reproduces the same Primitives. Randomness is permitted but is seeded from the RunScript step's serialized seed at the start of every execution, making a scatter reproducible and a reroll an authored change.
_Avoid_: sandbox (which suggests a security boundary; this is a determinism boundary), script globals

**Step name**:
An optional, non-unique label on a LayerBuildStep, existing so that a script can find a step without knowing its id. Unlike the step's id, which is stable for the owning Layer's lifetime, a name is authored and may be changed or duplicated; a script naming a step or Prefab that has since been renamed fails at its next execution, and repairing it is the script author's work.
_Avoid_: step id (the stable handle), step label, step title

**Wall collision override**:
A per-edge tri-state on a MeshPrimitive's Ring: Unset, Collides, or Doesn't collide. It is editable in the editor's Edge sub-mode only for an edge used by exactly one polygon in that Primitive's own mesh topology (Willpower's `External` edge connectivity). Unset delegates to generated collision: Border walls collide and Step walls do not unless their floor step is too tall or their clearance is insufficient. Collides additionally blocks a wall; Doesn't collide can open a Border but cannot bypass a Step wall's physical height or clearance constraints. When collinear authored edges contribute to one generated edge, Doesn't collide dominates Collides and Unset contributes nothing. An override never creates a wall where the fold produces none.
_Avoid_: wall property, collision flag (ambiguous with the runtime collision system), border flag (border here is local mesh-topology "one polygon", not the Arrangement's cross-primitive Border edge — the two usually but not always coincide)

**Wall visibility override**:
The same per-edge, External-only mechanism as Wall collision override, for a second independent flag: whether that edge's ArrangementWall renders. Also defaults on. Unlike collision, resolving it needs no world-level parameter (there is no threshold to fall back to), so it is settled directly when ArrangementWalls are built rather than deferred to ArrangementWorldData; like collision, it never creates a wall where the fold produces none — it can only hide a wall that already exists.
_Avoid_: render flag, hidden flag

**Wall normal-map override**:
A per-External-edge choice that is Unset, Disabled, or an ImageResource normal map, inherited by the uncut ArrangementWall surface that edge contributes to; Chip facets expose new surfaces and do not inherit it. Higher-precedence explicit choices dominate lower contributors; it varies wall-surface detail independently of the wall's Sub-material and never changes geometry or collision.
_Avoid_: Sub-material normal map, Primitive normal map, wall texture, image filepath

**World dependent resource**:
A named Willpower Resource referenced by authored World content and required before that World can be deserialized and activated. A World's serialized list is the exact, sorted projection of all such references, including references in disabled LayerBuildSteps and Prefab definitions.
_Avoid_: asset path, normal-map dependency

**Player proxy**:
A position and facing angle stored on the Document, representing where the in-game player currently would be. Independent of any Primitive or Layer; used to render the editor's player-view overlay and to seed a flythrough's starting pose.
_Avoid_: player start, spawn point

**Player feet elevation**:
The simulated elevation of the player's feet. It equals the sampled floor elevation while grounded, but differs while the player steps, falls, floats, or swims.
_Avoid_: player floor, floorZ (which confuses player state with the generated surface beneath it)

**Player Torch**:
The permanently equipped point light carried by the player. It supplies the World's direct illumination and casts shadows in every direction; in player-view previews, the Player proxy stands in for the player carrying it.
_Avoid_: light source (too broad), shadow light (an implementation input rather than the game concept)

**Water mantle**:
The deliberate player action that leaves liquid by hauling the player onto an adjacent higher floor. Its reach is measured vertically from the player's eye to that floor and is a player capability shared by every World, distinct from the player's smaller ordinary step height.
_Avoid_: climb-out (use mantle for the action), water step (ordinary stepping is a different capability)

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
A named, fully-parameterized instance of one Technique, defined inside a ProcMaterial resource: fixed parameter values, a fixed base colour, Chip settings, and a stable string id unique across every ProcMaterial resource. A wall, floor, or ceiling is assigned a Sub-material by that id alone — never a Technique directly, and never with per-instance parameter overrides.
_Avoid_: material, material definition (the retired per-Primitive params+colour struct), procedural material

**Emboss preset**:
A named, globally reusable Embossing definition in the one Embossing catalog. A Primitive's floor, ceiling, and wall independently reference a preset by stable id, or none, separately from their Sub-material ids.
_Avoid_: Sub-material emboss, material emboss, embossing catalog entry

**Embossing**:
The tiling relief an Emboss preset lays over one assigned surface — a square, hexagon, running-bond, modular-opus or Voronoi pattern, plus the tile size, groove depth and per-tile depth variation that shape it. Evaluated in the plane of the surface itself, so a wall tiles across and up its own face rather than through a ground-plane projection, and applied as a normal-map perturbation only: it never changes geometry or collision. It is shared independently of ProcMaterial catalogs and bounded by its own limits rather than by a Technique schema. Was once a global floor-only render option.
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

**Liquid level**:
An authored, per-Primitive scalar defaulting to zero, meaningful only on a Primitive whose operation is Union — other operations store it but it has no effect. A Primitive's total liquid volume is this value times its own raw area, as though the Primitive existed alone. That volume is conserved through the fold: however the fold subdivides the Primitive's footprint across faces, or carves part of it away, whatever area survives holds the whole volume at one uniform seeded depth — so on an uncarved Primitive the authored value is literally the depth of liquid standing on it before any flow.
_Avoid_: liquid depth (the derived per-face quantity), fill level

**Liquid depth**:
The finished depth of standing liquid in one Arrangement face, derived after the fold from the equilibrium of the Pool that face belongs to and clamped between zero and that face's own floor-to-ceiling clearance. Zero means dry.
_Avoid_: liquid level (the authored per-Primitive quantity), liquid height, liquid elevation

**Liquid type**:
An authored, per-Primitive enum (`core::LiquidType`) alongside Liquid level, meaningful under the same Union-only condition. Selects the reserved render material a face wet from that Primitive uses (see `LiquidMaterialIndex`). A single value, Water, exists today; a face's own type is whichever Primitive's properties won that face, the same source its floor/ceiling materials come from.
_Avoid_: liquid material (the render-side index it selects, not the authored choice itself)

**Liquid reflectance**:
A per-Liquid-type scalar from zero to one that grades the Liquid surface's entire angle-dependent reflected contribution. Zero suppresses reflection; one leaves the response implied by its Liquid F0 unchanged. It does not replace or alter Liquid F0.
_Avoid_: reflectivity (use the authored property name), reflection strength (does not identify its per-Liquid-type ownership)

**Liquid F0**:
A per-Liquid-type scalar from zero to one giving the Liquid surface's Fresnel reflectance at normal incidence. It supplies the base value of the angle-of-incidence response; Liquid reflectance separately grades the resulting response at every angle.
_Avoid_: Fresnel coefficient (ambiguous between F0 and the angle-dependent result), refractive index (a different optical property from which physical F0 can be derived)

**Water reflection technique**:
The mutually exclusive method used to produce reflected radiance for Liquid interfaces: Screen-space or Planar. It changes the reflection source without changing Liquid reflectance, Liquid F0, absorption, or interface compositing.
_Avoid_: reflection mode, SSR mode

**Planar reflection resolution**:
The per-dimension fraction of the active 3D world target used by Planar water-reflection images: Full, Half, or Quarter.
_Avoid_: reflection render scale (Render scale already names the resolution of the 3D world)

**Liquid-adjacency**:
The relation between two solid Arrangement faces across whose shared edge Hydraulic cells may link: both faces must be solid and some positive-clearance part of the shared opening must exist. The Arrangement's outer, unbounded face is liquid-adjacent to a bordering solid face only where the Border wall is explicitly authored not to collide — a solid wall there blocks liquid exactly as it blocks the player, so an ordinary outer wall is not an opening just because nothing is authored beyond it. Where reached, an open exterior link acts as a permanent drain.
_Avoid_: face adjacency (two faces sharing an edge are not liquid-adjacent when no traversable opening exists)

**Hydraulic cell**:
One generated Arrangement triangle together with its affine floor and ceiling functions and derived Liquid state. It is the unit whose integrated capacity determines how much of a horizontal Pool it can hold and whose wet portion is clipped to produce visible Liquid geometry; its World-plane triangle remains ordinary derived triangulation, never new Arrangement topology.
_Avoid_: Liquid triangle (the cell also exists while dry), face (one Arrangement face may contain several Hydraulic cells)

**Hydraulic link**:
A traversable shared edge between two Hydraulic cells, including an artificial triangulation edge inside one Arrangement face, or an explicitly open edge from one cell to the exterior drain. It exists only where some part of the edge has positive vertical clearance and is crossed only when Liquid reaches its Sill.
_Avoid_: face adjacency (links join cells), opening (the traversable span from which the link is derived)

**Wet component**:
A maximal set of Hydraulic cells connected by Hydraulic links. Its liquid settles as one or more Pools, not necessarily at one shared elevation: a cell unreachable from any seed Liquid stays dry regardless of its own floor height, and a cell standing above every reached Sill stays dry while its neighbours hold Liquid.
_Avoid_: lake, basin, pond, Pool (a Wet component may hold several)

**Pool**:
One set of Hydraulic cells within a Wet component holding Liquid at a single shared surface elevation. Two Pools become one the moment their combined equilibrium would stand at or above the Sill between them; below it they stay two, and the higher one spills only what stands above the Sill into the lower, ending exactly brim-full at the Sill rather than emptying into it.
_Avoid_: Wet component (the connectivity, not the body of Liquid), lake, pond

**Sill**:
The elevation Liquid must reach to cross one Hydraulic link: the minimum, over every positive-clearance part of its shared edge, of the maximum adjacent floor elevation. For an exterior-drain link, only the bordering cell's floor and ceiling bound the opening. Affine floor and ceiling crossings along the edge can delimit the traversable part and therefore the Sill.
_Avoid_: saddle, spill point, threshold, wall clearance (which decides where an opening exists, not the elevation at its bottom)

**AudioEmitter**:
A point sound source owned by exactly one Primitive, positioned by a two-dimensional offset from its Primitive's position together with a height offset above the floor of the face it falls in. It is authored on its Primitive and transforms with it, but its world position is settled once, when a World's geometry is generated, and never changes afterwards. A sound that has to move through a World is an entity, not an AudioEmitter.
_Avoid_: sound source (which also covers moving entity sources), audio event (the authored sound an emitter names, not the place it sounds from), speaker

**Emitter capture**:
The generation-time resolution of every AudioEmitter into a single world position, and the decision of whether it survives at all. An emitter survives only where the Arrangement has a solid face, its parent Primitive still contributes to the solid there, and its derived emitter height stands below that face's ceiling. An emitter that does not survive takes no part in the generated World. Existence is decided by the parent Primitive; elevation is not.
_Avoid_: emitter placement (the authoring act, not the generation-time resolution), emitter culling (which names only the discarding half)

**Derived emitter height**:
The world height a captured AudioEmitter sounds from: its authored height offset added to the floor of the face it falls in, taken after floor Wedges have raised that floor — the same surface the player stands on. That floor comes from whichever Primitive won the face's properties, never from the emitter's own parent Primitive, so one authored offset resolves to different heights on different faces.
_Avoid_: emitter height offset (the authored value, not the resolved height), parent floor (the parent Primitive's own floorZ, which does not determine it)

**Acoustic preset**:
A named description of how a surface absorbs, scatters, and transmits sound, referenced by stable id from a Sub-material so that one authored surface carries the same acoustic character everywhere it appears. A missing or empty id is a valid, if unresolved, state; resolution happens outside the World, exactly as it does for Sub-material and Embossing references. It describes what the world does to a sound, never the sound itself: an AudioEmitter names what is heard, an Acoustic preset only what the surfaces do to it.
_Avoid_: acoustic material (ambiguous with both Sub-material and the render Material), surface absorption (one of its several properties), reverb setting (a property of the space, not of a surface)
