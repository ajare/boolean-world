# Remove Clipper2 and path primitives

Clipper2 remained after the arrangement rewrite only as a fixed-point container library and as the offsetting implementation behind authored `Path` primitives.

## Decision

Remove the dependency completely, replace its container types with native `bw::core::arr` fixed-point vertices and contours, and delete `PathPolygon` rather than maintain or replace its path-stroking algorithm.

Worlds containing a serialized `Path` primitive are not migrated or partially loaded: `Path` is an unknown primitive type and loading fails through the standard unknown-primitive error. The independent older Clipper implementation in `Willpower.Geometry` is unaffected.

## Consequences

- The fixed-point scale, rounding, topology, and arrangement behavior remain unchanged.
- Contours are closed vertex sequences; shell and hole relationships are derived geometrically rather than stored as intrinsic contour metadata.
- Clipper2 headers, libraries, allocator lifecycle, build integration, compatibility types, and path authoring UI are removed from every BooleanWorld target.
- Historical documentation may still name Clipper2 when describing the retired boolean pipeline, but no current architecture depends on it.
