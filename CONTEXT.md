# Boolean World Geometry

Boolean World Geometry turns authored two-dimensional shapes into the regions and surfaces of a three-dimensional world.

## Language

**Primitive**:
An authored closed shape that contributes an operation, fill rule, priority, and regional properties to a world. Owned by exactly one Layer.
_Avoid_: Path primitive, clip shape

**Layer**:
A named, owned collection of Primitives and WorldTriggerLines within a World. A generation selects a set of Layers to fold together; the World's active Layer is the one currently focused for authoring. Ownership is permanent: neither a Primitive nor a WorldTriggerLine ever moves between Layers.
_Avoid_: Layer tag, layer id (as a primitive attribute)

**Contour**:
A closed sequence of fixed-point vertices forming one boundary of a primitive. A primitive may contribute multiple contours whose combined interior is determined by its fill rule. The generation-side form of a Ring.
_Avoid_: Clipper path, Clipper polygon

**Ring**:
A closed loop of authored vertices forming one boundary of a Primitive's shape, in that Primitive's own local space. Becomes a Contour when the World's geometry is generated. The editor's Polygon sub-mode — so labelled because Vertex/Edge/Polygon is the familiar sub-object triad — selects Rings.
_Avoid_: Contour (which is the fixed-point, generation-side form), loop, path, outline (the editor's Outline panel is unrelated)

**Fixed-point vertex**:
An exact point on the world geometry grid. It is the canonical coordinate type for topology and arrangement output.
_Avoid_: Clipper point, floating-point topology vertex

**Arrangement**:
The planar subdivision induced by all selected primitive contours. Its faces are classified by primitive membership and the priority-ordered fold.
_Avoid_: Clip result

**LayerBuildStep**:
One step in a Layer's ordered, serialized recipe for producing its Primitives. Each step's `execute()` reads the Layer as built so far and may only add new Primitives to it; a Layer's Primitives are always derived by re-running its enabled steps in order, never authored or stored independently. A step's type is fixed once created — changing it means deleting the step and adding a new one, never an in-place type change. The first step of a Layer is always a PrimitiveField step and its type cannot be changed (it can only be disabled, never deleted). Deliberately not called "LayerGenerationStep" — "Generation" already names the unrelated boolean-fold pipeline that turns selected Layers' Primitives into world geometry (see `docs/glossary.md`).
_Avoid_: LayerGenerationStep, generation step

**PrimitiveField (step)**:
The basic LayerBuildStep: an embedded, literal list of Primitive definitions that it adds verbatim. Unrelated to the existing Voronoi/Lloyd-relaxed `PrimitiveFieldLayout`/"Generate Primitive Field…" placement feature, which the name coincidentally echoes.
_Avoid_: conflating with the Voronoi Primitive Field placement feature

**Prefab**:
A named, stably-identified collection of Primitives authored as a unit so that a PrefabField can place and reference it. Holds Primitives only — never WorldTriggerLines, unlike the removed `addPrefabInstance` copy-paste grouping that once bore this name (see Prefab instance, its unrelated successor). A Prefab's Primitives never contribute world geometry directly; they exist to be authored and, later, referenced by Prefab instances. A Prefab's pivot — the point its Primitives orbit when it is rotated — is the origin, not the centre of its contents.
_Avoid_: group, template

**DefinePrefabs (step)**:
The LayerBuildStep that owns a set of Prefabs. It defines rather than places: outside an authoring session it contributes nothing to its Layer at all. Which Prefab is being edited is ephemeral editor focus — never serialized, and unselected after construction, copy, or load — mirroring a Layer's active step and a World's active Layer.
_Avoid_: PlacePrefabs (the rejected name for PrefabField)

**Tiling guide**:
The single wireframe polygon a DefinePrefabs step draws, centred on the origin at the step's chosen PrefabTilingType and size. It is the frame a Prefab is authored against — the visible form of the Prefab's pivot — and is unrelated to the editor's snapping grid.
_Avoid_: grid, prefab grid

**Tile**:
One cell of the infinite grid a PrefabField lays out over the whole World, addressed by integer coordinates and sized/shaped by its referenced DefinePrefabs step's PrefabTilingType and size, anchored the same way the tiling guide is (tile (0,0) is that same footprint at the origin). A tile holds at most one Prefab instance; most tiles hold none. Distinct from the tiling guide, which is one polygon a DefinePrefabs step draws at the origin — the tile grid is PrefabField's, not DefinePrefabs'.
_Avoid_: cell, grid square

**PrefabField (step)**:
The LayerBuildStep that places Prefab instances across a Layer's tile grid, referencing exactly one DefinePrefabs step on the same Layer for its tiling settings and available Prefabs. Unlike DefinePrefabs, its Primitives always contribute to the main boolean fold, the same as any ordinary step's. Deleting a DefinePrefabs step or a Prefab that some PrefabField still references is refused, never silently unbound.
_Avoid_: PlacePrefabs (rejected in favour of the PrimitiveField-echoing name)

**Prefab instance**:
One Tile's occupant: a reference to a Prefab plus a rotation, not a copy — editing the Prefab's Primitives changes every instance of it. Rotation is one of the referenced DefinePrefabs step's PrefabTilingType's allowed angles (four for Square: 0/90/180/270), not an arbitrary orientation. Reuses the name of the removed Primitive+TriggerLine clipboard grouping (`addPrefabInstance`), now fully gone from the codebase — the two are unrelated, and this is the concept the name refers to going forward.
_Avoid_: prefab copy, instance (ambiguous alone — this codebase also has C++ class instances, animation-transform instances, etc.)
