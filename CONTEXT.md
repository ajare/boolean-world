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
A named, stably-identified collection of Primitives authored as a unit so that later LayerBuildSteps can place and manipulate copies of it. Holds Primitives only — never WorldTriggerLines, unlike the removed `addPrefabInstance` copy-paste grouping that once bore this name. A Prefab's Primitives never contribute world geometry; they exist to be authored and, later, instanced. A Prefab's pivot — the point its Primitives orbit when it is rotated — is the origin, not the centre of its contents.
_Avoid_: prefab instance (the removed Primitive+TriggerLine clipboard grouping), group, template

**DefinePrefabs (step)**:
The LayerBuildStep that owns a set of Prefabs. It defines rather than places: outside an authoring session it contributes nothing to its Layer at all. Which Prefab is being edited is ephemeral editor focus — never serialized, and unselected after construction, copy, or load — mirroring a Layer's active step and a World's active Layer.
_Avoid_: PrefabField, PlacePrefabs (a future, separate step type)

**Tiling guide**:
The single wireframe polygon a DefinePrefabs step draws, centred on the origin at the step's chosen PrefabTilingType and size. It is the frame a Prefab is authored against — the visible form of the Prefab's pivot — and is unrelated to the editor's snapping grid.
_Avoid_: grid, prefab grid, tile (a tile is a cell a placing step later fills; the guide is one polygon at the origin)
