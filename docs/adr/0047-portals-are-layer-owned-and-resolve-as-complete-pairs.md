# ADR-0047: Portals are Layer-owned and resolve as complete pairs

**Status:** Accepted
**Date:** 2026-11-21
**Relates to:** ADR-0005, ADR-0009, ADR-0013, ADR-0019

## Context

A Portal must keep an authored identity while the Arrangement edges beneath it are regenerated, split, or removed. Treating an endpoint as an ArrangementWall property would make that identity derived and unstable; treating the two endpoints independently would allow Layer selection or a failed resolution to create half a link. Generation also needs to normalize unequal authored widths without silently rewriting the author's dimensions.

## Decision

A Portal pair is first-class authored state permanently owned by one Layer. It has one stable Layer-local id and exactly two stable endpoint slots; each endpoint stores only its authored aperture (World-plane centre, width, bottom, and top), never an Arrangement edge index. Portal pairs are not LayerBuildStep output and are copied and serialized with their owning Layer.

A generation snapshots a pair only when its owning Layer is selected, then resolves both endpoints against rendered ArrangementWall coverage in that same Arrangement snapshot. Resolution is immutable data on `ArrangementWorldData`. Both resolved apertures use the smaller authored width, centred independently on each authored centre. Unequal heights, player-clearance failure, incomplete rendered-wall coverage, or either unresolved endpoint makes the whole pair inactive; generation retains diagnostics and any independently resolved endpoint for authoring feedback but does not alter authored state or the underlying walls.

## Consequences

- Arrangement edge and wall indices may exist in a resolved aperture only for the lifetime of its immutable WorldData snapshot and are never serialized.
- Layer selection cannot include one endpoint without the other, and geometry excluded from a generation cannot resolve a selected pair.
- Later rendering, collision, traversal, Liquid, and lighting work consumes one coherent resolved pair rather than re-resolving authored endpoints independently.
- A wider authored endpoint can regain its full authored width if its partner changes; generation normalization is non-destructive.
- Worlds with no Portal pairs omit the optional keyed Portal section and produce no Portal resolution work or geometry changes.
