# ADR-0031: Wedges are World-configured additive post-fold detail geometry

**Status:** Accepted
**Related:** ADR-0005 (immutable World snapshot), ADR-0025 (shared real render pipeline), ADR-0027 (post-fold detail channel)

## Context

A ceiling meeting the top of a Border wall can carry extra geometric detail that fills the corner beneath the ceiling. This resembles Chip generation in timing and rendering needs, but has the opposite geometric meaning: a Chip removes material from an Arris, while this feature adds a tetrahedral volume to one. Calling both features Chips would obscure that distinction.

Like a Chip, this geometry introduces vertices that do not belong to the planar Arrangement and must not alter collision, containment, height queries, surface picking, or the editor's 2D viewport. ADR-0027's post-fold detail channel already provides that visual-only boundary. Unlike Chips, however, Wedges describe the World's structural geometry rather than how a particular material breaks, so making them a Sub-material property would put their ownership in the wrong place.

## Decision

A **Wedge** is additive geometric detail attached beneath the top Arris of a visible generated `Border` wall. Wedges are built after the boolean fold in the same detail channel as Chips. They are never authored as Primitives and are not stored as generated geometry in a World file. `FloorStep` and `CeilingStep` walls are ineligible.

Wedge generation is controlled by serialized World settings exposed in the editor's World settings. The settings consist of an enable toggle and minimum/maximum ranges for:

- **reach**, the total extent along the Arris; as with Chips, width is half-reach;
- **drop-down height**, measured from the ceiling down the wall;
- **projection depth**, measured horizontally from the wall beneath the adjoining ceiling.

Generation defaults to disabled. The default ranges are 4–8 world units for reach, 2–4 for drop-down height, and 2–4 for projection depth. Every value must be finite and strictly positive, and each minimum must not exceed its maximum. Invalid editor edits and invalid serialized Worlds are rejected rather than silently repaired. Editing the settings is atomic and undoable and regenerates the editor preview; Worlds without the setting retain the disabled default.

When enabled, generation makes exactly one Wedge attempt at the midpoint of each eligible Arris. Reach, drop-down height, and projection depth are independent uniform draws from their ranges, using streams derived from a stable hash of the Arris's fixed-point endpoints. Regeneration and unrelated edge ordering therefore cannot move or reshape a Wedge.

The initial Wedge is a tetrahedron with four vertices:

1. two reach endpoints on the top Arris, centred on its midpoint;
2. one point projected horizontally from the midpoint beneath the adjoining ceiling;
3. one point dropped from the midpoint down the wall.

The attachment triangles coincide with the existing ceiling and wall and are not emitted. Only the two exposed sloping triangles render, with flat geometric normals. The original wall and ceiling remain unsuppressed and unchanged. Every exposed facet uses the adjoining solid face's ceiling Sub-material and participates in ordinary opaque rendering, including casting and receiving shadows.

A candidate must fit the available geometry. Arris length limits reach, wall height limits drop-down height, and the complete triangular ceiling attachment footprint must fit inside the adjoining ceiling face, including around holes and non-convex boundaries. Existing Chip cuts are unavailable space: Wedges are generated after Chips and may be reduced or skipped rather than covering or refilling a Chip. Each configured maximum is first capped to available space; dimensions are then drawn from the resulting range. If any configured minimum cannot fit, no Wedge is placed on that wall.

Wedges are evaluated independently and may overlap other Wedges. They are not moved away from the required midpoint to resolve overlap.

`world-test-1.yaml` explicitly enables Wedges with the default ranges so Launcher and editor rendering exercise the feature, while other existing and newly created Worlds remain unchanged until the setting is enabled.

## Consequences

- `ArrangementWorldData` gains additive detail triangles that have no suppressed source surface, extending the detail channel beyond replacement geometry while preserving its visual-only contract.
- Rendering must allow detail triangles to select the adjoining ceiling Sub-material rather than inheriting a wall source's material.
- The game and editor preview must consume the same generated Wedge geometry, as required by ADR-0025.
- World serialization gains geometry-generation settings, but never stores individual Wedges.
- Wedges can intersect one another by design; only insufficient source geometry or Chip occupancy prevents placement.

## Considered alternatives

**Configure Wedges per Sub-material.** Rejected because Wedges represent generated structural geometry, not a material's damage behaviour. The ceiling Sub-material supplies appearance only.

**Make dimensions compile-time constants.** Rejected because Worlds need independent control and the editor must be able to tune the geometry without recompilation.

**Insert Wedges into the Arrangement or ordinary wall/ceiling geometry.** Rejected for the same reasons as Chips in ADR-0027: it would contaminate collision and spatial queries and require maintaining topology that the Arrangement builder currently establishes by construction.

**Render the two attachment faces.** Rejected because they are coplanar with the original ceiling and wall and would introduce overlapping triangles and z-fighting.

**Prevent Wedge-to-Wedge overlap.** Rejected for the initial feature. One independent midpoint attempt per eligible Border wall is simpler and intentional overlap is visually acceptable.
