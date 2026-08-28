# ADR-0032: Water equilibrium is a deterministic post-arrangement watershed pass

**Status:** Accepted
**Related:** ADR-0004 (faces are the unit of derived truth; membership already tracks every covering Primitive), ADR-0027 (precedent for a distinct post-fold detail channel layered after the core Arrangement)

## Context

A Primitive with a Union operation can be given an authored Water level (default zero), representing a volume of water — `waterLevel * area` — poured into the world at that Primitive's location. After the fold, each Arrangement face may draw its floor from several such Primitives at once (its `membership` bitset), and neighbouring faces at different floor heights can exchange water across a shared wall if that wall has clearance. The end state a level designer expects is physical: water seeks its own level, spreads from a wet Primitive into an adjoining dry one that is lower, and two initially separate pools merge into a single surface if they rise enough to connect through a low pass. Nothing about that end state is expressible as a per-face local computation — it is a global equilibrium over however many faces turn out to be connected once water is considered.

The codebase has no precedent for this kind of pass: there is no existing area API (`Primitive`, `ComplexPolygon`, and `ArrangementFace` only have bounding-box or internal fixed-point winding helpers), no generic face-adjacency query (adjacency is implicit in `ArrangementEdge::face[0]/face[1]`), and no iterative solver anywhere in the pipeline. Two genuinely different shapes were available for solving the equilibrium itself: an iterative numerical relaxation that nudges volume between neighbours each step until a convergence threshold is met, or a closed-form rising-level fill borrowed from terrain-hydrology depression-filling (a priority-flood / watershed algorithm).

A second question was where a Primitive's water volume comes from when the fold carves it up: whether "its area" means the Primitive's raw, as-if-solo shape, or the sum of the areas of the faces it actually still contributes to after other operations cut into it.

## Decision

Water level is computed by a distinct post-arrangement pass, e.g. `ComputeWaterLevels(ArrangementResult&)`, run after `BuildArrangement` (and after wall-building), the same way `BuildArrangementWalls` sits after core arrangement construction rather than inside it. It is a one-shot derived computation, recomputed whenever the arrangement or any Primitive's Water level changes — not a time-stepped simulation, and it produces no rendering or mesh output.

Each face's undistributed water level is the sum, over every Union-operation Primitive in that face's `membership`, of `waterLevel * faceArea / primitiveRawArea`, where `primitiveRawArea` is the Primitive's own raw geometric area computed as if it existed alone. A Primitive later carved away by a Difference, Intersection, or XOR operation therefore redistributes less than its full declared volume — that lost volume is not conserved elsewhere. The alternative, using the sum of areas the Primitive still contributes to post-fold, was rejected: it matches the literal wording of the design less well, and it creates a dependency cycle between "how much area a Primitive covers" and "which faces exist," where the raw-area version needs nothing but the Primitive's own geometry.

Water-adjacency is the relation between two non-solid faces whose shared edge has nonzero wall clearance — reusing the existing collision-clearance concept rather than inventing a second one, so a solid wall that already blocks the player also blocks water. The Arrangement's outer, unbounded face is water-adjacent to every bordering face with an effective floor of negative infinity, making it a permanent drain: any Wet component touching it settles at zero depth throughout.

Equilibrium is solved with a deterministic rising-level watershed fill: faces are processed in floor-height order, connected reachable regions are merged via union-find as the notional water level conceptually rises, and each resulting component's equilibrium elevation is solved exactly from total volume versus capacity-below-that-elevation — no epsilon, no iteration count, no convergence loop. Two Wet components merge into one the moment rising water would connect them across a saddle (full watershed behaviour, not independently resolved pools). All comparisons and the equilibrium value are absolute elevation (`floorZ + depth`), and each face's depth is capped at its own `ceilingZ`. A sealed component with more volume than total capacity simply fills every member face to its ceiling and silently discards the excess — this is treated as a benign level-authoring overshoot, not an error worth asserting on.

The finished value stored per face is water **depth** (`clamp(equilibriumElevation - floorZ, 0, ceilingZ - floorZ)`), not absolute elevation, so consumers get a directly usable "how much water sits on this floor" and zero reads naturally as dry.

## Consequences

- `PrimitivePropertySet` gains a `waterLevel` float (default zero), serialized like `floorZ`/`ceilingZ`; it exists on every Primitive but is inert unless that Primitive's operation is Union. Editor UI gating it to Union-only primitives is a deferred follow-up, not part of this decision.
- `ArrangementFace` gains a derived water-depth field, populated by the new pass rather than during `BuildArrangement`.
- A public area computation is introduced for the first time (raw Primitive area and face area, both shoelace-based and hole-aware), since no such API existed before this feature needed one.
- This design introduces the codebase's first iterative/graph-equilibrium solver; nothing else in the fold pipeline follows this shape yet.
- Because a merged component resolves to exactly one surface elevation, a full pool that spills over a saddle into a lower, drier basin equalizes with it completely rather than draining down only as far as the saddle and stopping there. Retaining water up to the saddle would need a directed, partial volume transfer between pools instead of a union — a strictly larger algorithm than "one component, one elevation," and it is not what this decision buys.
- The seed step (`ComputeUndistributedWaterDepths`) deliberately leaves each face's depth uncapped by its own clearance; capping there would destroy volume that has to flow onward, so the ceiling cap applies only once, to the settled surface.
- Rendering or meshing a water surface, and any animated/time-stepped flow, are explicitly out of scope for this decision.

## Considered alternatives

**Iterative numerical relaxation instead of a closed-form watershed fill.** Rejected: it is approximate, needs a floating-point conservation tolerance and an iteration cap, and can be slow to converge over many faces — the closed-form fill produces an exact "single equilibrium value" directly, with no tuning.

**A Primitive's water volume based on its post-fold contributed area rather than its raw solo area.** Rejected: it would make a Primitive's declared volume dependent on which faces happen to exist after other operations carve it, rather than on the Primitive's own geometry, and it reads against the plain wording of "its area."

**Independently resolving each seeded Wet component, never merging across a rising saddle.** Rejected: real connected water does merge, and this is exactly the scenario the feature calls out by name — a dry face gaining water because a higher, wetter neighbour reaches it.

**Treating the outer/unbounded face as a wall instead of a drain.** Rejected: an open edge of the world is a spillway, not a container wall, and treating it as a wall would require inventing an arbitrary boundary convention that doesn't otherwise exist in the Arrangement.
