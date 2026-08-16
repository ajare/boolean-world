# Architecture decisions — World geometry rewrite

Replacing Clipper2 as the boolean engine of the World clip pipeline with a
single planar arrangement. Terms are defined in [../glossary.md](../glossary.md).

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

## Scope

**In:** the boolean engine behind `World`'s geometry generation — `Clipper`,
`ClipperUtils`, `ZCallback`, the `Defines.h` z-bitfield, `WorldVertexData`,
`PolygonGraph`, `ClippedPolygon`, `WorldData`, `Triangulator`, and the two
`WorldDataGenerator` implementations.

**Out:**

- **Clipper2 as a dependency.** `PathPolygon.cpp` uses it for path *offsetting*
  (`InflatePaths`), which is a different algorithm and one Clipper2 is good at.
  It stays.
- **The rest of the `floored` application.** Its arrangement prototype is in
  scope and moves into `core`; its document and UI remain otherwise untouched.
- **`Tiling`, `SquareTiling`, `InfluenceEye`** and the prefab-area code, none of
  which touch the clip pipeline.
- **The authoring model.** ADR-0001 preserves it; changing it is a separate
  decision for later.

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
