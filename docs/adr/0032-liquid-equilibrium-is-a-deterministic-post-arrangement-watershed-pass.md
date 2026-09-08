# ADR-0032: Liquid equilibrium is a deterministic post-arrangement watershed pass

**Status:** Accepted; face-level capacity and depth partially superseded by #425
**Related:** ADR-0004 (faces are the unit of derived truth; membership already tracks every covering Primitive), ADR-0027 (precedent for a distinct post-fold detail channel layered after the core Arrangement)

## Sloped Hydraulic cell amendment

#425 replaces an Arrangement face as the unit of Liquid capacity with one
**Hydraulic cell** per generated Arrangement triangle. Pool capacity is the sum
of the cells' integrated affine floor-to-ceiling columns, Pool elevation is
solved by deterministic fixed-iteration bisection, local Liquid depth is sampled
beneath that horizontal elevation, and visible interfaces are clipped to each
cell's wet portion.

#426 replaces the face-level settlement graph with **Hydraulic links** across
both Arrangement edges and artificial triangulation edges. A link exists only
where the maximum adjacent floor is below the minimum adjacent ceiling, and its
Sill is the lowest opening-bottom elevation over those traversable portions.
This lets one non-convex face retain separate low Pools until their connecting
route is reached. Explicitly non-colliding exterior Borders produce drain links;
ordinary Borders remain watertight. The conservation, Pool, watershed,
directed-spill, overflow, and exterior-drain decisions below remain in force.

## Context

A Primitive with a Union operation can be given an authored Liquid level (default zero), representing a volume of liquid — `liquidLevel * area` — poured into the world at that Primitive's location. After the fold, each Arrangement face may draw its floor from several such Primitives at once (its `membership` bitset), and neighbouring faces at different floor heights can exchange liquid across a shared wall if that wall has clearance. The end state a level designer expects is physical: liquid seeks its own level, spreads from a wet Primitive into an adjoining dry one that is lower, and two initially separate pools merge into a single surface if they rise enough to connect through a low pass. Nothing about that end state is expressible as a per-face local computation — it is a global equilibrium over however many faces turn out to be connected once liquid is considered.

The codebase has no precedent for this kind of pass: there is no existing area API (`Primitive`, `ComplexPolygon`, and `ArrangementFace` only have bounding-box or internal fixed-point winding helpers), no generic face-adjacency query (adjacency is implicit in `ArrangementEdge::face[0]/face[1]`), and no iterative solver anywhere in the pipeline. Two genuinely different shapes were available for solving the equilibrium itself: an iterative numerical relaxation that nudges volume between neighbours each step until a convergence threshold is met, or a closed-form rising-level fill borrowed from terrain-hydrology depression-filling (a priority-flood / watershed algorithm).

A second question was where a Primitive's liquid volume comes from when the fold carves it up: whether "its area" means the Primitive's raw, as-if-solo shape, or the sum of the areas of the faces it actually still contributes to after other operations cut into it.

## Decision

Liquid level is computed by a distinct post-arrangement pass, e.g. `ComputeLiquidLevels(ArrangementResult&)`, run after `BuildArrangement` (and after wall-building), the same way `BuildArrangementWalls` sits after core arrangement construction rather than inside it. It is a one-shot derived computation, recomputed whenever the arrangement or any Primitive's Liquid level changes — not a time-stepped simulation, and it produces no rendering or mesh output.

A Primitive's authored liquid volume is `liquidLevel * primitiveRawArea`, where `primitiveRawArea` is the Primitive's own raw geometric area computed as if it existed alone. Basing the volume on raw area rather than on post-fold contributed area keeps it a property of the Primitive's own geometry, with no dependency on which faces happen to exist.

That volume is **conserved**, and is seeded at one uniform depth across the Primitive's surviving footprint: each face's undistributed liquid depth is the sum, over every Union-operation Primitive in that face's `membership`, of `liquidLevel * primitiveRawArea / primitiveSurvivingArea`, where `primitiveSurvivingArea` totals the areas of every solid face that Primitive is still a member of. The fold routinely splits one Primitive across several faces without removing any of it — any other Primitive's edge crossing it does that — and such a split must not change the answer, so depth cannot be decided from the area of the single face being seeded. Computing it needs the surviving total first, which makes seeding a two-pass computation over the faces rather than a per-face one.

A Primitive genuinely carved away by a Difference, Intersection, or XOR operation therefore keeps its whole volume in whatever area remains, standing correspondingly deeper — a pillar sunk into a flooded room displaces liquid rather than deleting it. The alternative, letting the carved-away share simply vanish, was rejected: authoring must not silently lose liquid, and there is no point in the fold at which a "removed" area can be distinguished from a merely subdivided one.

Liquid-adjacency is the relation between two solid faces (the same faces BuildArrangementTriangles renders and the player walks on) whose shared edge has nonzero wall clearance — reusing the existing collision-clearance concept rather than inventing a second one, so a solid wall that already blocks the player also blocks liquid. The same reuse applies at the world's own edge: the Arrangement's outer, unbounded face is liquid-adjacent to a bordering solid face, with an effective floor of negative infinity, only where that boundary's Border wall is explicitly authored not to collide — an ordinary wall there is solid by default, the same as any other room wall, and merely having nothing authored beyond it is not an opening. Where it is open, it acts as a permanent drain: any Wet component reaching it settles at zero depth throughout.

Equilibrium is solved with a deterministic rising-level watershed fill: faces are processed in floor-height order, connected reachable regions are merged via union-find as the notional liquid level conceptually rises, and each resulting Pool's equilibrium elevation is solved exactly from total volume versus capacity-below-that-elevation — no epsilon, no iteration count, no convergence loop. All comparisons and the equilibrium value are absolute elevation (`floorZ + depth`), and each face's depth is capped at its own `ceilingZ`. A sealed Pool with more volume than total capacity simply fills every member face to its ceiling and silently discards the excess — this is treated as a benign level-authoring overshoot, not an error worth asserting on.

A Wet component is the connectivity, not the body of liquid: it settles as one or more Pools, and the fill decides which by asking, at each Sill it reaches, what elevation the two Pools either side would settle at as a single body. At or above the Sill, the Sill is submerged and they really are one — two ponds becoming one lake the moment the rising surface tops the saddle between them — so they merge. Below it they are not, and what is happening instead is a directed spill: the higher Pool is pouring over a saddle into somewhere lower and drier, and the pouring stops the moment its own surface falls back to the saddle. Only the liquid standing above the Sill crosses; the donor is left standing exactly brim-full at the Sill and the two Pools survive at their two different elevations. Equalizing them anyway would sink a full room below the rim it is pouring over, which is not what liquid does.

The exterior drain is the one place a merge happens unconditionally: any Pool that reaches the unbounded face empties completely rather than draining down to the elevation of its opening, and so does anything that later spills into it.

The finished value stored per face is liquid **depth** (`clamp(equilibriumElevation - floorZ, 0, ceilingZ - floorZ)`), not absolute elevation, so consumers get a directly usable "how much liquid sits on this floor" and zero reads naturally as dry.

## Consequences

- `PrimitivePropertySet` gains a `liquidLevel` float (default zero), serialized like `floorZ`/`ceilingZ`; it exists on every Primitive but is inert unless that Primitive's operation is Union. Editor UI gating it to Union-only primitives is a deferred follow-up, not part of this decision.
- `ArrangementFace` gains a derived liquid-depth field, populated by the new pass rather than during `BuildArrangement`.
- A public area computation is introduced for the first time (raw Primitive area and face area, both shoelace-based and hole-aware), since no such API existed before this feature needed one.
- This design introduces the codebase's first iterative/graph-equilibrium solver; nothing else in the fold pipeline follows this shape yet.
- A Wet component can finish holding several Pools at several different elevations, so no consumer may assume one liquid surface per connected region. Termination does not rest on that, though: every step of the fill either merges two Pools — at most once per face — or leaves a donor standing exactly at a Sill, which cannot spill over that Sill again until something pours into it, and can only be poured into from strictly higher up. The "already brim-full" test is exact rather than epsilon-guarded: re-running the capacity computation over an unchanged member list and an unchanged Sill reproduces the retained volume bit-for-bit, so the spilled volume subtracts to exactly zero.
- The seed step (`ComputeUndistributedLiquidDepths`) deliberately leaves each face's depth uncapped by its own clearance; capping there would destroy volume that has to flow onward, so the ceiling cap applies only once, to the settled surface.
- Seeding is two passes over the faces, not one: every Union Primitive's surviving area must be totalled before any face carrying it can be given a depth. The seeded total volume is an invariant worth testing directly — it must equal `Σ liquidLevel * primitiveRawArea` over Union Primitives whose footprint survives at all, however the fold subdivided them.
- Rendering or meshing a liquid surface, and any animated/time-stepped flow, are explicitly out of scope for this decision.

## Considered alternatives

**Iterative numerical relaxation instead of a closed-form watershed fill.** Rejected: it is approximate, needs a floating-point conservation tolerance and an iteration cap, and can be slow to converge over many faces — the closed-form fill produces an exact "single equilibrium value" directly, with no tuning.

**A Primitive's liquid volume based on its post-fold contributed area rather than its raw solo area.** Rejected: it would make a Primitive's declared *volume* dependent on which faces happen to exist after other operations carve it, rather than on the Primitive's own geometry, and it reads against the plain wording of "its area." Note that surviving area does decide the *depth* that volume settles to — it is the divisor, not the multiplier — which is a two-pass computation over the faces, not a dependency cycle.

**Seeding each face at `liquidLevel * faceArea / primitiveRawArea`.** Rejected — and this was the shipped behaviour until it was found to be wrong. A face's volume is its depth times its area, so that expression contributes `liquidLevel * faceArea² / primitiveRawArea`, which totals the authored volume only when the Primitive occupies exactly one face. Splitting a Primitive into *n* equal faces divides its liquid by *n*, and splitting is not something an author does deliberately: any unrelated Primitive whose edge crosses this one causes it. In `world-test-1.yaml` a fully intact Primitive subdivided into four faces retained 1317 of its authored 4000 units of volume, settling roughly eight units below where it belonged and so never reaching the sill it should have spilled over.

**Independently resolving each seeded Wet component, never merging across a rising saddle.** Rejected: real connected liquid does merge, and this is exactly the scenario the feature calls out by name — a dry face gaining liquid because a higher, wetter neighbour reaches it.

**Merging every Pool that reaches a Sill, so that one Wet component always resolves to one elevation.** Rejected: it is the simpler algorithm — union-find and nothing else — but it sinks a full room below the rim it is pouring over. A room brim-full at elevation 30, spilling through a passage into a deep dry room beyond, would equalize with it and end up holding nothing, when what liquid actually does is pour over the rim until its own surface falls back to 30 and then stop. The directed spill costs one extra branch in the same loop.

**Treating the outer/unbounded face as a wall instead of a drain.** Rejected: an open edge of the world is a spillway, not a container wall, and treating it as a wall would require inventing an arbitrary boundary convention that doesn't otherwise exist in the Arrangement.

**Treating every solid face bordering the unbounded exterior as a drain, regardless of that boundary's own Border wall collision.** Rejected — and this was the shipped behaviour until it was found to be wrong. It ignored the very reuse the decision above states: a solid wall already blocks the player, and by the same rule should block liquid, whether the wall in question is interior or sits at the world's own edge. An ordinary room's outer wall collides by default (`ArrangementWorldData`'s `authoredCollision`) whether or not anything happens to be authored beyond it, so nothing being there is not the same as an opening. Any solid face anywhere near the edge of an authored World therefore drained its whole connected component to zero on contact, independent of liquid level. In `world-test-1.yaml`, three rooms totalling 36000 units of authored volume settled at zero because one of them had an ordinary, fully-walled 16×20 alcove that happened to touch the unbounded face on three sides.
