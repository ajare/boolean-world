# Boolean World Geometry

Boolean World Geometry turns authored two-dimensional shapes into the regions and surfaces of a three-dimensional world.

## Language

**Primitive**:
An authored closed shape that contributes an operation, fill rule, priority, and regional properties to a world. Owned by exactly one Layer.
_Avoid_: Path primitive, clip shape

**Layer**:
A named, owned collection of Primitives and WorldTriggerLines within a World. A generation selects a set of Layers to fold together; the World's active Layer is the one currently focused for authoring.
_Avoid_: Layer tag, layer id (as a primitive attribute)

**Contour**:
A closed sequence of fixed-point vertices forming one boundary of a primitive. A primitive may contribute multiple contours whose combined interior is determined by its fill rule.
_Avoid_: Clipper path, Clipper polygon

**Fixed-point vertex**:
An exact point on the world geometry grid. It is the canonical coordinate type for topology and arrangement output.
_Avoid_: Clipper point, floating-point topology vertex

**Arrangement**:
The planar subdivision induced by all selected primitive contours. Its faces are classified by primitive membership and the priority-ordered fold.
_Avoid_: Clip result
