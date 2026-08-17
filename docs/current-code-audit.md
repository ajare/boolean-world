# Post-remediation code audit

**Date:** 2026-08-17  
**Commit:** `b2bc00f` (`master`)  
**Scope:** first-party C++ under `src/`, the BooleanWorld Python scripts, and project tooling.  
**Excluded:** `ext/`, `vendor/`, generated build trees, and embedded third-party sources (ImGui, ImPlot, imnodes, stb, tinyxml2, polypartition, Clipper 1, miniz internals, and StackWalker).

This is a fresh audit of the code after the findings in [`bug-audit.md`](bug-audit.md) were remediated. Findings already recorded there are intentionally not repeated.

## Method and limitations

- Read the domain glossary and the accepted geometry ADRs before reviewing the arrangement and generation paths.
- Reviewed ownership, copying, deserialization, threading, state transitions, resource loading, rendering, collision, editor mutation, launcher input, the C ABI, and Python generation scripts.
- Built every Release target successfully.
- Ran all 59 Release tests: **59 passed, 0 failed**.
- Ran targeted clang-tidy/Clang Static Analyzer checks against the MSVC compilation database and manually verified reported paths in source.
- Ran the seven checked-in performance benchmarks. They confirm the recent remediations, but there is no benchmark yet for the new optimisation candidates below.
- Ran `python tools/format.py --check`: one first-party file currently fails (`src/BooleanWorld/common/include/common/MaterialRegistry.h`).

This remains a static audit, not a proof of correctness. In particular, the interactive editor/game and Zip resource path were not exercised end-to-end.

---

## Ratings

### Criticality

| Rating | Meaning |
| --- | --- |
| **P1 — Critical** | Memory corruption, use-after-free, unbounded writes, or a normal feature that terminates the process. |
| **P2 — High** | Crashes, hangs, silent data loss, or substantially wrong output on reachable paths. |
| **P3 — Medium** | Real but narrower correctness, lifetime, or performance defects. |
| **P4 — Low** | Hygiene/tooling defects with little runtime impact. |

### Fix difficulty

| Rating | Estimate |
| --- | --- |
| **Trivial** | Minutes; local edit and a focused test. |
| **Small** | Under half a day; one component plus tests. |
| **Medium** | Roughly half a day to two days; several call sites or ownership decisions. |
| **Large** | Cross-cutting design work; multiple days. |

---

## Summary

| Priority | Count |
| --- | ---: |
| P1 — Critical | 5 |
| P2 — High | 13 |
| P3 — Medium | 8 |
| P4 — Low | 1 |
| **Total** | **27** |

### Highest-priority fixes

1. Replace the three `getRadius()` implementations that throw a `float` (CA-01).
2. Remove live `Primitive*` pointers from completed generation snapshots (CA-02).
3. Make `YamlSerializer::readVector2` enforce exactly two values (CA-03).
4. Repair `World` copy/assignment ownership and pointer rebinding (CA-04).
5. Harden the exported core DLL state machine (CA-05).

---

## Finding index

| ID | Priority | Difficulty | Area | Finding |
| --- | --- | --- | --- | --- |
| CA-01 | P1 | Trivial | core | Torus, torus segment, and superformula bounds throw a non-standard exception |
| CA-02 | P1 | Large | core/threading | Generation snapshots retain dangling `Primitive*` values |
| CA-03 | P1 | Small | core/serialization | `readVector2` can overflow or read uninitialised stack data |
| CA-04 | P1 | Large | core | `World` copy and assignment violate ownership and parent/generator lifetimes |
| CA-05 | P1 | Medium | core DLL | Exported setters can dereference a freed current primitive |
| CA-06 | P2 | Medium | launcher/resources | Zip resource locations close the archive and mismatch allocators |
| CA-07 | P2 | Small | resources | Cyclic dependencies construct `ResourceException(nullptr)` and crash |
| CA-08 | P2 | Medium | launcher | Shutdown is not safe after partial startup and destroys services before states |
| CA-09 | P2 | Medium | resources | Resource validation dereferences missing namespaces and mutates copies |
| CA-10 | P2 | Trivial | resources | Resource locations never persist their `scanned` flag |
| CA-11 | P2 | Small | core/common | Three copy operations still omit defining state |
| CA-12 | P2 | Trivial | core | Torus-segment thickness changes meaning at exactly 360 degrees |
| CA-13 | P2 | Medium | core/editor | Moving trigger-line points leaves bounds and the lookup grid stale |
| CA-14 | P3 | Trivial | AppLib | Finished screen flashes remove later active flashes |
| CA-15 | P2 | Small | launcher/input | Unrecognised mouse/key input can read out of bounds or become Escape |
| CA-16 | P2 | Small | AppLib/ECS | Entity setup copies components the prototype does not own |
| CA-17 | P2 | Medium | core/authoring | Primitive shape parameters permit hangs, huge allocations, and invalid topology |
| CA-18 | P2 | Medium | core/threading | Generation work can queue without bound |
| CA-19 | P2 | Trivial | Python tooling | Generated worlds swap orbit inputs and ignore requested world size |
| CA-20 | P3 | Small | resources | A callback changes `releaseResource` reference-count semantics |
| CA-21 | P3 | Medium | Willpower.Geometry | Mesh assignment leaks owned state; self-assignment can erase loops |
| CA-22 | P3 | Small | Willpower.Geometry | Short round/bezier operations collapse because tessellation counts reach 0 or 1 |
| CA-23 | P3 | Small | editor | Superformula parameter edits bypass undo and dirty tracking |
| CA-24 | P3 | Small | common | Repeated timer reads double-count elapsed time |
| CA-25 | P3 | Trivial | core/threading | A non-positive generation interval creates a busy loop |
| CA-26 | P3 | Trivial | common | Short convex polygons underflow an intersection loop |
| CA-27 | P4 | Trivial | tooling | The formatting check currently fails |

---

# P1 — Critical

## CA-01 — Three authored primitive types terminate during ordinary bounds calculation

**Where:**
- `src/BooleanWorld/core/src/TorusPolygon.cpp`, `TorusPolygon::getRadius`
- `src/BooleanWorld/core/src/TorusSegmentPolygon.cpp`, `TorusSegmentPolygon::getRadius`
- `src/BooleanWorld/core/src/SuperformulaPolygon.cpp`, `SuperformulaPolygon::getRadius`

All three functions contain `throw 1.0f;`, apparently where `return 1.0f;` was intended. `World::addPrimitiveToLookupGrid` calls `calculateBounds`, which calls `getRadius` unless exact bounds are enabled. Creating any of these primitives through the editor or core DLL therefore throws a `float`. Most surrounding handlers catch only `std::exception`, so this can escape the C ABI or terminate the application.

**Fix:** return a conservative radius. `1.0f` is correct for the torus outer radius. For a superformula, calculate the maximum radius represented by its generated contour (or always use exact bounds). Add tests that construct, add, copy, and serialize all three primitive types without enabling exact bounds.

---

## CA-02 — Completed generations are not actually lifetime-independent snapshots

**Where:** `DynamicWorldDataGenerator::{GenerationInput,Clipping,canCommit}`

The geometry and properties are copied before worker execution, but `GenerationInput` and `Clipping` still carry `sourcePrimitives` and `updatedPrimitives` as raw `Primitive*`. A generation can finish after the editor removes and deletes one of those primitives. `canCommit` later dereferences `updatedPrimitives` to read bounds, and debug accessors expose pointers from active/queued generations. This creates a use-after-free even though arrangement construction itself now uses copied input.

This is adjacent to ADR-0005: immutable `WorldData` is safe, but the commit metadata beside it is not.

**Fix:** make commit metadata value-based. Store copied bounds and immutable diagnostic records instead of pointers. If the visibility gate must observe the current primitive rather than the generation-time bounds, introduce a stable, generation-safe handle and validate it through `World`; current vector indices are not stable across removal. Add a regression test that starts a blocked generation, removes its updated primitive, completes the worker, and attempts a commit.

---

## CA-03 — A YAML vector can overwrite the stack or return uninitialised coordinates

**Where:** `src/BooleanWorld/core/src/YamlSerializer.cpp`, `YamlSerializer::readVector2`

The method declares `float v[2]` and writes every sequence item with `v[i++]` without an upper bound. Three values overwrite the stack; zero or one value returns uninitialised data. If parsing throws after `beginArray`, the optional-value path returns without balancing `endArray`, corrupting the serializer's node/path stacks for subsequent fields.

This is reachable from many world fields, including positions, sizes, vertices, and offsets.

**Fix:** initialise a `std::array<float, 2>`, reject any sequence whose size is not exactly two, and use an RAII scope guard for `endArray`. Add tests for 0, 1, 2, and 3 elements, both required and optional, followed by another read to verify stack recovery.

---

## CA-04 — `World` copy and assignment preserve pointers into the source and leak destination state

**Where:** `src/BooleanWorld/core/src/World.cpp`, `World::copyFrom` and `operator=`

There are three related failures:

1. Assignment does not clear the destination. It overwrites grid/generator pointers (leaking the old objects) and appends copied primitives and trigger lines to existing vectors.
2. Primitive copies retain `mParent` pointers into the source world. `copyFrom` builds a `primitiveMap` but never uses it to rebind parent links; destroying the source leaves copied child primitives dangling.
3. Copying a `DynamicWorldDataGenerator` preserves its `mWorld` pointer to the source world, not the new `World`.

Self-assignment is worse: adding to `mPrimitives` while iterating the same vector invalidates the loop.

**Fix:** implement copy construction from a clean delegated state, rebind every copied parent through `primitiveMap`, and create/rebind the generator explicitly to `this`. Implement assignment with copy-and-swap (or delete it if no valid use remains). Prefer `unique_ptr` ownership for grids, generators, primitives, and trigger lines. Test non-empty assignment, self-assignment, parent chains after source destruction, and a copied dynamic generator after source destruction.

---

## CA-05 — The exported core DLL can use a freed primitive

**Where:** `src/BooleanWorld/core/src/Module.cpp`

`gPrimitive` is not reset when `mod_destroy_world` deletes `gWorld`, nor when `mod_create_world` replaces an existing world. Any setter called before the next primitive creation dereferences freed memory. The floor and ceiling setters are particularly unsafe: they do not check `gPrimitive`, do not catch exceptions, and return `1` (failure) even after successfully changing the value. Negative `numValues` in `mod_set_primitive_animation_value` can also become a huge vector allocation.

**Fix:** clear `gPrimitive` before deleting/replacing a world; validate every pointer, enum, material index, count, and buffer argument at the ABI boundary; return `0` on successful floor/ceiling changes; and ensure no exception (including non-`std::exception`) crosses `extern "C"`. Build primitives in `unique_ptr` until `World::addPrimitive` succeeds. Add a direct DLL API test covering call-order errors.

---

# P2 — High

## CA-06 — Zip resource loading is unusable and has allocator UB

**Where:** `src/Launcher/src/ZipResourceLocation.cpp`

The constructor indexes the archive and then calls `mz_zip_reader_end`, so later extraction uses a closed archive. `hardResourceExists` checks `zip-path/member` on the host filesystem instead of `mFileEntries`. `readData` dereferences a missing map entry, reads the caller's uninitialised `*dataSize`, and returns memory from miniz's heap; `DataStream` later frees that pointer with `delete[]`, which mismatches miniz's default `malloc` allocator.

**Fix:** keep the reader open until destruction, call `mz_zip_reader_end` in the destructor, check `mFileEntries` for existence, validate size against `uint32_t`, allocate a `new[]` buffer, and use `mz_zip_reader_extract_to_mem`. A Zip integration test should scan a manifest, create a text/image resource, and destroy the manager under ASan.

---

## CA-07 — Cyclic resource dependencies crash while constructing the error

**Where:**
- `ResourceManager::sortResourcesByDependency`
- `ResourceException` in `ResourceExceptions.h`
- Launcher's `catch (ResourceException&)`

The cycle path throws `ResourceException(nullptr, ...)`, but that constructor immediately calls `resource->getType()` and `getQualifiedName()`. The intended diagnostic is replaced by a null dereference. The launcher catch also assumes `getResource()` is non-null.

**Fix:** throw `ResourceSystemException` for graph-level errors. Make `ResourceException` either require a non-null resource by type or format a safe generic message when null, and make the launcher defensive. Add cyclic and missing-dependency tests.

---

## CA-08 — Launcher teardown is ordered after the services states depend on

**Where:** `src/Launcher/src/Main.cpp`, `shutdown` and exception handlers

`shutdown` deletes the resource manager, audio system, render system, render resource manager, and window before deleting `StateManager`. `StateManager::~StateManager` calls `_exit()` on every state, so normal window-close teardown can run state exit code against already-freed services.

The same function is not safe after partial startup: a malformed config or early MPP failure reaches unconditional calls such as `gRenderSystem->destroyCoreResources()` and `gRenderSystemResourceMgr->dumpResources()`. The `ExitApplicationException` handler also divides an integer duration by `numFramesProcessed` without checking zero.

**Fix:** destroy/unwind the state manager first, while all services and the DLL are alive; then destroy renderers/resources in dependency order. Replace globals with an RAII application object or, minimally, null-guard every partial-startup step. Guard zero-frame statistics. Test failure injected after each startup phase.

---

## CA-09 — Resource validation both crashes on missing namespaces and fails to persist reordering

**Where:** `ResourceLocation::validateResourceDefinitions`

A qualified dependent resource does `mNamespaces.find(depNamesp)->second` without checking `end()`, so an unknown namespace dereferences an invalid iterator. Separately, both outer loops use `for (auto ... : ...)`; moving the default definition to the end mutates copies, not stored records. Because `Resource::parseDefinition` picks the first matching factory, an explicitly declared default can mask later specialised definitions despite validation claiming to reorder it.

Validation is also location-local, so references intended to cross resource locations are reported missing before the manager's combined registry is considered.

**Fix:** validate dependencies against the manager's merged namespace registry, report missing namespaces explicitly, and use references when mutating definitions. Add tests for missing namespaces, cross-location references, and a default definition authored before a specialised definition.

---

## CA-10 — `scanLocations` always treats every location as unscanned

**Where:** `ResourceManager::scanLocations`

The loop is `for (auto record : mLocations)`. Setting `record.scanned = true` updates only a copy. A second call rescans all manifests, overwrites records/resources, and can duplicate allocations and references.

**Fix:** iterate `for (auto& record : mLocations)`. Decide and test whether rescan should replace or reject already-instantiated resources.

---

## CA-11 — Copy operations still lose shape/spline state

**Where:**
- `RegularPolygon::copyFrom`
- `SuperformulaPolygon::copyFrom`
- `BezierSpline::copyFrom`

`RegularPolygon::copyFrom` omits `mNumSides`; its own copy constructor compensates manually, but assignment does not, and derived `CirclePolygon`/`CircleSegmentPolygon` copy constructors do not. `SuperformulaPolygon::copyFrom` omits `mResolution`. `BezierSpline::copyFrom` does not copy the base `SplinePath::mPoints`, so copied splines have no control points.

**Fix:** copy defining members in one authoritative operation (or use compiler-generated copy operations). Add copy-constructor and assignment tests that compare every public property and generated contour/control point.

---

## CA-12 — Partial and full torus segments interpret thickness oppositely

**Where:** `TorusSegmentPolygon::generateVerticesImpl`

For `arcLength == 360`, inner radius is `1 - mThickness`; for every smaller arc it is `mThickness`. A thickness of 0.1 therefore jumps from an inner radius of 0.1 at 359.99 degrees to 0.9 at 360 degrees.

**Fix:** use `1.0f - mThickness` consistently if thickness means wall thickness, matching `TorusPolygon` and the documentation. Add continuity tests around 360 degrees.

---

## CA-13 — Trigger-line edits leave spatial metadata at the old position

**Where:**
- `WorldTriggerLine::setPoint`
- editor drag code in `editor/src/Main.cpp`
- prefab copying in `Document::addPrefabInstance`

`setPoint` changes only `mPoints`; it neither calls `updateBounds` nor updates `World::mTriggerLookupGrid`. Editor rendering uses `findTriggerLines`, so a moved trigger line can disappear at its new location, remain visible at its old one, or be omitted from queries. Prefab copies are inserted with copied pre-transform bounds.

**Fix:** make trigger-line geometry mutation a `World` operation: remove the old grid entry, update both points and bounds atomically, then reinsert. Alternatively give the line a world callback matching primitive invalidation. Ensure setters mark serialization state modified. Add move and prefab tests that query only the new bounds.

---

## CA-15 — Input translation accepts indices and keys outside its tables

**Where:**
- `Launcher/src/sdl/WindowSDL.cpp`

*Resolved when the Launcher moved from GLFW to SDL3; the GLFW backend it describes no longer exists.*

The GLFW backend indexed a three-element `gButtonTranslator` with any GLFW mouse-button number, so side buttons read out of bounds, and unknown keys went through `gKeyTranslator[key]`, inserting a default `Key::Escape` so an unmapped/media key could become Escape.

The SDL backend now looks keys up with a checked `find` and ignores ones it has no mapping for, and bounds-checks the button index against `MouseButton::NUMBUTTONS` before translating. `getKeyModifiers` ORs only the individual left/right bits: it previously *added* the aggregate masks (`SDL_KMOD_SHIFT`, `CTRL`, `ALT`) on top of them, and because `KeyModifiers::Shift == LeftShift | RightShift` that carried into unrelated modifiers.

**Remaining:** add unknown-key and side-button tests.

---

## CA-16 — ECS setup assumes every prototype has every registered component

**Where:** `AppLib/src/EntityHandler.cpp`, `EntityHandler::setup`

The method iterates every registry storage and unconditionally evaluates `storage.value(protoId)`. With multiple prototypes that have different component sets, creating one reads a component it does not own and can assert or fail inside EnTT. `copyEntityComponents` already contains the correct `storage.contains(from)` guard but is not used here.

**Fix:** call `copyEntityComponents(protoId, entity->mCompSysId)` or duplicate its contains check. Add two prototypes with disjoint components and instantiate both.

---

## CA-17 — Shape parameter APIs do not enforce finite, bounded geometry

**Where:** regular/circle/circle-segment/torus/torus-segment/superformula setters, constructors, and deserializers

Examples:

- Negative resolution converted to `uint32_t` can request billions of vertices.
- Zero circle-segment or torus-segment resolution divides by zero while generating vertices.
- Zero rectangle ratio produces infinite coordinates.
- Negative superformula resolution makes `for (float a ...; a += inc)` run forever; zero produces a degenerate/non-finite contour.
- A superformula YAML `values` array with fewer than six entries commits uninitialised values.
- NaN/infinite vertices reach `llround` in `ToFixedPointCoordinate`, outside its valid contract.

Most guards are debug-only assertions, while the editor itself permits resolution 0.

**Fix:** centralise constructor/setter/deserializer validation: finite values only, at least three contour vertices, bounded resolution/count, nonzero formula denominators, and a total vertex cap before allocation. Validate all six superformula values exactly. Reject invalid authored data with `SerializationException`; do not silently clamp file data unless that policy is documented.

---

## CA-18 — Generation requests can outrun generation workers indefinitely

**Where:** `DynamicWorldDataGenerator::generate` and `handleEvents`

The completed-generation queue is bounded, but every clip event posts another full arrangement task to the executor. There is no single-flight gate or bound on queued `GenerationInput` snapshots. Repeated animation events or rapid layer/editor changes can retain many full contour/property copies and spend CPU producing generations that will be discarded.

**Fix:** allow one running generation plus one coalesced latest request. Replace older not-yet-started input with the newest snapshot; after the worker completes, run the latest pending request. Keep layer selection/generation IDs so stale results remain rejectable. Add a stress test with a deliberately blocked worker and thousands of requests, asserting bounded retained requests.

---

## CA-19 — Python world generation writes the wrong authored values

**Where:** `src/BooleanWorld/scripts/core.py` and `gen-world.py`

Both regular and rectangle helpers write `orbit_distance` into the `OrbitAngle` animator and `orbit_angle` into `OrbitDistance`. `gen-world.py:create_world` ignores its `size` argument and always calls `create_world(8192)`. The generated YAML is valid but semantically wrong, making this silent data corruption in tooling.

**Fix:** swap the two animator assignments and pass `size`. Add a small generation test that loads the output and verifies world size plus both animator values.

---

# P3 — Medium

## CA-14 — Screen-flash compaction keeps the finished element

**Where:** `AppLib/src/ScreenFxManager.cpp`, `update`

When an earlier flash expires and a later flash remains active, the intended compaction line is `mFlashes[count] = mFlashes[i]`. The implementation assigns `mFlashes[i] = mFlashes[i]`, then pops the active tail. Overlapping flashes therefore disappear early.

**Fix:** assign into `count`, preferably using `std::erase_if` plus a separate colour fold. Test two flashes with different lifetimes.

---

## CA-20 — Supplying a callback prevents release of an unloaded resource

**Where:** `ResourceManager::releaseResource`

If a callback is present and the resource is not loaded, the function emits callback states and returns before decrementing `mRefCount`. The same call without a callback decrements it. Diagnostic/progress reporting therefore changes ownership semantics and can keep resources permanently acquired.

**Fix:** perform reference-count transitions independently of callback emission; callbacks should observe state, not control it. Test loaded, created-only, and instantiated-only resources with and without callbacks.

---

## CA-21 — Geometry assignment leaks ownership and is not self-safe

**Where:**
- `Willpower.Geometry/src/Mesh.cpp`, `Mesh::operator=`/`copyFrom`
- `DirectedEdgeLoop::operator=` and `Polygon::operator=`

`Mesh::copyFrom` overwrites up to four owned attribute objects and three acceleration-grid pointers without deleting the destination's old values. Every assignment to an initialised mesh leaks. `DirectedEdgeLoop::copyFrom` clears `mEdges` before iterating `other`; on self-assignment `other` is now empty, and `Polygon` inherits the same loss.

**Fix:** use `unique_ptr` and copy-and-swap for `Mesh`; add explicit self checks or compiler-generated value copying for loop data. Test assignment into a populated object under ASan and self-assignment of a polygon with edges.

---

## CA-22 — Small curved operations produce invalid tessellation counts

**Where:**
- `SplinePath::divide`
- `MeshOperations::extrudeVertexExternal`
- `MeshOperations::chamferVertex`

Tessellation counts are derived by truncating length/scale/angle to `int`. Counts of 0 or 1 produce division by zero (`1 / (n - 1)` or `i / (numPoints - 1)`) and then cause split operations to return without creating the expected curve. Small geometry silently degenerates to a chord or empty result.

**Fix:** define a minimum sample contract (normally two endpoints plus at least one interior point for a visible curve), clamp counts before division, and make adaptive mode actually adaptive or remove the unused flag. Test sub-unit curves and small round extrusions.

---

## CA-23 — Superformula value sliders are outside the editor transaction system

**Where:** `editor/src/UI.cpp`, `renderEditSuperformulaPolygon`

Resolution uses `transactUndoableAction`, but the six value sliders call `setValue` directly. They do not create undo history or set `Document::mModified`, so a user can change geometry and close without a save prompt, and cannot undo the edit.

**Fix:** use the same begin/commit/abandon transaction pattern as rectangle edits, grouping a drag into one action. Add an editor test checking dirty state and undo restoration.

---

## CA-24 — `wp::Timer` double-counts across repeated reads

**Where:** `Willpower.Common/src/Timer.cpp`, `elapsedNanoseconds`

While running, each call adds `now - mTimeStarted` to `mDuration` but never advances `mTimeStarted`. The second read includes the first interval twice, the third includes it three times, and pause after a read adds it again. `nsToString(0)` also calls `log10(0)`.

**Fix:** make an elapsed read non-mutating (`mDuration + now - mTimeStarted`) or advance the start point whenever accumulating. Define formatting for non-positive durations. Add deterministic sleep/fake-clock tests with repeated reads and pause/resume.

---

## CA-25 — Invalid scheduling intervals spin a worker at 100% CPU

**Where:** `DynamicWorldDataGenerator::generateOnInterval` and `startGenerationSchedule`

For interval 0 or a negative value, `ceil(interval)` produces zero or fewer sleep iterations. The outer `while (true)` then continuously sets `mScheduledGenerationRequested` without sleeping. NaN is also converted to `int` without validation.

**Fix:** reject non-finite or non-positive intervals in `setScheduledGenerationInterval`/`startGenerationSchedule`. A condition-variable wait with stop notification is cleaner and makes shutdown immediate.

---

## CA-26 — Convex-polygon mesh intersection underflows on short input

**Where:** `Willpower.Common/src/BoundingConvexPolygon.cpp`, `intersectsTriMesh`

The loop condition casts the vertex count to `uint32_t` and subtracts one. Empty input becomes `UINT32_MAX`, causing out-of-bounds reads. The default constructor leaves an empty polygon, so this is representable through the public API.

**Fix:** return false when there are fewer than three vertices and validate polygons at construction. Add 0/1/2/3-vertex tests.

---

# P4 — Low

## CA-27 — The checked-in formatting gate is red

**Where:** `src/BooleanWorld/common/include/common/MaterialRegistry.h`

`python tools/format.py --check` reports one file would change.

**Fix:** run clang-format on that file and make the check part of CI if it is intended as a gate.

---

# Optimisation candidates

These are current opportunities, not repeats of the completed optimisations documented in `bug-audit.md`.

| ID | Priority | Difficulty | Opportunity |
| --- | --- | --- | --- |
| OPT-01 | High | Small | **Precompute priority order once per arrangement.** `EvaluateFold` allocates and stable-sorts all primitive indices for every face. Build one order in `BuildArrangement` and reuse it for solid evaluation and winner selection, preserving ADR-0001's stable equal-priority order. Changes `O(F · P log P)` sorting/allocations to `O(P log P + F · P)`. |
| OPT-02 | High | Medium | **Avoid the dense face × primitive winding matrix.** `vector<vector<int32_t>> windingNumbers` allocates `F·P` integers and copies a full `P`-element row at every BFS edge. Propagate a compact membership/winding state with copy-on-write rows, sparse deltas, or process connected components while retaining only needed rows. Benchmark memory and classification time on dense arrangements. |
| OPT-03 | Medium | Medium | **Use bulk-built immutable spatial grids for `ArrangementWorldData`.** Triangle, vertex, and wall grids never move/remove items, yet `AccelerationGrid::addItem` builds a reverse `map<index, cells>` and binary-inserts each index. A static builder can append naturally ordered indices and omit reverse maps entirely. |
| OPT-04 | Medium | Small | **Remove per-item temporary point vectors while building snapshot grids.** Every triangle currently constructs/grows a `vector<Vector2>` solely to make a bounding box; wall and vertex initializer lists do likewise. Add a `BoundingBox` constructor from `span`/fixed arrays or calculate min/max directly. This removes several heap allocations per arrangement item. |
| OPT-05 | Medium | Medium | **Use the trigger-line grid during per-frame collision checks.** `World::update` walks every trigger line despite maintaining `mTriggerLookupGrid`. Query the swept player segment expanded by radius, then run `checkCollide` only on candidates. This is semantically local and does not contradict ADR-0007's prohibition on culling primitives from the non-local fold. CA-13 must be fixed first so the grid stays valid. |
| OPT-06 | Medium | Medium | **Coalesce generation requests.** The correctness fix in CA-18 also prevents wasted arrangements. Keep only the latest pending snapshot while one generation runs and expose dropped/coalesced counts in generation statistics. |
| OPT-07 | Medium | Trivial | **Correct superformula sampling density.** `inc = 1 / (BaseResolution * resolution)` creates roughly `2π · BaseResolution · resolution` vertices—about 402 at resolution 1 despite `BaseResolution == 64`. Use `WP_TWOPI / (BaseResolution * resolution)` if resolution is intended to match the other primitive types. This cuts superformula generation/arrangement input by about 6.28×. |
| OPT-08 | Low | Small | **Stop copying resource registries in range loops.** Resource validation, instantiation, destruction, and queries repeatedly use `for (auto entry : map)`, copying nested maps, records, strings, and `StructuredData`. Convert read-only loops to `const&` and mutation loops to `&`; CA-09 and CA-10 already require two of these changes. |
| OPT-09 | Low | Medium | **Avoid sequential index buffers for fully duplicated triangle vertices.** The world renderer emits three unique vertices and indices `0,1,2,...` for every triangle. Either draw non-indexed or deduplicate floor/ceiling vertices by `(arrangement vertex, material, normal/height/UV)` per mesh. Measure first; walls still need some duplicated corners. |

## Recommended implementation order

1. **Immediate safety:** CA-01, CA-03, CA-05, CA-07.
2. **Snapshot/ownership:** CA-02 and CA-04, with focused lifetime tests.
3. **Normal authoring paths:** CA-11 through CA-18 and CA-23.
4. **Resource/launcher hardening:** CA-06, CA-08 through CA-10, CA-15, CA-20.
5. **Performance:** OPT-01 and CA-18/OPT-06 first; then profile before OPT-02 through OPT-05 and OPT-09.
