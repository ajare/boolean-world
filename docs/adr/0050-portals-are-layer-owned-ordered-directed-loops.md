# ADR-0050: Portals are Layer-owned ordered directed loops

**Status:** Partially superseded by ADR-0051 (named Portal expansion)
**Date:** 2026-09-25
**Supersedes:** ADR-0047 (fixed two-endpoint slots)
**Supersedes in part:** ADR-0045 (bidirectional Portal Liquid amendment)
**Relates to:** ADR-0005, ADR-0009, ADR-0013, ADR-0019

## Context

ADR-0047 made Portals first-class Layer-owned pairs resolved atomically against immutable World snapshots. Exactly two endpoint slots made the partner implicit, but cannot express `A → B → C → A`. Retaining both pair and loop models would allow traversal, views, lighting, and Liquid to disagree about destinations. The existing bidirectional Portal Liquid amendment in ADR-0045 likewise does not describe directed cycles.

## Decision

A Portal loop is an ordered cycle of at least two endpoints, permanently owned by one Layer, not by a LayerBuildStep or ArrangementWall. The Layer-local loop ID is stable. Every endpoint has a stable, never-reused loop-local ID independent of its mutable traversal position; the loop keeps a monotonic next-ID allocator and explicit order. Insertions splice after an endpoint, deletion reconnects its neighbours only when more than two remain, and reordering preserves IDs and apertures. Entering position `i` routes to `(i + 1) mod N` through one canonical next-endpoint lookup. In the two-endpoint case both routes lead to the other endpoint.

Generation snapshots complete loops from selected Layers and resolves every endpoint against the same immutable Arrangement snapshot. It narrows each resolved aperture around its own authored centre to the smallest authored width in the loop without altering authored dimensions. The loop is active only when every endpoint resolves, all heights match within the existing tolerance, and player clearance and rendered-wall coverage succeed. Partial successful resolutions remain available for diagnostics. The canonical rigid transform from each source to its next destination reverses tangent and front, preserves scale, handedness and World-up, and translates elevation by the difference between aperture bottoms. Rendering, traversal, collision, Torch placement, lighting and Liquid consume that mapping, not independently inferred partners. Existing traversal and recursion budgets remain.

Portal liquid-adjacency is directed from each endpoint's incident Hydraulic cells to the next endpoint's cells. Each hop is gated by its source Sill and maps the surface to the same height above the destination bottom. Liquid settles deterministically at generation time without retained runtime flow. Loops are trialled atomically in stable Layer-id/loop-id order against ordinary Hydraulic links and previously accepted loops; missing cells or contradictory accumulated offsets omit the whole loop from Liquid, with one diagnostic, without deactivating rendering or traversal.

Old serialized pairs load as two-endpoint loops with endpoint IDs 0 and 1 and their original Layer-local loop ID. New saves use only the loop schema. Pair compatibility is confined to the legacy serialized-pair reader; there are no runtime pair aliases, adapters, or accessors. Resolved loops have no implicit endpoint slots or default traversal order.

## Consequences

- Endpoint identity never depends on its vector position, generated wall identity, or render snapshot.
- One unresolved endpoint makes the complete loop inactive, while a Liquid-only conflict affects only Liquid.
- ADR-0047 remains historical evidence for ownership and atomic resolution; its fixed slots are replaced. ADR-0045's affine Hydraulic cells remain in force, but its bidirectional Portal Liquid amendment is replaced.
- No arbitrary authored endpoint maximum, branching routes, reverse traversal, or cross-Layer loops are introduced.
