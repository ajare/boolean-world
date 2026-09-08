# ADR-0045: Liquid equilibrium uses affine Hydraulic cells

**Status:** Accepted
**Date:** 2026-09-08
**Supersedes in part:** ADR-0032 (face-level capacity, Sill, and depth details)
**Relates to:** ADR-0002 (the Arrangement remains planar), ADR-0005 (`WorldData` is an immutable snapshot)

## Context

ADR-0032 established Liquid as a deterministic post-Arrangement watershed
pass. It correctly decided conservation from authored Primitive volume, Pools,
directed spill across Sills, exterior drains, benign sealed overflow, and
recomputation after every Arrangement build. Its concrete solver nevertheless
made each Arrangement face a hydraulic unit with one scalar floor, ceiling,
capacity, Sill, and finished Liquid depth.

An Elevation plane is affine over the World plane. One Arrangement face can
therefore contain a sloped basin, a varying ceiling, a pinched opening, or
several low regions connected only above an internal rise. Sampling the
plane's base elevation cannot represent any of those cases. Subdividing exact
Arrangement topology merely to follow shorelines or elevation crossings would
also mix transient three-dimensional state into the planar boolean model.

## Decision

Liquid settlement uses one **Hydraulic cell** per generated Arrangement
triangle. Each cell retains its affine floor and ceiling functions and
integrates the vertical capacity below a prospective horizontal Pool elevation.
The deterministic solver finds equilibrium elevation by fixed-iteration
bisection over the sum of those cell capacities. A sealed Pool above total
capacity remains deterministically full and discards excess volume, as decided
by ADR-0032.

**Hydraulic links** join cells across Arrangement edges and artificial
triangulation edges. Their **Sill** is the minimum opening-bottom elevation over
the portions of the shared edge with positive affine floor-to-ceiling
clearance. This permits separate Pools inside one non-convex Arrangement face
until Liquid reaches their connecting route. Explicitly non-colliding exterior
Borders remain permanent drains; ordinary Borders remain watertight.

A Pool has one horizontal equilibrium elevation, but no Arrangement face has a
general scalar Liquid depth. Gameplay samples depth from the containing cell's
Pool elevation and local floor, clamped by the local ceiling. Rendering clips
each cell's wet region against its affine shoreline and ceiling and emits only
the exposed horizontal interface. A completely flooded cell remains
hydraulically connected while emitting no interface.

The old face-depth API remains only as a flat-world compatibility view. It
preserves the prior value for zero-gradient floors and ceilings and returns
zero for non-horizontal faces rather than presenting a sampled elevation as
face-wide truth. Face-level liquid-adjacency likewise remains a compatibility
projection, but is derived from Hydraulic links so sloped and pinched openings
are classified correctly.

Chip and Wedge detail generation does not yet construct geometry in arbitrary
surface planes. Candidates involving non-horizontal floor or ceiling Arrises
are therefore omitted rather than generated from base elevations. Horizontal
candidates retain their existing deterministic generation unchanged.

## Retained decisions from ADR-0032

This ADR does not replace ADR-0032's decisions that:

- authored Liquid volume is `liquidLevel * primitiveRawArea` and is conserved
  over every surviving part of the Primitive;
- Liquid settlement is a distinct deterministic post-Arrangement pass;
- a Wet component may contain multiple Pools;
- Pools merge only when their combined equilibrium reaches the Sill;
- otherwise only volume above a Sill spills, leaving the donor brim-full;
- a reached exterior drain empties its Pool; and
- every Arrangement rebuild derives Liquid state afresh rather than mutating or
  retaining previous Pool identity.

## Consequences

- Sloped floors and ceilings with nonzero Liquid are ordinary supported input.
- Capacity, connectivity, local depth, visible interface geometry, and Sills
  no longer depend on a face-level scalar floor or ceiling.
- Hydraulic triangulation is derived state only; the exact two-dimensional
  Arrangement gains no elevation-only vertices or edges.
- The solver's fixed bisection count makes affine capacity inversion stable and
  deterministic without an epsilon-based convergence condition.
- Position-dependent consumers must use Hydraulic-cell queries. New consumers
  must not use the flat compatibility depth API.
