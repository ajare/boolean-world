# ADR-0044: Steam Audio uses a triangle scene derived from each World snapshot

**Status:** Accepted
**Date:** 2026-09-06
**Supersedes:** ADR-0043
**Relates to:** ADR-0005 (`WorldData` is an immutable snapshot), ADR-0042 (acoustics are simulated in real time)

## Context

ADR-0043 chose a custom Steam Audio scene so ray callbacks could trace the
Arrangement directly and no second geometry representation would need a
lifecycle. It made that choice conditional on measuring the callback path
against Steam Audio's ordinary triangle-mesh `IPLScene`.

The #387 spike built both scenes from `world-mines-1.world.yaml`'s generated
Arrangement (190 faces, 625 horizontal triangles, and 686 walls). The custom
scene used a 2D acceleration-grid march for walls and analytic floor/ceiling
intersections; the mesh scene contained the equivalent floors, ceilings, and
visible wall quads, totalling 2,622 triangles. In an MSVC x64 Release build on
an AMD Ryzen 9 9955HX, Steam Audio 4.8.1's single-threaded Hybrid reflection
simulation was measured over 30 runs after five warm-ups at a fixed candidate
quality preset: 4,096 rays, four bounces, a 1.0-second impulse response,
first-order Ambisonics, one source, and ray batch size one.

The custom scene averaged **18.9 ms** and the triangle scene **10.0 ms**: the
custom path cost **1.89x** as much. Five complete repetitions put the ratio
between 1.87x and 1.91x. An empty-scene control isolated only the DLL-to-host
callback crossing at roughly 0.14 ms per 4,096 callbacks; callback dispatch was
small, but the proposed structural tracer still failed to recover the
triangle scene's highly optimized BVH cost.

## Decision

Steam Audio receives a default triangle-mesh `IPLScene` derived from each
published `ArrangementWorldData` snapshot. The mesh contains the same analytic
surfaces the custom path would have exposed: each solid Arrangement triangle's
floor and ceiling and each visible ArrangementWall's triangular or
quadrilateral surface. Acoustic presets are resolved to the mesh's material
table while it is built.

The scene and the immutable World snapshot it represents move through runtime
handoff as one versioned unit. Replacing a World snapshot builds and commits a
new scene before publishing it to the simulation thread; an in-flight tick
retains the old pair until that tick ends. Geometry is never edited in place.

Keep the custom Arrangement ray tracer as a test-only correctness reference for
the triangle export, not as Steam Audio's runtime scene.

## Consequences

- Reflection simulation uses the measured path that was about 47% cheaper on
  the representative World.
- World commits now have an acoustic-scene build and lifecycle cost. The
  snapshot-pair handoff contains that cost rather than allowing a scene to
  drift out of step with its Arrangement.
- The acoustic mesh is a derived representation, but it has one owner, one
  source snapshot, and no incremental mutation or reverse synchronization.
- Triangle export must be compared against the custom Arrangement reference so
  omitted surfaces, wrong winding, or wrong acoustic-material assignment fail
  as geometry errors rather than presenting as subtly incorrect reverb.
