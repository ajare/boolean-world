# Clipping Generation

Clipping works as follows:

## Clipper::clipToClipper2Polygons()

This function builds up a list of operations to perform on a set of Primitives.  It splits the list each time it encounters a Union operation.
The idea is that a series of detailed Primitives is built up by taking an initial subject Primitive, and then applying Difference operations to
it to carve out detail.  These "intermediate Primitives" are then Unioned together.  XOR and Intersection can also be used as well as Difference
but have limited use.

## Clipper::clip()

The following process is used:

1. Generate the intermediate clippings as described above.
2. Union them together to create a border clipping, and interpolate vertex data.
3. XOR them together, in order to cut up the Primitives like a jigsaw.
4. From the XOR clipping, we need to reconstruct the individual pieces, which is done by
   calculating the Difference and Intersection of each intermediate clipping against it.
5. There will be duplicate polygons, where any Primitives overlap in the original configuration.
   These are removed in two places:
   - When the graph is created, we make sure no duplicate lines are added.