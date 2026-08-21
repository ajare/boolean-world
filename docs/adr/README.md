# Architecture decisions

The first ten decisions document the World geometry rewrite, which replaced
Clipper2 as its boolean engine with a single planar arrangement and later
removed the dependency entirely. Geometry terms are defined in
[../glossary.md](../glossary.md).

| # | Decision | Status |
| --- | --- | --- |
| [0001](0001-preserve-priority-ordered-fold.md) | Preserve the priority-ordered left-fold exactly | Accepted |
| [0002](0002-single-arrangement-replaces-sequential-booleans.md) | One planar arrangement replaces the sequence of boolean operations | Accepted |
| [0003](0003-exact-integer-topology.md) | Exact integer topology, fixed-point coordinates, snap-rounding | Accepted |
| [0004](0004-new-output-contract.md) | New output contract; remove `WorldVertexData` | Accepted |
| [0005](0005-immutable-worlddata-snapshot.md) | `WorldData` is an immutable snapshot with a property palette | Accepted |
| [0006](0006-step-wall-collision-threshold.md) | Step walls collide above a world-level height threshold | Accepted |
| [0007](0007-remove-culling.md) | Remove culling from generation | Accepted |
| [0008](0008-validation-by-sampled-predicate.md) | Validate by sampled predicate, not by diffing polygons | Accepted |
| [0009](0009-layer-selection-is-a-per-generation-set.md) | Layer selection is a per-generation set | Accepted |
| [0010](0010-remove-clipper2-and-path-primitives.md) | Remove Clipper2 and path primitives | Accepted |
| [0011](0011-remove-dormant-combat-framework.md) | Remove the dormant combat framework | Accepted |
| [0012](0012-world-renders-through-an-offscreen-target.md) | The world renders through an offscreen target, always composited | Accepted |
| [0013](0013-layer-is-a-first-class-owning-collection.md) | Layer is a first-class collection that owns its Primitives and WorldTriggerLines | Accepted |
| [0014](0014-primitives-are-derived-from-layerbuildsteps.md) | A Layer's Primitives are derived from its LayerBuildSteps, never stored independently | Accepted |
| [0015](0015-layerbuildsteps-declare-editing-capabilities.md) | LayerBuildSteps declare whether their Primitives may be directly edited or added to | Accepted |
| [0016](0016-meshprimitive-editing-goes-through-a-geometry-proxy.md) | MeshPrimitive editing goes through a `wp::geometry::Mesh` proxy | Superseded by ADR-0020 |
| [0017](0017-prefab-primitives-are-derived-but-excluded-from-the-build.md) | Prefab Primitives are derived Primitives excluded from the build | Accepted |
| [0018](0018-a-prefabs-pivot-is-the-origin.md) | A Prefab's pivot is the origin, not its bounds centre | Accepted |
| [0019](0019-layer-ownership-is-permanent.md) | Layer ownership is permanent — nothing moves between Layers | Accepted |
| [0020](0020-meshprimitive-stores-an-authoritative-containment-tree.md) | MeshPrimitive stores an authoritative containment tree | Accepted |

## World geometry rewrite scope

**In:** the boolean engine behind `World`'s geometry generation — `Clipper`,
`ClipperUtils`, `ZCallback`, the `Defines.h` z-bitfield, `WorldVertexData`,
`PolygonGraph`, `ClippedPolygon`, `WorldData`, `Triangulator`, and the two
`WorldDataGenerator` implementations.

**Out:**

- **The rest of the `floored` application.** Its arrangement prototype is in
  scope and moves into `core`; its document and UI remain otherwise untouched.
- **`Tiling`, `SquareTiling`, `InfluenceEye`** and the prefab-area code, none of
  which touch the clip pipeline.
- **The authoring model.** ADR-0001 preserves its priority-ordered fold.
  ADR-0010 subsequently removes the `Path` primitive together with Clipper2.

## Defects found during analysis

Recorded so none are lost in the rewrite. Most are removed structurally by the
ADRs above; the last three are incidental fixes.

| Defect | Where | Disposition |
| --- | --- | --- |
| Global vertex index truncated to 20 bits without check; `ZCallback` appends one record per intersection per pass | `Defines.h`, `ZCallback.cpp` | Removed by ADR-0004 |
| Property interpolation computes weights and discards them; the flag is never set | `ZCallback.cpp` | Removed by ADR-0004 |
| `is2Sided()` can never be true — all border polygons carry `primitiveIndex == ~0u`, so step walls are dead code | `Clipper.cpp:424`, `ClipperUtils.cpp:270`, `WorldRenderer.cpp:136-210` | Removed by ADR-0004 |
| Triangle dedup keyed on exact float centroid in a `std::map` | `Triangulator.cpp:88` | Removed by ADR-0002 — faces are disjoint |
| Graph vertex identity keyed on floats with ~2 ULP of margin at the world edge | `PolygonGraph.h` | Removed by ADR-0003 |
| Culling drops primitives from a non-local fold | `WorldDataGenerator.cpp` | Removed by ADR-0007 |
| `WorldData` deep-copied by value every frame | `DynamicWorldDataGenerator.cpp:286`, `World.cpp:1226` | Removed by ADR-0005 |
| Pinch-point/self-intersection union never implemented — bare comment, and `SplitTouchingPolygon` written but never called | `Clipper.cpp:229`, `ClipperUtils.cpp:31` | Removed with the file |
| `borderPolygons` passed for *both* the border and arrangement arguments | `DefaultWorldDataGenerator.cpp:52-53` | **Fix** — generator survives the rewrite |
| `pointInPolygon` walks every border polygon linearly, every frame | `WorldData.cpp:248` | **Fix** — grid-accelerated (ADR-0006) |
| `getNearestBorderDistance` walks every polygon and every edge, unaccelerated | `WorldData.cpp:357` | **Fix** — wall-edge grid (ADR-0006) |

## Consumers that must be ported

- `WorldRenderer::updateDataProviders` — floors, ceilings and walls
- `StatePlayBooleanWorld` — collision queries and vertex inspection
- `editor/src/UI.cpp` — inspector and debug overlays
- `editor/src/Actions.cpp:203` — mesh templates tiled into grid cells
- `World::createMeshPrimitive` / `convertPrimitivesToMesh` — baking primitives
