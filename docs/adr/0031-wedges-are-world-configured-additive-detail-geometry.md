# ADR-0031: Wedges are World-configured additive post-fold detail geometry

**Status:** Accepted
**Related:** ADR-0005 (immutable World snapshot), ADR-0025 (shared real render pipeline), ADR-0027 (post-fold detail channel)

## Context

A floor or ceiling meeting a Border wall can carry extra geometric detail that fills the corner above the floor or beneath the ceiling. This resembles Chip generation in timing and rendering needs, but has the opposite geometric meaning: a Chip removes material from an Arris, while this feature adds a tetrahedral volume to one. Calling both features Chips would obscure that distinction.

Like a Chip, this geometry introduces vertices that do not belong to the planar Arrangement and must not alter its topology, wall collision, containment, surface picking, or the editor's 2D viewport. ADR-0027's post-fold detail channel provides that boundary while still allowing floor collision to consume selected derived facets. Unlike Chips, however, Wedges describe the World's structural geometry rather than how a particular material breaks, so making them a Sub-material property would put their ownership in the wrong place.

## Decision

A **Wedge** is additive geometric detail attached to visible generated `Border` walls. An **Edge Wedge** is centred beneath a ceiling Arris or above a floor Arris. A **Corner Wedge** occupies the trihedral Corner where the same floor or ceiling meets two connected Border walls. Wedges are built after the boolean fold in the same detail channel as Chips. They are never authored as Primitives and are not stored as generated geometry in a World file. `FloorStep` and `CeilingStep` walls are ineligible.

Wedge generation is controlled by serialized World settings exposed in the editor's World settings. Alongside the enable toggle, independent floor and ceiling **average Wedges per unit distance** control Edge Wedge frequency, and a **Corner probability** controls each floor or ceiling Corner attempt. Dimension settings provide minimum/maximum ranges for:

- **reach**, the total extent along the Arris; as with Chips, width is half-reach;
- **vertical extent**, stored as drop-down height and measured down from the ceiling or up from the floor;
- **projection depth**, measured horizontally from the wall across the adjoining floor or ceiling;
- **Corner reach**, measured independently away from the Corner along each incident horizontal Arris;
- **Corner vertical extent**, measured down or up the shared vertical Arris.

Generation defaults to disabled. Floor and ceiling Edge frequency both default to 0.05 per world unit, while Corner probability defaults to one. The default Edge ranges are 12–24 world units for reach, 6–12 for vertical extent, and 6–12 for projection depth. Corner reach and Corner vertical extent both default to 6–12 world units. Dimension values must be finite and strictly positive, each minimum must not exceed its maximum, and frequency/probability values must be finite and in [0, 1]. Invalid editor edits and invalid serialized Worlds are rejected rather than silently repaired. Editing the settings is atomic and undoable and regenerates the editor preview; Worlds without the setting retain the disabled default.

When enabled, each eligible Border wall independently derives its floor and ceiling attempt counts by rounding Arris length multiplied by the corresponding average-per-unit-distance value to the nearest whole number. A single attempt remains at the midpoint; multiple attempts are distributed through equal-length slots within the interval where the minimum reach fits, with a deterministic random offset in each slot. Reach, vertical extent, and projection depth are independent uniform draws from their ranges, using candidate-specific streams derived from a stable hash of the Arris's fixed-point endpoints. Regeneration and unrelated edge ordering therefore cannot move or reshape an Edge Wedge.

An Edge Wedge begins with a tetrahedral envelope defined by four attachment vertices:

1. two reach endpoints on the floor or ceiling Arris, centred on the candidate position;
2. one point projected horizontally from that position across the adjoining horizontal surface;
3. one point extending from that position up or down the wall.

The exposed centre line between the projected and wall-extending points is split into three segments. Its two interior points are offset horizontally ten percent toward the wall, with a quadratic falloff to zero at both endpoints while retaining their linear heights. This makes the exposed surface subtly convex and arches its centre line inward toward the wall. Each side is emitted as a three-triangle fan with flat geometric normals, for six exposed triangles per Wedge.

At each eligible non-collinear pair of connected Border walls, generation independently applies the configured probability to one floor and one ceiling Corner Wedge candidate. A Corner Wedge's tetrahedral envelope has the hidden Corner plus three independently drawn exposed points: one along each incident horizontal Arris and one along their shared vertical Arris. Only the triangular face joining those exposed points renders. Its stable seed uses the fixed-point Corner, horizontal-surface height, and dedicated streams for both reaches and vertical extent. The complete horizontal attachment triangle must fit the shared solid face; both wall attachment triangles must avoid their exact Chip reservations. If any Corner minimum cannot fit, that Corner Wedge is skipped.

The attachment triangles coincide with the existing horizontal surface and wall and are not emitted. The original floor, ceiling, and wall remain unsuppressed and unchanged. Floor Wedge facets join the immutable floor-collision mesh: floor-height queries return the highest containing floor Wedge facet above the source floor, including where independently generated Wedges overlap. Ceiling Wedges remain absent from collision. Neither kind changes planar containment, wall collision, surface picking, or editor 2D geometry. Every exposed facet uses its adjoining floor or ceiling Sub-material and participates in ordinary opaque rendering, including casting and receiving shadows.

An Edge candidate must fit the available geometry. Arris length limits reach, wall height limits vertical extent, and the complete triangular horizontal attachment footprint must fit inside the adjoining face, including around holes and non-convex boundaries. Existing Chip cuts are unavailable space: Wedges are generated after Chips and may be reduced or skipped rather than covering or refilling a Chip. Each configured maximum is first capped to available space; dimensions are then drawn from the resulting range. If any configured minimum cannot fit, no Wedge is placed on that wall.

Wedges are evaluated independently and may overlap other Wedges. Edge Wedges are not moved away from their deterministic candidate positions, and Corner Wedges are not moved away from their Corner, to resolve overlap.

`world-test-1.yaml` explicitly enables Wedges with the default frequency, probability, and dimension values so Launcher and editor rendering exercise the feature, while other existing and newly created Worlds remain unchanged until the setting is enabled.

## Consequences

- `ArrangementWorldData` gains additive detail triangles that have no suppressed source surface. It indexes floor Wedge facets separately for floor-height collision while keeping Chips and ceiling Wedges render-only.
- Rendering must allow detail triangles to select the adjoining floor or ceiling Sub-material rather than inheriting a wall source's material.
- The game and editor preview must consume the same generated Wedge geometry, as required by ADR-0025.
- World serialization gains geometry-generation settings, but never stores individual Wedges.
- Wedges can intersect one another by design; only insufficient source geometry or Chip occupancy prevents placement.

## Considered alternatives

**Configure Wedges per Sub-material.** Rejected because Wedges represent generated structural geometry, not a material's damage behaviour. The adjoining floor or ceiling Sub-material supplies appearance only.

**Make dimensions compile-time constants.** Rejected because Worlds need independent control and the editor must be able to tune the geometry without recompilation.

**Insert Wedges into the Arrangement or ordinary wall/ceiling geometry.** Rejected because it would require maintaining topology that the Arrangement builder currently establishes by construction. Floor collision instead indexes the generated floor facets directly, leaving planar containment and wall queries unchanged.

**Render the two attachment faces.** Rejected because they are coplanar with the original horizontal surface and wall and would introduce overlapping triangles and z-fighting.

**Prevent Wedge-to-Wedge overlap.** Rejected. Independent Edge and Corner attempts are simpler, and intentional overlap is visually acceptable.
