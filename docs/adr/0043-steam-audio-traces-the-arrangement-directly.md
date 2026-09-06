# ADR-0043: Steam Audio traces the Arrangement directly, not a triangle mesh

**Status:** Superseded by ADR-0044
**Date:** 2026-09-05
**Relates to:** ADR-0042 (acoustics are simulated in real time), ADR-0005 (`WorldData` is an immutable snapshot)

## Context

Steam Audio's conventional integration hands it a triangle soup with per-triangle
acoustic materials, from which it builds a BVH and traces rays. For a
hand-modelled level that is the obvious path.

Here it would mean maintaining a second, derived representation of the world
geometry — built from the Arrangement, rebuilt on every commit, and subject to
exactly the lifecycle problems that have made every other derived structure in
this codebase expensive. It would also ask a general 3D BVH to trace a world
that is not general: a planar Arrangement extruded between per-face `floorZ` and
`ceilingZ`, with an acceleration grid over its walls already built and already
documented as the right structure for line-of-sight queries.

## Decision

Steam Audio is given a **custom scene**: it calls back into the game for every
ray query it needs, for occlusion and reflections alike, and those callbacks are
answered directly from `ArrangementWorldData` — a 2D march over the rendered
wall grid for walls, analytic plane intersections for floors and ceilings.

No acoustic geometry representation exists. There is nothing to build, nothing
to rebuild on commit, and nothing to keep in step with the Arrangement.

Ray hits report an **Acoustic preset**, referenced by stable id from the
Sub-material of the surface that was hit, resolved outside the World in the same
way Sub-material and Embossing references already are.

Because the ray callbacks run on the simulation thread, they read the snapshot
concurrently with the game thread. ADR-0005 makes that safe: `WorldDataPtr` is a
`shared_ptr<ArrangementWorldData const>`, so the simulation thread takes its own
reference at the start of each tick and releases it at the end. A commit that
lands mid-tick means the tick finishes against the old world and the next one
picks up the new; the refcount keeps the old snapshot alive exactly as long as
it is being traced. No geometry is locked.

## Considered alternatives

**Build an `IPLScene` from triangles.** The conventional path, and the fallback
if callback overhead proves fatal. Rejected as the primary design: it
reintroduces a derived structure and its lifecycle, and gives up the structural
knowledge that makes tracing this world cheap.

**Blocking commits until the simulation tick completes.** Rejected: it couples
world generation latency to audio and buys nothing that the refcount does not
already provide.

**Running simulation on the existing concurrencpp executor.** Rejected in favour
of a dedicated thread. World generation is CPU-heavy and runs on that pool, so
sharing it stalls reflections precisely when geometry changes — the moment the
acoustics are most obviously wrong and the player most likely to notice.

## Consequences

- The acoustic scene lifecycle problem does not exist, because the acoustic
  scene does not exist.
- The project owns a ray tracer. Its bugs present as subtly wrong reverb rather
  than as crashes, which makes a comparison against a triangle-mesh scene on a
  known world the natural regression test.
- Ray callbacks are per-ray and cross a plugin boundary. If that overhead
  outweighs the structural win, the triangle-mesh fallback is available — and by
  then there is a correct 2.5D reference to validate it against.
- Anything the ray callbacks touch must be safe for concurrent reads.
  `ImmutableAccelerationGrid` is named for this, but any lazy caching inside a
  query path would be a data race presenting as rare, unreproducible glitches.
- Acoustic character is authored where visual character already is: a surface
  that looks like stone sounds like stone, because both hang off the same
  Sub-material.
