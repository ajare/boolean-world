# Bug audit

**Date:** 2026-08-16
**Commit:** `1183ca7` (master)
**Scope:** the project's own C++ — 537 files, ~66,000 lines across `src/BooleanWorld`
(`core`, `app`, `editor`, `common`, `profiler`, `core-dll`), `src/Willpower`, `src/AppLib`
and `src/Launcher`.

**Excluded:** `ext/` and `vendor/`, plus vendored third-party code identified by the same
explicit path list `tools/format.py` uses (ImGui, ImPlot, imnodes, StackWalker, Clipper 1)
and, additionally, tinyxml2, polypartition, miniz and `IconsFontAwesome5.h` — all of which
that list misses but which are equally third-party.

**Method:** static review. Nothing was built, run or profiled. Performance claims are
algorithmic — asymptotic cost, allocations per frame, thread-pool construction — not
measured. Where a finding depends on a path that could not be proven reachable, the finding
says so.

---

## How to read this

### Priority

| | Meaning |
| --- | --- |
| **P1 — Critical** | Memory corruption, silent data loss, or undefined behaviour on paths hit in normal use. |
| **P2 — High** | Wrong results, resource exhaustion, or crashes on reachable but less common paths. Includes the two places the arrangement rewrite did not land as the ADRs specify. |
| **P3 — Medium** | Real defects with a narrower blast radius: degenerate inputs, misleading debug output, contracts that hold only by accident. |
| **P4 — Low** | Confusing logic, redundancy, hygiene. Nothing here bites today; all of it makes the next bug harder to find. |

### Difficulty

| | Meaning |
| --- | --- |
| **Trivial** | A few lines. No design decision. Minutes. |
| **Small** | One function or file, plus a regression test. Under an hour. |
| **Medium** | Several call sites, or a decision worth recording. Half a day to a day. |
| **Large** | Cross-cutting or architectural. Multiple days, and worth planning first. |

---

## Summary

| Priority | Count |
| --- | --- |
| P1 — Critical | 10 |
| P2 — High | 23 |
| P3 — Medium | 34 |
| P4 — Low | 23 |
| **Total** | **90** |

By difficulty: 38 Trivial, 33 Small, 15 Medium, 4 Large.

### Three patterns behind most of the critical list

Fixing these as classes is cheaper than fixing them one site at a time.

1. **Hand-written `copyFrom` methods that miss members.** The codebase uses a consistent
   idiom — a copy constructor delegating to a protected `copyFrom` — and at least five
   implementations silently drop fields. Because the copy constructors carry no member
   initialiser lists, a missed member is not merely stale, it is *uninitialised*. The worst,
   `Primitive::copyFrom`, drops the entire property set (BW-02), so every clone, undo, prefab
   instance and world copy loses floor heights and materials.

2. **Unsigned index arithmetic with no size guard.** `size() - 1`, `count - 1` and bare
   `pop_back()` after a shift loop recur throughout. Several underflow into multi-billion
   iteration loops or write past the end of a vector (BW-01, BW-20, BW-34, BW-44).

3. **Ownership and threading contracts that were never written down.** The generation worker
   mutates `Primitive` objects the main thread is reading (BW-14); callbacks registered on the
   generator are never unregistered (BW-15); the pending-clipping queue has a head-of-line
   block with no bound (BW-16). ADR-0005 removed the *property* race structurally; the vertex
   race beside it is still live.

### Where the arrangement rewrite stands

The new engine reads well. The predicates in `Arrangement.cpp` are genuinely exact, the
snap-rounding is direction- and order-independent, and the half-edge cycle extraction is
textbook-correct. Two things the ADRs specify did not land:

- **ADR-0002 specifies a grid broad phase** for arrangement construction. `BuildPSLG` has
  none and is quadratic in segments with a cubic tail (BW-11). This is almost certainly the
  dominant cost in a generation.
- **`ExtractMinimalCycles` carefully preserves nested clockwise leaf boundaries**, which
  `BuildArrangement` then deletes on the next line (BW-12). The whole special case, and the
  `O(C·S²)` predicate that drives it, is dead.

---

## Index

| ID | P | Difficulty | Area | Finding |
| --- | --- | --- | --- | --- |
| BW-01 | P1 | Small | core | `Interpolator::addPoint` writes past the end of `segments` |
| BW-02 | P1 | Trivial | core | `Primitive::copyFrom` silently drops the property set |
| BW-03 | P1 | Small | core / editor | Material indices uninitialised; editor indexes an array with them |
| BW-04 | P1 | Small | willpower / editor | `set_union` writes its output into one of its own inputs |
| BW-05 | P1 | Trivial | core | `World::clear` never touches `mTriggerLines` |
| BW-06 | P1 | Trivial | core | Reloading a world frees the grids and declines to recreate them |
| BW-07 | P1 | Large | editor | Every undo level deep-copies a `World` and a thread-pool runtime |
| BW-08 | P1 | Trivial | app | `new[]` buffers released with scalar `delete` |
| BW-09 | P1 | Small | core | `Primitive::triangulate` indexes a stale vertex list |
| BW-10 | P1 | Small | core | A world file with >4 animators overflows a stack array |
| BW-11 | P2 | Medium | core | Arrangement construction has no broad phase (ADR-0002) |
| BW-12 | P2 | Medium | core | The nested-leaf cycle case is preserved then immediately deleted |
| BW-13 | P2 | Trivial | core | Unstable sorts make the priority fold non-deterministic (ADR-0001) |
| BW-14 | P2 | Large | core | The generation worker mutates primitives the main thread reads |
| BW-15 | P2 | Small | core | Generation callbacks are never unregistered, and read unlocked |
| BW-16 | P2 | Medium | core | A stale layer selection blocks the commit queue permanently |
| BW-17 | P2 | Medium | app | Vertex index bases truncated to 16 bits |
| BW-18 | P2 | Small | app | Wall normals derived from arbitrary edge direction |
| BW-19 | P2 | Trivial | core | `Interpolator::setScale` validates the wrong values |
| BW-20 | P2 | Trivial | core | `setPoints({})` pushes four billion segments |
| BW-21 | P2 | Trivial | willpower | `convexPolygonArea` uses the wrong operator |
| BW-22 | P2 | Small | core | A copied `DynamicWorldDataGenerator` has a null world |
| BW-23 | P2 | Small | willpower | `addStaticLine` can push uninitialised endpoints |
| BW-24 | P2 | Trivial | editor | `getHoveredPrimitiveIndex` dereferences before the null check |
| BW-25 | P2 | Trivial | app | A failed world load leaves `Map::mWorld` dangling |
| BW-26 | P2 | Small | core | `removePrimitive` edits the grid before confirming membership |
| BW-27 | P2 | Trivial | all | `throw e;` slices the exception (7 sites) |
| BW-28 | P2 | Trivial | core | `Serializable` is a polymorphic base with no destructor |
| BW-29 | P2 | Small | core | Four more `copyFrom` implementations drop members |
| BW-30 | P2 | Trivial | applib | `ObjectArray::acquireFreeSlot` pops from an empty vector |
| BW-31 | P2 | Medium | core | `SamplePoint` can land outside the face it samples |
| BW-32 | P2 | Medium | app | Toggling all layers silently disables every trigger line |
| BW-33 | P2 | Small | core | `Interpolator::getValue` clamps below-range to the last point |
| BW-34 | P3 | Trivial | core | `removeTransform` / `removeEvent` underflow on empty containers |
| BW-35 | P3 | Trivial | core | `checkCommitPendingClipping` reads a moved-from optional |
| BW-36 | P3 | Small | core | Deserialisation leaks everything allocated on an early return |
| BW-37 | P3 | Trivial | core | Deserialising a world silently resets `alwaysUpdateVertices` |
| BW-38 | P3 | Small | core | A world built without grids crashes on its first primitive |
| BW-39 | P3 | Medium | core | Edges with the same face on both sides pick the wrong vertex |
| BW-40 | P3 | Small | core | Face containment requires strictly nested bounding boxes |
| BW-41 | P3 | Trivial | core | Triangulation converts exact integer topology to `float` |
| BW-42 | P3 | Trivial | willpower | `normalise` returns a length; a caller tests it against zero |
| BW-43 | P3 | Trivial | willpower | `projectLine` builds a negative-size bounding box |
| BW-44 | P3 | Trivial | willpower | `pointInConvexPolygon` underflows on short input |
| BW-45 | P3 | Trivial | core | `deserialize` marks freshly loaded objects as modified |
| BW-46 | P3 | Small | core | `transformT` reads uninitialised operands for bad enum values |
| BW-47 | P3 | Small | core | Waveform operands divide by a file-supplied multiplier |
| BW-48 | P3 | Small | core | TriggerLine operands throw `out_of_range` on stale indices |
| BW-49 | P3 | Trivial | core | `setOrientation` is the only setter that does not invalidate |
| BW-50 | P3 | Medium | core | Parent chains walked per vertex with no cycle detection |
| BW-51 | P3 | Trivial | app | `printf` format mismatch on a 64-bit value |
| BW-52 | P3 | Small | app | The clipping debug panel reports statistics nothing populates |
| BW-53 | P3 | Medium | app | Player location tested at the pre-movement position |
| BW-54 | P3 | Medium | app | The player collider is owned by the sim but referenced elsewhere |
| BW-55 | P3 | Small | willpower | The physics sim iterates colliders in pointer order |
| BW-56 | P3 | Trivial | willpower | `Simulation` owns raw pointers with no copy control |
| BW-57 | P3 | Small | willpower | `AccelerationGrid::addItem` orphans an already-registered item |
| BW-58 | P3 | Trivial | willpower | `ExtendedAccelerationGrid`'s `failIfNotFound` cannot fire |
| BW-59 | P3 | Small | core | Primitive visibility is tested by edge crossings only |
| BW-60 | P3 | Small | core | `rotatedCopy` mutates polygons directly and never invalidates |
| BW-61 | P3 | Small | core | Material colours go stale when parameters are edited |
| BW-62 | P3 | Small | core | Early returns leave the serializer's map/array stack unbalanced |
| BW-63 | P3 | Small | editor | `undo(count > 1)` records the same world in every redo entry |
| BW-64 | P3 | Trivial | editor | `abandonUndoableAction` leaves the initial value behind |
| BW-65 | P3 | Trivial | app | `ReactiveCamera::setPosition` does not mark the camera dirty |
| BW-66 | P3 | Trivial | app | Animation advance loops forever on a zero-duration frame |
| BW-67 | P3 | Medium | app | A face with no winning primitive throws from the render path |
| BW-68 | P4 | Large | core | Three layers of pure forwarding wrap the same four animators |
| BW-69 | P4 | Trivial | core | `World::getWorldData` ignores both parameters |
| BW-70 | P4 | Trivial | core | Two grid-metadata accessors have identical bodies |
| BW-71 | P4 | Trivial | core / app | Debug residue left in shipping code |
| BW-72 | P4 | Trivial | core | Arrangement fields and functions nothing reads |
| BW-73 | P4 | Small | core | Statistics structures left over from the Clipper pipeline |
| BW-74 | P4 | Small | core | Layer filtering and priority sorting implemented three times |
| BW-75 | P4 | Trivial | core | `PrimitiveGroup` is unreachable and its `removePrimitive` leaks a slot |
| BW-76 | P4 | Trivial | app | Five app headers are never included |
| BW-77 | P4 | Medium | app | `WorldCollisionSim` carries a spatial grid it never uses |
| BW-78 | P4 | Small | willpower | `Vector2`'s assignment operators return by value |
| BW-79 | P4 | Trivial | willpower | Two `Vector2` / `MathsUtils` helpers are wrong and unused |
| BW-80 | P4 | Small | willpower | `MathsUtils::Epsilon` is mutable global state |
| BW-81 | P4 | Small | core | Non-standard exception constructor; case-mismatched include |
| BW-82 | P4 | Small | core | `setWorldDataGeneratorFactory` does not store a factory |
| BW-83 | P4 | Trivial | core | `instantiatePrimitive` rebuilds a map of 8 functions per call |
| BW-84 | P4 | Trivial | app / core | Vertex data copied by value in two per-frame paths |
| BW-85 | P4 | Trivial | app / editor | Ring-buffer caps off by one; overlay ignores view scale |
| BW-86 | P4 | Small | app | ImGui context and allocators set across the DLL boundary, not restored |
| BW-87 | P4 | Trivial | app | `getWDG` can return null and no caller checks |
| BW-88 | P4 | Trivial | common | Shared material tables are per-TU statics; a define is misspelled |
| BW-89 | P4 | Trivial | editor / app | An unreachable guard and a no-op pre-work step |
| BW-90 | P4 | Trivial | launcher | `dllSetArgument` accepts unknown argument names |

---

# P1 — Critical

Fix before anything else on this page.

## BW-01 — `Interpolator::addPoint` writes one element past the end of `segments`

**Priority** P1 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/include/core/Interpolator.h:463-497`

**Problem.** After the two `push_back`s, `points` holds `n+1` entries and `segments` holds `n`.
The insertion-point search runs `i` from `0` to `n-1`, so appending a point later in time than
every existing one leaves `i == n`. `_setPointClamped(points, n, …)` is in range; the very next
line, `segments[i].easing = Easing::Linear`, is not. Adding an animation keyframe at the end of
a curve — an everyday editor action — writes into the heap past the vector.

**Fix.** Clamp the write, or restructure so `segments` is always sized `points.size() - 1`:

```cpp
_setPointClamped(mCurStructure, i, time, value);
if (i < mCurStructure.segments.size()) {
  mCurStructure.segments[i].easing = Easing::Linear;
} else {
  mCurStructure.segments.back().easing = Easing::Linear;
}
```

Add a unit test that appends at the end, inserts in the middle, and inserts at index 0, and
asserts `segments.size() == points.size() - 1` after each.

---

## BW-02 — `Primitive::copyFrom` silently drops the entire property set

**Priority** P1 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/Primitive.cpp:228-245`

**Problem.** `copyFrom` copies flags, layer, operation, fill rule, priority, size, bounds,
vertices and polygons — but never `mProperties`. Since `Primitive(Primitive const&)` has no
member initialiser list, the copy's floor and ceiling heights and all three material
definitions are left default-constructed, and the three `uint32_t` material indices are left
*uninitialised* (see BW-03).

Every path that copies a primitive is affected: `World::copyFrom`, `Primitive::rotatedCopy`,
editor clone, prefab instancing, and — because the editor stores worlds by value in its undo
stack — every undo and redo. This is user-visible data loss.

**Fix.** One line in `copyFrom`:

```cpp
mProperties = other.mProperties;
```

Then work BW-29, which is the same class of omission in four more places.

---

## BW-03 — Material indices are never initialised, and the editor indexes a fixed array with them

**Priority** P1 · **Difficulty** Small · **Area** core / editor

**Where:**
- `src/BooleanWorld/core/include/core/PrimitivePropertySet.h:453-460`
- `src/BooleanWorld/editor/src/Actions.cpp:90-113`

**Problem.** `PrimitivePropertySet` gives `floorZ` and `ceilingZ` default member initialisers but
leaves `floorMaterialIndex`, `ceilingMaterialIndex` and `wallMaterialIndex` without one.
`Primitive`'s constructor does not mention `mProperties`, so it is default-initialised and those
three fields hold garbage.

`setPrimitiveDefaultMaterials` then does `bw::common::MaterialNames[properties.floorMaterialIndex]`
on a two-element `std::array` with unchecked `operator[]`. Creating a primitive from the ghost
reads out of bounds; combined with BW-02 it does so on every creation.

**Fix.** Three changes:

```cpp
// PrimitivePropertySet.h
uint32_t floorMaterialIndex{0};
uint32_t ceilingMaterialIndex{0};
uint32_t wallMaterialIndex{0};
```

```cpp
// Primitive.cpp — add mProperties to the constructor's initialiser list
: mWorld(nullptr), …, mProperties{}, mFrameNumber(0), mPolygons(complexPolygons)
```

```cpp
// Actions.cpp
void setPrimitiveDefaultMaterial(uint32_t materialIndex, bw::core::MaterialDefinitionData* def) {
  if (materialIndex >= bw::common::MaterialNames.size()) {
    return;  // or throw EditorException
  }
  …
}
```

---

## BW-04 — `std::set_union` writes its output into one of its own input ranges

**Priority** P1 · **Difficulty** Small · **Area** willpower / editor

**Where:**
- `src/Willpower/willpower.common/src/AccelerationGrid.cpp:249`
- `src/Willpower/willpower.common/include/willpower/common/ExtendedAccelerationGrid.h:752`
- `src/BooleanWorld/editor/src/Document.cpp:108-127` (both `addSelectedPrimitiveIndices` and
  `removeSelectedPrimitiveIndices`)

**Problem.** Four sites call `set_union` (or `set_difference`) with `inserter(indices, …)` where
`indices` is simultaneously passed as an input range. The algorithm requires the output range
to be disjoint from both inputs; inserting into a `std::set` while iterating it is undefined and,
in practice, produces wrong results or a crash as the tree rebalances. Both acceleration grids
do this on every spatial query, so it is on the per-frame path.

**Fix.** The union cases collapse to a plain insert:

```cpp
// AccelerationGrid::_getItemsInCellRange and the Extended equivalent
indices.insert(cell.begin(), cell.end());
```

```cpp
// Document::addSelectedPrimitiveIndices
mSelectedPrimitiveIndices.insert(indices.begin(), indices.end());

// Document::removeSelectedPrimitiveIndices
for (auto i : indices) { mSelectedPrimitiveIndices.erase(i); }
```

While in the grid, note that a per-cell `std::set` is the wrong container — see PERF-3.

---

## BW-05 — `World::clear` never touches `mTriggerLines`

**Priority** P1 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/World.cpp:473-493`

**Problem.** `clear()` deletes the data generator, both acceleration grids and every primitive,
then clears `mPrimitives` — and leaves `mTriggerLines` entirely alone. Since `~World` is just
`clear()`, every `WorldTriggerLine` in every world ever constructed leaks. Worse, after `clear()`
the vector still holds live pointers while `mTriggerLookupGrid` is null, so a subsequent
`addTriggerLine` dereferences null and any surviving id is stale.

**Fix.**

```cpp
for (auto triggerLine : mTriggerLines) {
  delete triggerLine;
}
mTriggerLines.clear();
```

Longer term, hold both collections as `std::vector<std::unique_ptr<T>>` so the destructor
cannot drift out of sync again.

---

## BW-06 — Reloading a world deletes the acceleration grids and then declines to recreate them

**Priority** P1 · **Difficulty** Trivial · **Area** core

**Where:**
- `src/BooleanWorld/core/src/World.cpp:373-386` (`deserializeImpl`)
- `src/BooleanWorld/core/src/World.cpp:446-467` (`createAccelerationGrids`)

**Problem.** `deserializeImpl` does `delete mPrimitiveLookupGrid; delete mTriggerLookupGrid;
createAccelerationGrids(size);` without nulling either pointer. `createAccelerationGrids` guards
both allocations with `if (!mPrimitiveLookupGrid)` / `if (!mTriggerLookupGrid)`, which are false
for a dangling non-null pointer — so nothing is recreated and both members point at freed
memory. The very next thing deserialisation does is call `addTriggerLine` and `addPrimitive`,
both of which write through those pointers.

The game happens to dodge this because `Map::loadWorldFromYaml` constructs a fresh `World` each
time. Deserialising twice into one `World` corrupts the heap.

**Fix.**

```cpp
delete mPrimitiveLookupGrid;
mPrimitiveLookupGrid = nullptr;
delete mTriggerLookupGrid;
mTriggerLookupGrid = nullptr;
createAccelerationGrids(workData.accelGridSize);
```

Better: make `createAccelerationGrids` unconditionally replace, and hold the grids in
`unique_ptr`.

---

## BW-07 — Every undo level deep-copies a `World`, and with it a concurrencpp thread-pool runtime

**Priority** P1 · **Difficulty** Large (Trivial for the per-frame half) · **Area** editor

**Where:**
- `src/BooleanWorld/editor/src/Undo.cpp:460-463` (`UndoData`)
- `src/BooleanWorld/editor/src/Undo.cpp:618-631` (`getActionHistory`)
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:179-183` (copy constructor)

**Problem.** `UndoData` stores `bw::core::World` **by value**. Copying a world copies every
primitive and calls `mDataGenerator->copy()`, which news a `DynamicWorldDataGenerator` whose
`concurrencpp::runtime` member is default-constructed — a fresh thread pool. With
`MAX_STACK_SIZE` 20 and a redo stack alongside it, the editor can hold forty-odd live runtimes.

`getActionHistory()` compounds it: `for (auto item : gUndoStack)` copies each entry **by value**,
cloning every stored world and spinning up a runtime per entry, every time the history panel is
drawn.

**Fix.** Two independent changes.

*(a) Trivial, removes the per-frame cost:*

```cpp
for (auto const& item : gUndoStack) { … }
ranges::reverse_view redoStackRev{gRedoStack};
for (auto const& item : redoStackRev) { … }
```

*(b) Large, removes the root cause:* stop snapshotting the live `World`. Either store the
serialised YAML per undo level (the serializer already exists and round-trips), or separate the
data generator from `World` so a snapshot is pure geometry data. At minimum, `World::copyFrom`
must not clone a generator that owns a thread pool — see BW-22, which is the same object from
the other side.

---

## BW-08 — Array buffers allocated with `new[]` are released with scalar `delete`

**Priority** P1 · **Difficulty** Trivial · **Area** app

**Where:**
- `src/BooleanWorld/app/src/WorldTriangle3dDataProvider.cpp:293-298` (destructor)
- `src/BooleanWorld/app/src/WorldTriangle3dDataProvider.cpp:390-409` (`updateInternals`)

**Problem.** `updateInternals` allocates with `new int8_t[…]` and `new uint16_t[…]` and correctly
releases with `delete[]`. The destructor releases the same two buffers with scalar `delete`.
Mismatched forms are undefined behaviour and on the MSVC heap will typically corrupt or assert
on shutdown.

**Fix.** Use `delete[]` in the destructor. Better, since the class already tracks its own sizes,
replace both raw buffers with `std::vector<std::byte>` and `std::vector<uint16_t>` and drop the
manual memory management entirely — that also removes the unchecked pointer bumping in
`nextVertexPtr` / `addTriangle`.

---

## BW-09 — `Primitive::triangulate` accumulates its input and then indexes a stale vertex list

**Priority** P1 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/Primitive.cpp:708-734`

**Problem.** `triangulationData` is declared outside the loop over complex polygons and never
cleared, so iteration *k* triangulates polygons 1..*k* again. `vertices`, however, is rebuilt per
iteration and holds only the current complex polygon. Earcut returns indices into the
concatenation of everything in `triangulationData`, so from the second iteration onward
`vertices[ti]` reads past the end.

On top of that, `result.merge` merges the triangulator's cumulative output each pass, so triangle
count grows quadratically. Latent for single-polygon primitives; live for `MeshPrimitive`, which
is exactly what baking and grid-clipping produce.

**Fix.** Clear the accumulator at the top of each complex-polygon iteration:

```cpp
for (auto const& complexPolygon : complexPolygons) {
  ClosedPolygon vertices;
  vector<TriangulationData> triangulationData;   // moved inside the loop
  …
}
```

Add a test that triangulates a mesh primitive with two or more complex polygons. Note also
`Triangulator::_triangulate` uses `i / 3` as its acceleration-grid item id, which collides across
calls and does not match `mTriangulation.tris` indices when duplicate removal is on.

---

## BW-10 — A world file with more than four animator entries overflows a stack array

**Priority** P1 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/VertexTransformer.cpp:310-357`

**Problem.** `deserializeImpl` declares `AnimatedProperty animators[(int)Key::COUNT]` and a
parallel `string interpolators[COUNT]`, then drives them from
`while (serializer->nextArrayItem()) { … animators[i] … i++; }` with no bound on `i`. A file with
five or more entries in the `animators` array writes past both arrays on the stack.

The same loop under-runs quietly in the other direction: a file with fewer than four entries
commits default-constructed `AnimatedProperty` objects over the key-specific interpolator ranges
set up in the constructor, so `Scale` silently loses its 1..10 range and reverts to the generic
0..1 default.

**Fix.**

```cpp
int i = 0;
while (serializer->nextArrayItem()) {
  if (i >= (int)Key::COUNT) {
    addDeserializationError("Too many animators in VertexTransformer");
    return false;
  }
  …
  i++;
}
if (i != (int)Key::COUNT) {
  addDeserializationError("Expected 4 animators in VertexTransformer");
  return false;
}
```

---

# P2 — High

## BW-11 — Arrangement construction has no broad phase, contrary to ADR-0002

**Priority** P2 · **Difficulty** Medium · **Area** core

**Where:** `src/BooleanWorld/core/src/Arrangement.cpp:407-501` (`BuildPSLG`)

**Problem.** ADR-0002 specifies "a grid broad phase — `wp::AccelerationGrid` already exists — plus
exact integer pair intersection". Only the second half was built.

`BuildPSLG` tests every segment pair (`O(S²)`), and the snap re-check loop that follows tests
every candidate point against every segment — `O(C·S)`, where `C` is itself `O(S²)` in the worst
case, giving a cubic tail. At the documented target of ~123 primitives × up to 1024 vertices,
`S` reaches six figures and `S²` reaches ten.

**Fix.** Build the promised grid over segment bounding boxes and iterate candidate pairs
per cell:

1. Compute the bounding box of every segment, insert into a `wp::AccelerationGrid` sized from
   the world extents.
2. Replace the `i`/`j` double loop with: for each cell, test the pairs registered in it,
   deduplicating pairs that share more than one cell (a `set<pair<int,int>>` or a per-`i`
   visited-marker array).
3. For the re-check pass, insert the snapped candidate points into the same grid and test each
   only against segments in its cell and the eight neighbours (snapping moves a point by at most
   half a quantum, so one cell of slack is ample).

Contained entirely within `BuildPSLG`; nothing downstream needs to know. Benchmark against
`gen-3.yaml` and `stress-test-1` before and after.

---

## BW-12 — The nested-leaf cycle case is preserved and then immediately deleted

**Priority** P2 · **Difficulty** Medium · **Area** core

**Where:**
- `src/BooleanWorld/core/src/Arrangement.cpp:377-404` (`IsLeafSolidBoundaryInsideSolid`)
- `src/BooleanWorld/core/src/Arrangement.cpp:572-582` (`ExtractMinimalCycles`)
- `src/BooleanWorld/core/src/Arrangement.cpp:657-660` (`BuildArrangement`)

**Problem.** `ExtractMinimalCycles` keeps clockwise cycles when `IsLeafSolidBoundaryInsideSolid`
says so, with a comment explaining that a nested leaf solid is the exception where a clockwise
boundary separates two bounded faces. `BuildArrangement` then runs

```cpp
erase_if(cycles, [](Cycle const& cycle) { return cycle.area <= 0; });
```

on the next line, which removes exactly those cycles and nothing else. So the special case never
reaches the hierarchy builder, and `IsLeafSolidBoundaryInsideSolid` — itself `O(C·S²)` over
source contours — is pure cost.

**Fix.** Decide which behaviour is correct, and record it.

- *If the nested-leaf handling is needed:* drop the `erase_if` and teach
  `BuildPolygonHierarchy` and `assignCycleSide` to handle negative-area cycles (the sign
  determines which side of each edge the face sits on).
- *If it is not:* delete `IsLeafSolidBoundaryInsideSolid`, `GetContourBounds`, `EqualBox` and
  `PSLG::sourceContours`, and move the `area > 0` filter into `ExtractMinimalCycles` where the
  comment already lives.

Either way, add a degeneracy test with a solid disc nested inside a solid annulus — ADR-0008
step 1 calls for exactly this suite.

---

## BW-13 — Unstable sorts make the priority-ordered fold non-deterministic

**Priority** P2 · **Difficulty** Trivial · **Area** core

**Where:**
- `src/BooleanWorld/core/src/World.cpp:1174-1184` (`sortPrimitiveIndicesByPriority`)
- `src/BooleanWorld/core/src/World.cpp:949-955` (`getPrimitivesByPriority`)

**Problem.** ADR-0001 fixes the evaluation model as a left fold in priority order, which requires
a total order — equal priorities must break ties by list position.
`ArrangementWorldDataGenerator::generate` and `DefaultWorldDataGenerator::generate` correctly use
`stable_sort`. These two do not: one uses `std::sort`, the other `partial_sort_copy`, neither of
which is stable.

`createMeshPrimitive(indices)` goes through the unstable one first, so baking a selection that
contains equal priorities can produce a different solid region run to run.

**Fix.** `stable_sort` in `sortPrimitiveIndicesByPriority`; replace the `partial_sort_copy` in
`getPrimitivesByPriority` with a copy plus `stable_sort`. While there, hoist the comparator —
there are currently four separate priority comparators (`World::SortPrimitivesByPriority`,
`WorldDataGenerator::SortPrimitivesByPriority`, and two lambdas).

---

## BW-14 — The generation worker mutates primitives the main thread is reading

**Priority** P2 · **Difficulty** Large · **Area** core

**Where:**
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:271-302` (`preparePrimitives`)
- `src/BooleanWorld/core/src/ArrangementWorldDataGenerator.cpp:28-42` (`SnapshotPrimitives`)
- `src/BooleanWorld/core/src/World.cpp:1136-1168` (`World::update`)

**Problem.** `generateWorldData` runs on a concurrencpp pool thread. It copies the primitive
*pointer* list under `mGenMutex`, then releases the lock and calls `preparePrimitives`, which
invokes `primitive->updateVertexPositions()` — a write into the primitive's vertex arrays.
`SnapshotPrimitives` then reads `getVertices()` from the same objects.

Meanwhile `World::update` on the main thread calls `updateTime`, `setInputs`,
`calculateAnimationValues` and, when `mAlwaysUpdateVertices` is set, `updateVertexPositions` on
those same primitives every frame.

ADR-0005 removed the property race structurally; this one is still live. ADR-0007 explicitly
retains `preparePrimitives`, so this is not a design question — it is the retained design missing
its synchronisation.

**Fix.** Move the snapshot to the main thread and hand the worker plain data, which is what
ADR-0005's palette argument already implies:

1. On the main thread, under the lock, build `vector<ArrangementPrimitive>` directly — contours,
   operation, fill rule, priority, index, properties. This is the only step that touches live
   `Primitive` objects.
2. Pass that vector to the worker by value/move. `BuildArrangement` already takes exactly this
   type, so the worker needs no `Primitive` access at all.
3. `preparePrimitives`' vertex regeneration for non-visible primitives has to move with it, or
   operate on a private copy of the geometry.

This also removes `ArrangementWorldDataGenerator`'s dependency on `World`.

---

## BW-15 — Generation callbacks are never unregistered and are read without the lock

**Priority** P2 · **Difficulty** Small · **Area** core

**Where:**
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:266-269` (`registerGenerationCallback`)
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:363-367` (`fireCallbacks`)
- `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:358-361` (registration site)

**Problem.** Three defects in one mechanism:

1. `registerGenerationCallback` appends under `mGenMutex`; `fireCallbacks` iterates `mCallbacks`
   from the worker thread with no lock — a data race on the vector.
2. There is no unregister API. `StatePlayBooleanWorld::setup` registers a callback bound to
   `this`; `destroyGameObjects` only stops the schedule. The generator is owned by the `World`,
   which outlives the state, so any in-flight generation completing after teardown calls into
   freed memory.
3. `fireCallbacks` is invoked from `checkCommitPendingClipping` *while* `mGenMutex` is held, so a
   callback that reaches back into `getActiveClippingPrimitives()` self-deadlocks on a
   non-recursive mutex.

**Fix.** Return a token from `registerGenerationCallback` and add
`unregisterGenerationCallback(token)`; call it from `StatePlayBooleanWorld::destroyGameObjects`.
Copy under the lock and fire outside it, which fixes the race and the deadlock together:

```cpp
void DynamicWorldDataGenerator::fireCallbacks(GenerationDetails const& details) {
  std::vector<GenerationCompleteCallback> callbacks;
  {
    lock_guard<mutex> lock(mGenMutex);
    callbacks = mCallbacks;
  }
  for (auto const& callback : callbacks) {
    callback(details);
  }
}
```

and hoist the `fireCallbacks` call in `checkCommitPendingClipping` out of the `lock_guard` scope.

---

## BW-16 — A stale layer selection blocks the commit queue permanently, and the queue is unbounded

**Priority** P2 · **Difficulty** Medium · **Area** core

**Where:**
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:369-418` (`canCommit`,
  `checkCommitPendingClipping`)
- `src/BooleanWorld/core/include/core/ThreadsafeQueue.h:42-45` (`can_pop`)
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:159` (`MAX_NUM_PENDING_CLIPPINGS`)

**Problem.** `canCommit` returns false when a clipping's layer selection differs from the current
one. `ThreadSafeQueue::can_pop` only tests `queue_.front()`. So a generation that was in flight
when the layer set changed sits at the head of the queue and is never popped — and because it is
never popped, nothing behind it is either. Committing stops for the rest of the session.

`MAX_NUM_PENDING_CLIPPINGS` is `#define`d at the top of the file and never referenced, so the
queue grows without bound, each entry holding a full `ArrangementWorldData` with two acceleration
grids.

**Fix.** Drop rather than block. Add a `pop_if` / `drop_while` to `ThreadSafeQueue`, or handle it
in the caller:

```cpp
void DynamicWorldDataGenerator::checkCommitPendingClipping() {
  // Discard clippings whose layer selection is no longer current.
  while (mPendingClippings.can_pop([this](Clipping const& c) {
           return c.layerSelection != getLayerSelection();
         })) {
    mPendingClippings.pop();
  }
  … existing commit logic …
}
```

Then enforce the bound the macro was clearly meant to enforce: in `generateWorldData`, before
pushing, discard the oldest entry when the queue is at `MAX_NUM_PENDING_CLIPPINGS`.

See also BW-35 — a use-after-move on `clipping->id` sits three lines below this code.

---

## BW-17 — Vertex index bases are truncated to 16 bits while indices are 16-bit

**Priority** P2 · **Difficulty** Medium · **Area** app

**Where:**
- `src/BooleanWorld/app/src/WorldRenderer.cpp:88-91, 105-107, 127-138`
- `src/BooleanWorld/app/src/WorldTriangle3dDataProvider.cpp:382-384` (`getIndexWidth`)

**Problem.** `updateDataProviders` computes each triangle's base vertex index as
`uint16_t(dataProvider->getNumIndices(meshIndex))`. The provider reports 16-bit indices, so past
65,535 indices — about 21,845 triangles in a single material mesh — the cast wraps silently and
subsequent triangles index the wrong vertices. Nothing checks or reports it; the failure mode is
scrambled geometry.

A world folds ~123 primitives into an arrangement whose every solid face is triangulated twice
(floor and ceiling) plus two triangles per wall, so this is within reach on a dense world.

**Fix.** Preferred: move the provider to 32-bit indices — `getIndexWidth()` returns 32, allocate
`uint32_t`, widen `addTriangle`'s parameters and `_workIndex`. Check that the MPP mesh path
accepts 32-bit index buffers first.

Minimum: assert on overflow so the failure is loud rather than silent.

Separately, the base is derived from the *index* count and only happens to equal the vertex count
because every triangle emits three unique vertices. Track a per-mesh vertex count explicitly
(`meshData.numVertices` already exists) rather than inferring it.

---

## BW-18 — Wall normals are derived from arbitrary edge direction

**Priority** P2 · **Difficulty** Small · **Area** app

**Where:** `src/BooleanWorld/app/src/WorldRenderer.cpp:110-139`

**Problem.** Wall quads take their normal from `(v1 - v0).normalisedCopy().perpendicular()`, where
`v0` and `v1` come from `ArrangementEdge::v[0..1]`. Those endpoints are stored in
sorted-vertex-index order by `BuildPSLG` — which has nothing to do with which incident face is
solid. So roughly half of all border walls get a normal pointing into the solid instead of out
of it: inverted lighting, and invisible walls under backface culling.

**Fix.** The arrangement already carries the answer. `edge.face[0]` is the left face relative to
`v[0] -> v[1]`, so:

```cpp
auto const& face0 = worldData.faces[edge.face[0]];
// For a border wall, emit with the empty side in front.
bool flip = face0.solid;          // left face is the solid one -> normal must point right
auto normal = (v1 - v0).normalisedCopy().perpendicular();
if (flip) { normal = -normal; std::swap(v0, v1); }
```

`ArrangementWallKind` already distinguishes `Border`, `FloorStep` and `CeilingStep`; step walls
should face the lower (floor) or higher (ceiling) side respectively. Verify visually — ADR-0001
notes wall output must be reviewed by eye, not diffed.

---

## BW-19 — `Interpolator::setScale` validates the values it is replacing, not the ones it is given

**Priority** P2 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/include/core/Interpolator.h:390-401`

**Problem.** Both guards read `mScale` — the current member — and then the parameters are assigned
unchecked. The "time scale cannot go below zero" and "cannot run backwards" invariants are
therefore never enforced on any new value. Because the default `mScale` is `{0,0}..{1,1}`, the
first call always passes and can install anything, including an inverted or degenerate range that
later divides by zero in `render()`.

**Fix.**

```cpp
void setScale(wp::Vector2 const& scaleMin, wp::Vector2 const& scaleMax) {
  if (scaleMin.x < 0.0f) {
    throw CoreException("Interpolator time scale cannot go below zero");
  }
  if (scaleMin.x >= scaleMax.x) {
    throw CoreException("Interpolator time scale cannot run backwards");
  }
  mScale[0] = scaleMin;
  mScale[1] = scaleMax;
}
```

(The existing message also has a duplicated "cannot cannot".)

---

## BW-20 — `setPoints` with an empty list pushes four billion segments

**Priority** P2 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/include/core/Interpolator.h:408-428`, and
`getNumSegments` at `:451-453`

**Problem.** After resizing `points` to `numPoints`, the segment loop is
`for (uint32_t i = 0; i < numPoints - 1; ++i)`. With `numPoints == 0` that is `0xFFFFFFFF`
iterations of `push_back` — an out-of-memory hang. Reachable through
`setAnimationValues(key, {})` and `setInfluenceValues(key, {})`, both public and both exposed by
the editor.

`getNumSegments()` has the same underflow: `getNumPoints() - 1` on an empty interpolator returns
`4294967295`, which makes every `assert(index < getNumSegments())` in the class vacuously true.

**Fix.**

```cpp
if (numPoints < 2) {
  throw CoreException("An interpolator needs at least two points");
}
…
uint32_t getNumSegments() const {
  auto n = getNumPoints();
  return n > 0 ? n - 1 : 0;
}
```

The two-point minimum is the same invariant `removePoint` already enforces from the other side.

---

## BW-21 — `MathsUtils::convexPolygonArea` uses the wrong operator and returns nonsense

**Priority** P2 · **Difficulty** Trivial · **Area** willpower

**Where:** `src/Willpower/willpower.common/src/MathsUtils.cpp:229-240`

**Problem.** The body is `area += (v[j].x + v[i].x) - (v[j].y - v[i].y);`. The shoelace formula
needs a product: `(v[j].x + v[i].x) * (v[j].y - v[i].y)`. As written the function sums coordinates
and returns a value with no geometric meaning — it is not off by a constant, it is unrelated to
area. It is currently unreferenced outside `MathsUtils`, which is the only reason it has not
caused a visible failure.

**Fix.** Change `-` to `*`, and note the result is twice the signed area, so the return needs a
`* 0.5f` alongside the existing sign flip. Add a test against a unit square (area 1) and a unit
right triangle (area 0.5). Deleting the function is also defensible — see BW-79.

---

## BW-22 — A copied `DynamicWorldDataGenerator` has a null world and will dereference it

**Priority** P2 · **Difficulty** Small · **Area** core

**Where:**
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:179-208` (copy ctor, `operator=`, `copy`)
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:479-489` (`handleEvents`,
  `handleLayerSelectionChanged`)
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:191-204` (`copyFrom`, never called)

**Problem.** The copy constructor explicitly sets `mWorld(nullptr)` and copies only the world-data
pointer, ignoring `mAlwaysUpdateVertices` and `mAllowCommitIfVisible`. `copy()` returns such an
object, and `World::copyFrom` calls `copy()` — so every copied world gets a generator with no
world.

`handleEvents` (fired from `update` whenever a primitive raises `BW_PRIMITIVE_GLOBAL_EVENT_CLIP`)
and `handleLayerSelectionChanged` both call the no-argument `generate()`, which posts
`generateWorldData(mWorld)` with that null pointer. The editor's undo path produces exactly this
object.

The `copyFrom` member that would have copied the settings correctly is defined and never called.

**Fix.** Route the copy operations through the existing `copyFrom`, and decide deliberately
whether `mWorld` carries over (it should — the copy is a copy of the same world's generator):

```cpp
DynamicWorldDataGenerator::DynamicWorldDataGenerator(DynamicWorldDataGenerator const& other) {
  copyFrom(other);
}
DynamicWorldDataGenerator& DynamicWorldDataGenerator::operator=(DynamicWorldDataGenerator const& other) {
  if (this != &other) { copyFrom(other); }
  return *this;
}
```

Guard the no-argument `generate()` against a null world regardless. See BW-07 for why copying
this object at all is the deeper problem.

---

## BW-23 — `Simulation::addStaticLine` can push a line with uninitialised endpoints

**Priority** P2 · **Difficulty** Small · **Area** willpower

**Where:** `src/Willpower/willpower.collide/src/Simulation.cpp:142-201`

**Problem.** Inside the `Intersecting` case, `startPoint` and `endPoint` are assigned by a nested
switch on `hit1.getFlags()` with cases for `None`, `HitEnters` and `HitExits` only, and the inner
`None` branch switches again on `hit2` with two cases. Any other flag combination falls through
both switches with both points still uninitialised — and `intersecting = true` is set
unconditionally afterwards, so the garbage is pushed into `mStaticLines` and registered in the
grid.

The outer switch likewise has no `default`, silently dropping `Touching` and `Coincident` results.

**Fix.**

```cpp
Vector2 startPoint = linev0, endPoint = linev1;   // safe defaults
bool intersecting = false;
switch (intersect) {
  case …::Intersecting:
    switch (hit1.getFlags()) {
      …
      default: break;          // keep the whole-segment default
    }
    intersecting = true;
    break;
  …
  default:
    intersecting = false;
    break;
}
```

Decide explicitly what `Touching` and `Coincident` should do — a coincident line probably *should*
be added.

---

## BW-24 — `getHoveredPrimitiveIndex` dereferences the world before checking whether one exists

**Priority** P2 · **Difficulty** Trivial · **Area** editor

**Where:** `src/BooleanWorld/editor/src/Document.cpp:135-169`

**Problem.** Both overloads guard the *return* with `isActive() ? … : ~0u`, but the loop that
builds the ignore set runs first and unconditionally calls `mWorld->getNumPrimitives()`. With no
document open and `settings.renderAnimatedPrimitives` false — a persisted setting — hovering the
viewport dereferences a null `shared_ptr`.

**Fix.** Early-return at the top of both:

```cpp
if (!isActive()) {
  return ~0u;   // or {} for the vector overload
}
```

The two overloads are otherwise identical apart from the call they end in; fold them together
while you are here.

---

## BW-25 — A failed world load leaves `Map::mWorld` dangling

**Priority** P2 · **Difficulty** Trivial · **Area** app

**Where:** `src/BooleanWorld/app/src/Map.cpp:695-705`

**Problem.** `loadWorldFromYaml` starts with `delete mWorld;` and only assigns a new one after
`ser->deserialize()`. If that throws — a malformed YAML file is enough — `mWorld` holds a freed
pointer, and `~Map` deletes it again.

**Fix.** `delete mWorld; mWorld = nullptr;` before the parse. Better, hold it as
`std::unique_ptr<bw::core::World>` and `reset()`.

---

## BW-26 — `removePrimitive` edits the acceleration grid before confirming the primitive is in the world

**Priority** P2 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/World.cpp:643-670`

**Problem.** The first statement removes `primitive->getId()` from `mPrimitiveLookupGrid`. Only
afterwards does the function search `mPrimitives`, and with `failIfNotFound == false` it can
return having silently evicted whichever primitive currently owns that id from the grid —
leaving a live primitive absent from every spatial query.

**Fix.** Find the primitive first; touch the grid only once membership is confirmed. The
compaction loop below is also O(n) grid remove/add pairs; consider marking-and-compacting in one
pass and rebuilding the affected grid entries at the end.

---

## BW-27 — Catch-and-rethrow by value slices the exception

**Priority** P2 · **Difficulty** Trivial · **Area** all

**Where:**
- `src/BooleanWorld/core/src/World.cpp:720` (`replacePrimitive`)
- `src/BooleanWorld/core/src/World.cpp:813` (`replaceTriggerLine`)
- `src/Willpower/willpower.common/include/willpower/common/ExtendedAccelerationGrid.h:471`
- `src/Willpower/willpower.geometry/src/Mesh.cpp:446`, `:1694`
- `src/Willpower/willpower.geometry/src/MeshOperations.cpp:1204`, `:1980`

**Problem.** Seven sites do `catch (exception& e) { … throw e; }`. That copy-constructs a plain
`std::exception`, discarding the derived type and, on MSVC, the message — so a
`CoreException("…")` propagates out as a bare "Unknown exception". The diagnostics the codebase
carefully builds are thrown away exactly where they are most needed.

**Fix.** `throw;`. In `ExtendedAccelerationGrid` the entire try/catch is dead anyway — see BW-58.

---

## BW-28 — `Serializable` is a polymorphic base with no destructor at all

**Priority** P2 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/include/core/Serializable.h:846-893`

**Problem.** The class declares five virtual functions and no destructor, so it gets an implicit
non-virtual one. Every serialisable type in the project derives from it — `World`, `Primitive`,
`VertexTransformer`, `Interpolator`, `MaterialDefinition`, `WorldTriggerLine` — and several own
heap memory. Deleting any of them through a `Serializable*` is undefined behaviour. Nothing does
today, which makes this a trap rather than an active bug, but the class is a public base and the
guard costs one line.

**Fix.** `virtual ~Serializable() = default;`. Note `Interpolator` already declares
`virtual ~Interpolator() = default`, which is correct only by accident given its base.

---

## BW-29 — Four more `copyFrom` implementations drop members that have no other initialiser

**Priority** P2 · **Difficulty** Small · **Area** core

**Where:**
- `src/BooleanWorld/core/src/AnimatedProperty.cpp:56-62` — omits `mName`
- `src/BooleanWorld/core/src/InfluenceEye.cpp:657-660` — omits `mArcLength`
- `src/BooleanWorld/core/src/VertexTransformerObject.cpp:26-35` — omits `mPrevEntityPosition`,
  `mPrevEntityAngle`
- `src/BooleanWorld/core/src/World.cpp:79-138` — omits `mPrevPlayerPosition`

**Problem.** The same defect as BW-02, in four more places.

- `AnimatedProperty::copyFrom` omits `mName`, so every copied animator serialises as "Unnamed"
  instead of Scale / Angle / OrbitAngle / OrbitDistance.
- `InfluenceEye::copyFrom` omits `mArcLength`, which the copy constructor does not initialise
  either — an uninitialised float.
- `VertexTransformerObject::copyFrom` omits `mPrevEntityPosition` and `mPrevEntityAngle`, both
  read on the next `setInputs` call to decide `playerMove` / `playerTurn`.
- `World::copyFrom` omits `mPrevPlayerPosition`, whose sentinel guard (`< 999998.0f`) then reads
  garbage and decides whether trigger lines fire at all.

**Fix.** Copy every member in each. Then guard against recurrence: most of these classes would
work correctly with the compiler-generated copy operations, so the strongest fix is to delete the
hand-written ones where nothing special is needed. Where they must stay, add a round-trip test
(construct, mutate every field, copy, compare).

---

## BW-30 — `ObjectArray::acquireFreeSlot` pops from an empty vector when the array is empty

**Priority** P2 · **Difficulty** Trivial · **Area** applib

**Where:** `src/AppLib/include/applib/ObjectArray.h:38-46`

**Problem.** The guard is `if (mFreeIndices.empty()) resize(mObjects.size() * 2);`. When
`mObjects` is empty that is `resize(0)`, whose body only runs for `newSize > curSize` — so nothing
grows, and `mFreeIndices.back()` is called on an empty vector. `ObjectArray(0)` reaches this on
its first acquisition.

`releaseSlot` is also unguarded against double release, which would put the same index in the
free list twice and hand two callers the same object.

**Fix.**

```cpp
if (mFreeIndices.empty()) {
  resize(std::max<size_t>(mObjects.size() * 2, 8));
}
```

Debug-assert in `releaseSlot` that the slot is not already free.

---

## BW-31 — `SamplePoint` can land outside the face it is meant to sample

**Priority** P2 · **Difficulty** Medium · **Area** core

**Where:**
- `src/BooleanWorld/core/src/Arrangement.cpp:271-282` (`SamplePoint`)
- `src/BooleanWorld/core/src/Arrangement.cpp:606-620` (`BuildPolygonHierarchy`)
- `src/BooleanWorld/core/src/Arrangement.cpp:701-713` (winding seed)

**Problem.** The sample point is the midpoint of the cycle's *first* edge, displaced along the
left normal by between 0.25 and 0.354 grid units. For a needle-shaped face — which snap-rounding
can produce — whose first edge is short and whose opposite boundary passes within a quarter of a
unit, that displacement lands outside the face.

The point feeds both `BuildPolygonHierarchy` (parent selection) and the winding seed in
`BuildArrangement`, so a bad sample misclassifies an entire connected component's membership, not
just one face. This is precisely the class of defect the repro worlds `bug-1.yaml` and
`collision-issue-repro.yaml` exist to catch.

**Fix.** Pick the sample from the cycle's *longest* edge rather than its first, and scale the
perpendicular offset from the cycle's area rather than from that edge's length:

```cpp
// offset ~ |area| / (2 * longestEdgeLength), capped at a quarter of the edge length
```

A more robust alternative: triangulate the cycle once and take the centroid of the largest
triangle — exact in integer arithmetic if you keep the rational form. Either way this belongs in
the ADR-0008 step-1 degeneracy suite, with a test case built from a deliberately snapped sliver.

---

## BW-32 — Toggling all layers silently disables every trigger line

**Priority** P2 · **Difficulty** Medium · **Area** app

**Where:**
- `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:448-452` (`ToggleAllLayers`)
- `src/BooleanWorld/core/src/World.cpp:1141-1147` (`World::update`)

**Problem.** `ToggleAllLayers` sets `mCurrentLayer` to `BW_LAYER_ALL` (255) and passes it into
`WorldUpdateData::activeLayer`. `World::update` filters trigger lines with
`triggerLine->getLayer() == data.activeLayer` — an exact match with no `BW_LAYER_ALL` special
case. So the moment the player switches to "all layers", every trigger line on layers 0–254 stops
firing, which in turn freezes the transform flows that read trigger counts.

ADR-0009 says `activeLayer` should have become a layer mask; it did for the generator and did not
for trigger lines.

**Fix.** Finish the ADR-0009 port. Replace `uint8_t activeLayer` in `WorldUpdateData` with a
`LayerSelection`, and test membership consistently:

```cpp
auto layer = triggerLine->getLayer();
if (layer == BW_LAYER_ALL || data.layerSelection.test(layer)) {
  triggerLine->checkCollide(…);
}
```

`World::getPrimitivesInGridCell` has the same exact-match filter and should move with it.

---

## BW-33 — `Interpolator::getValue` clamps below-range times to the last point, not the first

**Priority** P2 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/include/core/Interpolator.h:308-338`

**Problem.** The segment scan only matches when `points[i].first <= time <= points[i+1].first`.
For a time before the first point, nothing matches, `res` stays NaN, and the fallback returns
`points.back().second` — the value at the *end* of the curve.

The same fallback also swallows genuine NaNs produced by `getValueInSegment` when two points
share a time (division by `p1.first - p0.first == 0`), which `setPoints` permits since it only
rejects *descending* times.

**Fix.** Clamp explicitly and handle zero-width segments as steps:

```cpp
if (time <= points.front().first)  { return points.front().second; }
if (time >= points.back().first)   { return points.back().second; }
```

and in `getValueInSegment`, `if (p1.first == p0.first) { return p1.second; }` before the divide.

---

# P3 — Medium

## BW-34 — `removeTransform` and `removeEvent` underflow and `pop_back` on empty containers

**Priority** P3 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/VertexTransformer.cpp:392-400`,
`src/BooleanWorld/core/src/AnimatedProperty.cpp:490-499`

**Problem.** Both use the shift-then-`pop_back` idiom with `for (i = index; i < count - 1; ++i)`.
On an empty container `count - 1` is `0xFFFFFFFF` and the loop writes out of bounds indefinitely;
with a valid container but an out-of-range index the loop is skipped and `pop_back` silently drops
the wrong element. `removeEvent` has an `assert`, which is a no-op in Release; `removeTransform`
has nothing. (`AnimatedProperty::updateEvent`'s assert message also says "removeEvent".)

**Fix.** Replace both bodies with a bounds-checked erase:

```cpp
if (index >= container.size()) {
  throw CoreException("index out of range");
}
container.erase(container.begin() + index);
```

---

## BW-35 — `checkCommitPendingClipping` reads from a moved-from optional

**Priority** P3 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:397-418`

**Problem.** `mActiveClipping = move(clipping.value());` is followed four lines later by
`GenerationDetails details{ clipping->id, … }`. It works only because `Clipping`'s implicit move
copies its scalar members rather than clearing them — add a user-defined move that resets `id`, or
change `id` to a move-only type, and the value becomes garbage with no diagnostic.

**Fix.** Capture before the move: `auto const clippingId = clipping->id;`.

---

## BW-36 — World deserialisation leaks everything allocated so far on an early return

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/World.cpp:276-321`

**Problem.** The cleanup that deletes accumulated primitives and trigger lines lives only in the
`catch` block. Three `return false` paths bypass it: a primitive that fails to deserialise, a
trigger line that fails, and the vertex-count check that throws before `push_back` (leaking the
object under construction). The first two also leak everything already accumulated in
`primitives` and `triggerLines`.

**Fix.** Hold both as `std::vector<std::unique_ptr<T>>` and `release()` into the world on success.
That deletes the catch-block cleanup as well.

---

## BW-37 — Deserialising a world silently resets `alwaysUpdateVertices`

**Priority** P3 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/World.cpp:360-370`

**Problem.** The commit block contains an unconditional `mAlwaysUpdateVertices = false;`, and the
flag is not serialised. ADR-0008 requires the comparison harness to run with
`setAlwaysUpdateVertices(true)` for reproducibility; any caller that sets it before loading has it
silently discarded, with no warning.

**Fix.** Remove the line, leaving the caller's setting alone. If resetting really is intended,
say so in a comment and make the harness set the flag after loading rather than before.

---

## BW-38 — A world built without acceleration grids crashes on its first primitive

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/World.cpp:33-35` (default ctor),
`:1186-1203` (grid helpers), `:911-927` (`primitiveChanged`)

**Problem.** `World()` delegates to `World(BW_WORLD_SIZE, -1.0f)`, and a non-positive grid size
skips `createAccelerationGrids`. `addPrimitiveToLookupGrid`, `removePrimitiveFromLookupGrid`,
`addTriggerLineToLookupGrid` and `primitiveChanged` all dereference the grid pointers with no null
check — unlike their siblings `findPrimitives` and `getPrimitivesInGridCell`, which throw a clear
`CoreException`. So the default-constructed `World` is a landmine.

**Fix.** Apply the same guard consistently across all four, or make grid creation unconditional
with a degenerate 1×1 grid when no size is supplied. The latter removes a whole class of
conditional from the class.

---

## BW-39 — Edges with the same face on both sides pick the wrong vertex

**Priority** P3 · **Difficulty** Medium · **Area** core

**Where:** `src/BooleanWorld/core/src/Arrangement.cpp:910-923` (`BuildArrangementTriangles`),
and the parallel walk in `src/BooleanWorld/core/src/World.cpp:839-868`
(`createMeshPrimitive::boundaryVertices`)

**Problem.** `addBoundary` picks a boundary edge's start vertex with
`edge.face[0] == faceIndex ? edge.v[0] : edge.v[1]`. If an edge has the same face on both sides —
a bridge or a dangling spur that survives cycle extraction — the test is true both times it
appears and the same endpoint is emitted twice, producing a degenerate ring for earcut.

`boundaryVertices` re-implements the same walk a third way, inferring direction by testing which
endpoint the *next* edge shares, which is ambiguous for a two-edge boundary.

**Fix.** Walk the cycle's vertex list directly rather than re-deriving direction from face
incidence. `Cycle::vis` already carries the traversal order and is currently discarded — carry it
into `ArrangementFace` (an `outerBoundaryVertices` parallel to `outerBoundary`, or replace the
edge list with vertex indices) and have both consumers read it.

---

## BW-40 — Face containment requires strictly nested bounding boxes

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/Arrangement.cpp:367-370` (`ContainsBox`),
`:607-623` (`BuildPolygonHierarchy`)

**Problem.** `ContainsBox` uses strict inequalities on all four sides, and
`BuildPolygonHierarchy` uses it as a prefilter before the point-in-cycle test. A hole whose
bounding box touches its parent's on any side — a rectangular void flush against a rectangular
room, which authored worlds produce constantly — fails the prefilter, is treated as a root, and
ends up as an inner boundary of the exterior face instead of a hole of its parent. The parent then
triangulates over its own hole.

**Fix.** Use non-strict comparisons in the prefilter; it exists to reject candidates cheaply, not
to decide containment. `PointInCycle` makes the actual call, and the smallest-area tiebreak below
already resolves nesting correctly.

```cpp
bool ContainsBox(Box const& outer, Box const& inner) {
  return outer.minx <= inner.minx && outer.maxx >= inner.maxx &&
         outer.miny <= inner.miny && outer.maxy >= inner.maxy;
}
```

Guard the self-comparison (`i == j`) which the loop already does.

---

## BW-41 — Triangulation converts exact integer topology to `float` before earcut

**Priority** P3 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/Arrangement.cpp:896-940`, `:993-1025`

**Problem.** ADR-0003's whole argument is that float carries about two ULP of margin against the
0.001 grid quantum at the world edge, so topology must never touch it.
`BuildArrangementTriangles` then converts each vertex to `float` for earcut. Two distinct grid
points near ±4096 are separated by roughly two ULP; earcut can collapse or mis-order them and emit
a wrong triangulation of a correct arrangement.

**Fix.** Earcut is a template. Change one `using` line in each of the two functions:

```cpp
using EarcutPoint = array<double, 2>;
…
polygon.push_back({double(vertex.x) / FixedPointUnitsPerWorldUnit,
                   double(vertex.y) / FixedPointUnitsPerWorldUnit});
```

`double` has ample headroom for the ±4.1e6 integer range.

---

## BW-42 — `normalise` returns a length, and one caller tests it against zero

**Priority** P3 · **Difficulty** Trivial · **Area** willpower

**Where:** `src/Willpower/willpower.common/include/willpower/common/Vector2.h:381-392`,
`src/BooleanWorld/app/src/WorldCollisionSim.cpp:616-623`

**Problem.** `normalise()` leaves the vector untouched when its length is at or below `1e-8` and
returns that length. The wall-hit callback writes
`if (normal.normalise() == 0.0) { normal = line.getNormal(); … }` — but for a degenerate normal
the return is a tiny non-zero value, so the fallback never runs and the collision response uses an
unnormalised near-zero vector. The player is then pushed by `normal * 0.001f`, which is
effectively nothing, so they stay embedded in the wall.

**Fix.** `if (normal.normalise() <= 1e-8) { … }` at the call site. Consider changing `normalise`
to return `bool` with the length available separately, so the ambiguity cannot recur.

---

## BW-43 — `projectLine` builds a bounding box with a negative size

**Priority** P3 · **Difficulty** Trivial · **Area** willpower

**Where:** `src/Willpower/willpower.collide/src/Simulation.cpp:381-392`

**Problem.** `lineBounds.setPosition(v0); lineBounds.setSize(v1 - v0);` produces a box whose min
exceeds its max whenever `v1` is left of or below `v0`. `getExtents` then reports an inverted
range, the grid query's cell loops do not execute, and the function reports no hit for roughly
half of all input directions.

**Fix.**

```cpp
lineBounds.setPosition({std::min(v0.x, v1.x), std::min(v0.y, v1.y)});
lineBounds.setSize({std::abs(v1.x - v0.x), std::abs(v1.y - v0.y)});
```

---

## BW-44 — `pointInConvexPolygon` iterates four billion times on an empty polygon

**Priority** P3 · **Difficulty** Trivial · **Area** willpower

**Where:** `src/Willpower/willpower.common/src/MathsUtils.cpp:670-681`, and the same pattern at
`:1003` (`triangleIntersectsConvexPolygon`) and `:1020-1022`
(`convexPolygonIntersectsConvexPolygon`)

**Problem.** `for (uint32_t i = 1; i < vertices.size() - 1; ++i)` with fewer than two vertices
underflows the `size_t`, and the `uint32_t` counter is promoted for the comparison — so the loop
runs to `UINT32_MAX` reading out of bounds.

**Fix.** `if (vertices.size() < 3) { return false; }` at the top of all three.

---

## BW-45 — `Serializable::deserialize` marks freshly loaded objects as modified

**Priority** P3 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/Serializable.cpp:820-831`

**Problem.** `mModified = deserializeImpl(...)` — so a *successful* load sets the modified flag,
and a failed one clears it. That is backwards on both counts. `serialize()` correctly sets
`mModified = false`. A world is dirty the instant it opens, which makes `isModified()` useless for
its obvious purpose of driving a save prompt.

**Fix.**

```cpp
bool ok = deserializeImpl(serializer, workData);
postDeserialization(workData);
mModified = false;
return ok;
```

Check whether the editor's save-prompt logic currently compensates for the inversion before
changing it.

---

## BW-46 — `transformT` reads uninitialised operands for out-of-range enum values

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/TransformFlow.cpp:105-255`

**Problem.** Neither the operand-type switch nor the nested input-type switch has a `default`, and
`operands[2]` is a bare local array. Enum values come straight from the file with no validation in
`tTransform::deserializeImpl`, so a corrupt or hand-edited world feeds uninitialised floats into
the animation pipeline. The operation switch has the same gap, silently carrying the previous
iteration's value.

**Fix.** `float operands[2] = {0.0f, 0.0f};`, add `default:` arms to all three switches, and
validate `OperandType`, `InputType` and `Operation` in `tTransform::deserializeImpl` with a
`SerializationException` naming the bad value.

---

## BW-47 — Waveform operands divide by a file-supplied multiplier guarded only by an assert

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/TransformFlow.cpp:87-103`, `:161-184`;
`src/BooleanWorld/core/src/Utils.cpp:467-477` (`clamp_angle`)

**Problem.** `triangle`, `saw` and `square` all compute `fmod(time, value) / value`, and the
`Sine` / `InvCosine` cases divide by `fnMultipliers[i]`. Every one is protected by
`assert(transform.fnMultipliers[i] > 0.0f)`, which vanishes in Release. A zero multiplier from a
file produces NaN, which propagates into `clamp_angle` — whose `while` loops never terminate on
NaN.

**Fix.** Validate `fnMultipliers > 0` at deserialisation and clamp to a small positive minimum at
use. Make `clamp_angle` NaN-safe and loop-free:

```cpp
float clamp_angle(float angle) {
  if (!std::isfinite(angle)) { return 0.0f; }
  angle = std::fmod(angle, 360.0f);
  return angle < 0.0f ? angle + 360.0f : angle;
}
```

---

## BW-48 — TriggerLine operands throw `out_of_range` on stale indices

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/TransformFlow.cpp:186-208`,
`src/BooleanWorld/core/src/VertexTransformer.cpp:489-503`
(`updateTransformTriggerLineIndices`)

**Problem.** The three TriggerLine operand types call
`inputs.triggerLines->at(transform.indices[i])`. `tTransform::makeConstant` and friends initialise
`indices` to `~0u`, so any transform whose operand type is switched to TriggerLine without setting
an index throws every frame.

`updateTransformTriggerLineIndices` has the mirror problem: `mapping.at(...)` throws when a
trigger line referenced by a primitive is not in the remap — which is exactly what deleting a
trigger line produces.

**Fix.** Bounds-check in `transformT` and fall back to `0.0f`:

```cpp
auto idx = transform.indices[i];
operands[i] = (inputs.triggerLines && idx < inputs.triggerLines->size())
    ? float((*inputs.triggerLines)[idx]->getTotalTriggerCount())
    : 0.0f;
```

In the remap, use `find` and skip (or reset the operand to `Constant`) when the index is unmapped.

---

## BW-49 — `setOrientation` is the only transform setter that does not invalidate

**Priority** P3 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/VertexTransformerObject.cpp:151-157`

**Problem.** Every other setter on `VertexTransformerObject` — position, transform offset, parent,
eye offset, all animation and influence mutators, all transform-flow mutators — ends in
`invalidatePostTransform(true, true)`. `setOrientation` does not, so cached vertices and bounds go
stale until something else invalidates. The editor calls it directly from
`setPrimitiveOrientation` and from `addPrefabInstance`.

**Fix.** Add the call. Note the related coupling: `setInfluenceEyeAngleOffset` stores its argument
as `offset - mOrientation` and the getter adds `mOrientation` back, so changing orientation also
silently shifts the effective eye angle. Decide whether that is intended and comment it.

---

## BW-50 — Parent chains are walked per vertex with no cycle detection

**Priority** P3 · **Difficulty** Medium · **Area** core

**Where:** `src/BooleanWorld/core/src/VertexTransformerObject.cpp:185-197`
(`calculateWorldPosition`), `src/BooleanWorld/core/src/World.cpp:324-333` (parent fix-up)

**Problem.** `calculateWorldPosition` recurses through `mParent` and is called once per vertex from
`transformVertex`. Deserialisation rebuilds parent links from ids in the file with no check that
the graph is acyclic — `vtoIdToParentMap` is applied verbatim — so a hand-edited or corrupted
world produces infinite recursion and a stack overflow.

The fix-up loop also never checks for the `-1` "no parent" sentinel, so it inserts a null entry
into `vtoIdToVtoMap` for every root object and calls `setParent(nullptr)` on all of them.

**Fix.** In the fix-up loop:

```cpp
auto parentId = it->second;
if (parentId < 0) { continue; }
auto parentIt = workData.vtoIdToVtoMap.find(uint32_t(parentId));
if (parentIt == workData.vtoIdToVtoMap.end()) {
  addDeserializationWarning(format("Unknown parent id {}", parentId));
  continue;
}
vt->setParent(parentIt->second);
```

Then walk the resulting graph once and reject cycles. Separately, cache the world position per
frame (invalidated by `setPosition` / `setParent`) rather than recomputing it per vertex — see
PERF-4.

---

## BW-51 — `printf`-style format mismatch on a 64-bit value

**Priority** P3 · **Difficulty** Trivial · **Area** app

**Where:** `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:974`

**Problem.** `ImGui::Text("%d", record.generationTimeNs / 1000)` passes a `uint64_t` through
varargs to a `%d` conversion. That is undefined behaviour and on x64 prints the low half followed
by whatever the next argument slot holds. The generation-time column in the debug panel is
therefore not showing generation time.

**Fix.** `ImGui::Text("%llu", (unsigned long long)(record.generationTimeNs / 1000));`

Also at `:965`, the commit-lag column subtracts `generationCompleteTime` without checking it is
non-negative, so a clipping committed before generation was recorded shows a nonsense lag.

---

## BW-52 — The clipping debug panel reports statistics nothing populates

**Priority** P3 · **Difficulty** Small · **Area** app

**Where:** `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:900-1041` (the table),
`:502-504` (the commit message), `src/BooleanWorld/core/include/core/Stats.h:129-166`

**Problem.** Four of the panel's thirteen columns — primitive vertices, polygons generated,
vertices generated, vertices interpolated — read `ClipStats` fields that the old Clipper pipeline
wrote and the arrangement engine does not. They are permanently zero, as is the
"Clipping committed: N prims, M polys" message. A debug view that confidently reports zeros is
worse than one that reports nothing.

**Fix.** Delete the dead `ClipStats` fields (see BW-73) and the four columns that read them.
Replace with what the arrangement can actually report: vertex, edge and face counts, triangle and
wall counts, and time split between `BuildPSLG` and classification. Those are cheap to collect in
`BuildArrangement` and are the numbers that matter once BW-11 is being worked.

---

## BW-53 — Player location is tested at the pre-movement position

**Priority** P3 · **Difficulty** Medium · **Area** app

**Where:** `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:376-411` (`updatePreEntities`)

**Problem.** The method updates the world with `newPosition` and then evaluates
`getContainingFaceIndex` and `circleIntersectsWall` against `curPosition`. So
`mPlayerPolygonIndex`, `mPlayerBorderIntersectIndex`, and the collision-line set built from them
all describe where the player was, not where they are — and the collision-line radius is only
`BW_PLAYER_SPEED + BW_PLAYER_RADIUS`, which leaves no margin for the discrepancy.

**Fix.** Test the position the physics step actually resolved to. That means splitting the order:
build collision lines around the *predicted* position (as now), run the simulation, then evaluate
containment against the resolved `PhysicalStats::position`.

The `if (!playerInWorld() || playerIntersectsWorldBorders())` block immediately below is an empty
`// TODO`, so nothing consumes the answer today — decide what should happen there at the same
time, or delete the block.

---

## BW-54 — The player collider is owned by the simulation but referenced by the entity handler

**Priority** P3 · **Difficulty** Medium · **Area** app

**Where:**
- `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:126-137` (`setupPlayerCollision`)
- `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:189-194` (`destroyGameObjects`)
- `src/Willpower/willpower.collide/src/Simulation.cpp:23-34` (destructor)

**Problem.** `setupPlayerCollision` news a `ColliderCircle`, hands it to `WorldCollisionSim` (whose
base destructor deletes all colliders) and also to `EntityHandler::setupCollisions`, which stores
it as `mwPlayerCollider`. `destroyGameObjects` deletes the simulation, taking the collider with
it, but leaves the entity handler's pointer dangling. Any update that runs after teardown
dereferences it.

The ownership rule is never stated: `addCollider` does not take ownership by name,
`removeCollider` deletes, and the destructor deletes.

**Fix.** Make ownership explicit — `int32_t addCollider(std::unique_ptr<Collider>)` — and null the
handler's pointer in `destroyGameObjects`:

```cpp
applib::ModelInstance::entityHandler()->setupCollisions(nullptr, nullptr);
delete mWorldCollisionSim;
mWorldCollisionSim = nullptr;
mPlayerCollider = nullptr;
```

---

## BW-55 — The physics simulation iterates colliders in pointer order

**Priority** P3 · **Difficulty** Small · **Area** willpower

**Where:** `src/Willpower/willpower.collide/include/willpower/collide/Simulation.h:484`,
`src/Willpower/willpower.collide/src/Simulation.cpp:270-276`

**Problem.** `mColliders` is a `std::set<Collider*>`, so `update` resolves colliders in address
order — which varies between runs and between allocator states. With one collider this is
invisible; the moment a second is added, collision resolution stops being reproducible, which
undermines any attempt to record or replay a session.

**Fix.** Use a `std::vector<Collider*>` with stable insertion order (`removeCollider` already has
to search, so nothing is lost), or keep the set but sort by the index `addCollider` already
assigns before iterating.

---

## BW-56 — `Simulation` owns raw pointers with no copy control

**Priority** P3 · **Difficulty** Trivial · **Area** willpower

**Where:** `src/Willpower/willpower.collide/include/willpower/collide/Simulation.h:483-558`

**Problem.** The class owns two `AccelerationGrid*` and deletes every `Collider*` in its set, but
declares neither a copy constructor nor a copy assignment operator and does not delete them. Any
accidental copy — passing by value, storing in a container that reallocates — double-frees
everything. `mNumSweepChecks` is also missing from the constructor's initialiser list, so
`getNumSweepChecks()` before the first `update` returns garbage.

**Fix.**

```cpp
Simulation(Simulation const&) = delete;
Simulation& operator=(Simulation const&) = delete;
```

and add `mNumSweepChecks(0)` to the initialiser list.

---

## BW-57 — `AccelerationGrid::addItem` silently orphans an already-registered item

**Priority** P3 · **Difficulty** Small · **Area** willpower

**Where:** `src/Willpower/willpower.common/src/AccelerationGrid.cpp:95-135`, `:137-149`

**Problem.** `mIndicesToCells[index] = IndexCollection();` overwrites the record of which cells the
index currently occupies without removing it from them. The item stays in its old cells forever,
so queries return an index whose owner has moved. Callers currently always remove first, so this
is latent — but it is the kind of invariant that should be enforced where it is cheap.

`removeItem` in this class also guards only with an `assert` and dereferences `end()` in Release,
whereas the `Extended` variant throws properly.

**Fix.** In `addItem`, if the index is already present, remove it from its recorded cells first
(or assert loudly in debug). Give `removeItem` the same throw-on-missing behaviour as
`ExtendedAccelerationGrid::removeItem`, with a `failIfNotFound` parameter for symmetry.

---

## BW-58 — `ExtendedAccelerationGrid`'s `failIfNotFound` handling cannot fire

**Priority** P3 · **Difficulty** Trivial · **Area** willpower

**Where:**
`src/Willpower/willpower.common/include/willpower/common/ExtendedAccelerationGrid.h:460-480`

**Problem.** `removeItemFromCell` wraps `cell.indices.erase(index)` in a try/catch to set
`foundItem = false`. `std::set::erase(key)` does not throw and returns a count, so `foundItem` is
always true, the `failIfNotFound` parameter is inert, and the `updateFn` runs even when nothing was
removed — which means the cell's frame-number metadata is bumped by removals that did not happen.

**Fix.**

```cpp
bool foundItem = cell.indices.erase(index) > 0;
if (!foundItem && failIfNotFound) {
  throw std::runtime_error(std::format("Index {} not found in cell", index));
}
if (updateFn && foundItem) { updateFn(&cell.user); }
```

---

## BW-59 — Primitive visibility is tested by edge crossings only

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/Primitive.cpp:736-773` (`updateTime`)

**Problem.** The `BW_PRIMITIVE_NO_TIME_UPDATE_IF_VISIBLE` branch tests the three view-cone edges
against the primitive's bounding box with `lineIntersectsBox` and treats "no edge crosses" as not
visible. A box wholly inside the cone crosses nothing and is judged invisible; so is a cone wholly
inside a large box.

`DynamicWorldDataGenerator::preparePrimitives` answers the same question correctly with
`boxIntersectsTriangle` — two different visibility tests, disagreeing, for the same concept.

**Fix.** Use `wp::MathsUtils::boxIntersectsTriangle` here too. Share the view-triangle
construction as well: it is currently built three separate ways, in `Utils::calculateFovTriangle`,
`WorldDataGenerator::update` (which stores `mViewTriangle`) and
`StatePlayBooleanWorld::debug_renderMinimap`.

---

## BW-60 — `rotatedCopy` mutates polygons directly and never invalidates

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/Primitive.cpp:247-278`

**Problem.** The function writes rotated positions straight into `p->mPolygons` rather than going
through `setVertices`, so the copy's cached `mVertices` and `mBounds` still describe the unrotated
shape until something else invalidates. The editor adds the result to the world immediately, and
`addPrimitive` happens to call `updateVertexPositions` — so it works by luck rather than by
contract.

The inner loop also shadows the outer `p` (the copied primitive) with a local `p` (a vertex
position), and declares a `ComplexPolygon polyVertices` that is never used.

**Fix.** Build a new `vector<ComplexPolygon>` and call `setVertices` on it, which invalidates
correctly and enforces the per-contour vertex limit. Rename the shadowing local; delete the unused
one.

---

## BW-61 — Material colours go stale when parameters are edited

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/MaterialDefinition.cpp:1174-1225`,
`src/BooleanWorld/editor/src/Actions.cpp:90-113`

**Problem.** `data.baseColourUint` is the packed form of `data.baseColour` and is recomputed in
exactly one place — `deserializeImpl`. `MaterialDefinitionData` is a public struct, and
`setPrimitiveDefaultMaterial` writes `baseColour` directly without repacking. So a primitive whose
material is set in the editor renders with whatever colour was last loaded from disk.

`MaterialDefinitionData::hash` also hard-codes `array<uint32_t, 12>` where the contents are
`1 + BW_MATERIAL_PARAMS_MAX + 3` — correct today, silently truncating the moment that macro
changes.

**Fix.** Make `baseColourUint` a derived accessor (`uint32_t packedColour() const`) rather than
stored state, or make the fields private with a setter that repacks. Size the hash buffer from the
macro: `array<uint32_t, 1 + BW_MATERIAL_PARAMS_MAX + 3>`.

---

## BW-62 — Early returns leave the serializer's map/array stack unbalanced

**Priority** P3 · **Difficulty** Small · **Area** core

**Where:** `src/BooleanWorld/core/src/MaterialDefinition.cpp:1186-1210`,
`src/BooleanWorld/core/src/PrimitivePropertySet.cpp:1043-1059`

**Problem.** `MaterialDefinition::deserializeImpl` returns `false` from inside a `beginArray` block
without the matching `endArray`/`endMap`, so the serializer's nesting state is corrupt for anything
that tries to continue.

In the same area, `PrimitivePropertySet::deserializeImpl` ignores the return value of all three
`MaterialDefinition::deserialize` calls, so a material that fails to load is reported nowhere and
the caller sees success.

**Fix.** Throw a `SerializationException` instead of returning — the enclosing `try` in
`PrimitivePropertySet` already handles it and will unwind cleanly. Check the three return values
and `copyErrorsAndWarnings` from each.

---

## BW-63 — `undo(count > 1)` records the same world in every redo entry

**Priority** P3 · **Difficulty** Small · **Area** editor

**Where:** `src/BooleanWorld/editor/src/Undo.cpp:574-616`

**Problem.** Both `undo` and `redo` capture the current world and selection *before* the loop and
push that same snapshot for each of `count` iterations, so multi-step undo produces a redo stack
of duplicates. The selection is captured as a `const&` into the document and then mutated by
`setSelectedPrimitiveIndices` inside the loop, so later iterations read the already-overwritten
value. Neither function checks that the stack is non-empty before `back()`.

**Fix.** Move the capture inside the loop, take the selection by value, and guard at the top:

```cpp
void undo(Document* doc, int count) {
  for (int i = 0; i < count && canUndo(); ++i) {
    auto world = *doc->getWorld();
    auto selection = doc->getSelectedPrimitiveIndices();   // by value
    auto modified = doc->isModified();
    auto const& oldEntry = gUndoStack.back();
    gRedoStack.push_back({oldEntry.id, {world, selection, modified}});
    doc->setWorld(oldEntry.data.world);
    doc->setSelectedPrimitiveIndices(oldEntry.data.selection);
    doc->setModified(oldEntry.data.docModified);
    gUndoStack.pop_back();
  }
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_UNDO_REDO);
}
```

Note this makes the BW-07 cost worse per step, so land BW-07 first or together.

---

## BW-64 — `abandonUndoableAction` leaves the transaction's initial value behind

**Priority** P3 · **Difficulty** Trivial · **Area** editor

**Where:** `src/BooleanWorld/editor/src/Undo.cpp:560-568`, `:527-533`, `:570-572`

**Problem.** `abandon` clears the id, world, selection and function but not
`gTransactionalInitialFloatValue` or `gTransactionalInitialVectorValue`.
`transactionValueHasChanged` tests those against NaN, so after an abandoned action the next widget
to query it gets a stale answer and may record a spurious undo entry.

`undoableActionInProgress()` also tests `gTransactionalId` while `beginUndoableAction` guards on
`gTransactionalFunc` — two different notions of the same state.

**Fix.** Reset both initial values to NaN in `abandonUndoableAction` and
`commitUndoableAction`. Pick `gTransactionalFunc` as the single authoritative in-progress marker
and make `undoableActionInProgress()` test it.

---

## BW-65 — `ReactiveCamera::setPosition` does not mark the camera dirty

**Priority** P3 · **Difficulty** Trivial · **Area** app

**Where:** `src/BooleanWorld/app/src/ReactiveCamera.cpp:920-932`

**Problem.** `setYaw` and `setPitch` both set `mDirty = true`; `setPosition` does not.
`updatePreRenderers` calls all three every frame, so yaw and pitch changes mask it — but a frame in
which the player moves without turning relies on the base class recomputing from position anyway,
which is exactly the sort of assumption that breaks when the base changes.

**Fix.** Set `mDirty = true` in `setPosition` too.

---

## BW-66 — Animation frame advance loops forever on a zero-duration frame

**Priority** P3 · **Difficulty** Trivial · **Area** app

**Where:** `src/BooleanWorld/app/src/EntityHandlerBooleanWorld.cpp:822-860`

**Problem.** `while (visual->timer >= frame.time)` subtracts `frame.time` each pass. A frame whose
duration is zero — trivially producible from a resource file — makes the condition permanently true
and the loop never exits. The initial `getAnimationFrame(animation, visual->frame)` before the loop
is also unguarded against an out-of-range frame index.

**Fix.** `if (frame.time <= 0.0f) { break; }` inside the loop, and validate frame durations and
indices when the animation database loads.

---

## BW-67 — A face with no winning primitive resolves to palette index zero, which has no mesh

**Priority** P3 · **Difficulty** Medium · **Area** app / core

**Where:**
- `src/BooleanWorld/core/src/Arrangement.cpp:800-844` (winner derivation)
- `src/BooleanWorld/app/src/WorldBatch.cpp:486-494` (`getMeshIndexForMaterialHash`)
- `src/BooleanWorld/app/src/WorldRenderer3d.cpp:276-284` (`update`)

**Problem.** `BuildArrangement` derives the winning primitive with a run-based heuristic that is
deliberately different from the fold used for `solid`. Where the two disagree, a solid face keeps
`paletteIndex = 0` — the default-constructed entry reserved for exterior and empty faces.

`WorldRenderer::updateDataProviders` then hashes that entry's zeroed material and calls
`getMeshIndexForMaterialHash`, which **throws a `GameException` from inside the render path**,
because meshes are only created for materials that exist on a primitive.

Separately, `WorldRenderer3d::update` dereferences every entry of `mUniforms` without a null check,
and that vector is sized from the model's mesh count rather than the material count.

**Fix.** Three parts:

1. Make the winner derivation total. A solid face always has at least one member (every operation
   yields `false` from `false, false`), so fall back to the highest-priority member when the
   run-based pass finds none — the `!face.solid` branch below already implements exactly that
   lookup and can be reused.
2. Have `getMeshIndexForMaterialHash` return a default mesh index rather than throwing; a renderer
   should not throw mid-frame.
3. Skip null entries in `WorldRenderer3d::update`, and size `mUniforms` from
   `mMaterialHashToMesh.size()` or guard the index.

---

# P4 — Low

## BW-68 — Three layers of pure forwarding wrap the same four animators

**Priority** P4 · **Difficulty** Large · **Area** core

**Where:** `core/include/core/VertexTransformerObject.h`, `core/include/core/VertexTransformer.h`,
`core/include/core/AnimatedProperty.h` and the three matching `.cpp` files

**Problem.** `VertexTransformerObject` forwards roughly fifty methods to `VertexTransformer`, which
forwards the same fifty to `mAnimators[key]`, which forwards to two `Interpolator`s. Eight of those
pairs are exact duplicates of each other at the same level — `addAnimationValue` and
`addPointToAnimationInterpolator` have identical bodies, as do the remove, update and easing pairs,
and the same four again on the influence side. That is roughly 1,400 lines whose only content is
the name of the thing below.

**Fix.** `getAnimationInterpolator(key)` and `getInfluenceInterpolator(key)` already exist on both
layers. Expose them, delete the forwarders, and move callers onto the interpolator directly. The
`invalidatePostTransform` calls that `VertexTransformerObject` adds are the one piece of real
behaviour; keep those behind a small RAII mutation scope:

```cpp
{ auto scope = primitive->mutate(); scope.animation(Key::Scale).addPoint(t, v); }
```

Worth planning as its own change; it touches the editor UI extensively.

---

## BW-69 — `World::getWorldData` ignores both parameters

**Priority** P4 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/World.cpp:1205-1207`,
`src/BooleanWorld/core/include/core/World.h:241`

**Problem.** `getWorldData(wp::Vector2 const& position, float angle)` forwards to
`mDataGenerator->getWorldData(this)` and uses neither argument — not even a `BW_UNUSED`, so it
warns. Every call site computes and passes a position and a view angle for nothing.

**Fix.** Drop the parameters and update the three call sites.

---

## BW-70 — Two grid-metadata accessors have identical bodies

**Priority** P4 · **Difficulty** Trivial · **Area** core

**Where:** `src/BooleanWorld/core/src/World.cpp:998-1012`, `:1014-1031`

**Problem.** `getGridCellPrimitivesVersion` and `getGridCellFrameNumber` differ only in the name of
their out-parameter; both read `getUser(cellIndex).lastUpdatedFrameNumber`. Nearby,
`getPrimitivesInGridCell` takes a `frame_number_type* primitivesVersion` that it never writes, and
filters by `getLayer() == activeLayer` with no `BW_LAYER_ALL` handling — inconsistent with every
other layer test in the codebase (see BW-32).

**Fix.** Keep one accessor. Remove the unwritten out-parameter.

---

## BW-71 — Debug residue left in shipping code

**Priority** P4 · **Difficulty** Trivial · **Area** core / app

**Where:**
- `src/BooleanWorld/core/src/World.cpp:1130-1134` — `if (events & …DEBUG) { int x = 5; }`
- `src/BooleanWorld/core/src/DynamicWorldDataGenerator.cpp:312` — `if (true) {` opening a lock scope
- `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:351-355` — `for (int i = 0; i < 1; ++i) { /* comment */ }`
- `src/BooleanWorld/app/src/StatePlayBooleanWorld.cpp:683-696` — a 14-line `if (false)` block

**Problem.** A breakpoint anchor with an unused-variable warning, a scope opened with `if (true)`
instead of a bare brace, an empty loop, and a dead bounds-overlay block.

**Fix.** Delete all four. If the primitive-bounds overlay is wanted, put it behind the existing
`mDebugDisplay` flags alongside the other overlays.

---

## BW-72 — Arrangement fields and functions nothing reads

**Priority** P4 · **Difficulty** Trivial · **Area** core

**Where:** `core/include/core/Arrangement.h:52-58`, `:73-83`, `:194-201`;
`core/src/Arrangement.cpp:633-645`, `:993-1025`

**Problem.** `Cycle::primitiveIndices` is never populated. `Face::owningPolygon` and
`Face::holePolygon` are never set, and their explanatory comment describes behaviour that does not
exist. `BuildFaceTriangles` and the `Face`-based `PointInFace` overload have no callers.
`BuildFaces` takes a `cycles` parameter it discards with `(void)cycles`. `BuildPolygonHierarchy`
takes `vector<Cycle>&` non-const and never modifies it.

**Fix.** Delete the dead members and functions; make the two parameters reflect reality
(`const&`, and drop the unused one).

---

## BW-73 — Statistics structures left over from the removed Clipper pipeline

**Priority** P4 · **Difficulty** Small · **Area** core

**Where:** `core/include/core/Stats.h:129-176`, `core/include/core/Triangulator.h`,
`core/include/core/Vertex.h:196`, `core/src/Primitive.cpp:553`

**Problem.** `ClipStats` carries seven counters and two operators; nothing writes any of them.
`TriangulationStats` is written only by `Triangulator`, now reached only from
`Primitive::triangulate`. `Vertex` still has an `int64_t = 0` second constructor parameter — the
Clipper z field — that is accepted and discarded, and `Primitive::deserializeImpl` still reads and
drops a `"z"` key.

**Fix.** Delete `ClipStats` and its consumers (BW-52 removes the last readers). Drop the vestigial
`Vertex` constructor parameter. Keep the `"z"` read only if backward compatibility with
pre-rewrite world files is a requirement — and if so, say so in a comment.

---

## BW-74 — Layer filtering and priority sorting are implemented three times

**Priority** P4 · **Difficulty** Small · **Area** core

**Where:** `core/src/WorldDataGenerator.cpp:33-44`,
`core/src/ArrangementWorldDataGenerator.cpp:65-79`, `core/src/DefaultWorldDataGenerator.cpp:34-48`

**Problem.** Each of the three filters by layer and sorts by priority with its own copy of the
predicate. `ArrangementWorldDataGenerator` additionally carries its own `mLayerSelection` that
`DefaultWorldDataGenerator` bypasses entirely by constructing a local generator and calling the
vector overload.

**Fix.** One free function:

```cpp
std::vector<Primitive*> selectAndOrderPrimitives(World const&, LayerSelection const&);
```

Remove the second layer-selection member. This is the natural place to land BW-13's `stable_sort`
fix once and for all.

---

## BW-75 — `PrimitiveGroup` is unreachable, and its `removePrimitive` never shrinks

**Priority** P4 · **Difficulty** Trivial · **Area** core

**Where:** `core/src/PrimitiveGroup.cpp:945-962`, `core/include/core/Primitive.h:23`

**Problem.** Nothing constructs a `PrimitiveGroup` — the only references are its own files and the
`friend` declaration in `Primitive`. Its `removePrimitive` shifts elements down and then returns
without `pop_back()`, so the vector keeps its size with a duplicated last element.
`InfluenceEye::inArc` is similarly stranded: it returns `false` unconditionally under a comment
asking how it should work.

**Fix.** Delete `PrimitiveGroup`, its header and the `friend` declaration. Delete `inArc` or
implement it.

---

## BW-76 — Five app headers are never included

**Priority** P4 · **Difficulty** Trivial · **Area** app

**Where:** `app/include/MetricsDataProvider.h`, `PrimitiveLineDataProvider.h`,
`DebugLineDataProvider.h`, `ScrollingBuffer.h`, `WorldTriangle.h`

**Problem.** None of the five is referenced anywhere in the project. `MetricsDataProvider` is also
unfinished — its `update` writes only a timestamp and never touches the line vertices or colours it
exists to provide. `ScrollingBuffer` is duplicated in the editor's `UiHelpers.h`, which is the copy
actually used.

**Fix.** Delete all five, about 330 lines.

---

## BW-77 — `WorldCollisionSim` carries a spatial grid it never uses

**Priority** P4 · **Difficulty** Medium · **Area** app

**Where:** `app/src/WorldCollisionSim.cpp:597-598`, `:639-660`

**Problem.** The base `Simulation` is constructed with `ExtentsCalculator({0,0}, {100,100}, 0)` — a
100×100 region for an 8192-unit world. `WorldCollisionSim` then overrides `getLineIndices` to
ignore its `bounds` parameter and return every line index, and its `addLine`/`clearLines` bypass
the base's grid registration entirely. So the base's line-splitting and grid machinery is dead
here, and the extents are simply wrong. It happens not to matter because `createWorldCollisions`
already narrows to walls near the player.

**Fix.** Pick one model and commit to it. Either pass the real world extents and use the base's
grid (and delete the `getLineIndices` override), or keep the caller-side culling and document it,
dropping the base grid from this path. Either way, `getLineIndices` should not build a
`set<uint32_t>` of every index on every query — return a span or fill a caller-owned vector.

---

## BW-78 — `Vector2`'s assignment operators return by value

**Priority** P4 · **Difficulty** Small · **Area** willpower

**Where:** `willpower.common/include/willpower/common/Vector2.h:62-226`, `:551-568`

**Problem.** `operator=` and all ten compound assignment operators return `Vector2` rather than
`Vector2&`, so every chained or nested assignment silently copies and `(a += b) += c` modifies a
temporary.

`Vector2Compare` is also inverted on x and ascending on y — it is a valid strict weak ordering on
`(-x, y)`, so it compiles and "works", but it is not the order anyone reading it will expect.

**Fix.** Return references throughout. Fix `Vector2Compare` to be a plain lexicographic
`(x, y)` order, or rename it to say what it actually does.

---

## BW-79 — Two `Vector2` / `MathsUtils` helpers are wrong and unused

**Priority** P4 · **Difficulty** Trivial · **Area** willpower

**Where:** `willpower.common/include/willpower/common/Vector2.h:330-337` (`distanceToRay`),
`willpower.common/src/MathsUtils.cpp:229-240`, `:670-681`

**Problem.** `distanceToRay` returns a signed perpendicular projection, not a distance, and gives a
meaningless answer for points behind the ray origin. `convexPolygonArea` (BW-21) and
`pointInConvexPolygon` (BW-44) are likewise unused. A public geometry API with silently wrong
members is worse than a smaller one.

**Fix.** Delete them, or fix and test them. Given none has a caller, deletion is the cheaper
correct answer.

---

## BW-80 — `MathsUtils::Epsilon` is mutable global state used by geometric predicates

**Priority** P4 · **Difficulty** Small · **Area** willpower

**Where:** `willpower.common/include/willpower/common/MathsUtils.h:38`,
`willpower.common/src/MathsUtils.cpp:765-789`,
`willpower.collide/src/Simulation.cpp:408`

**Problem.** A non-const `static float` that `lineIntersectsLine` and `projectLine` consult. Any
code that adjusts it changes the behaviour of every predicate in the process, including on other
threads. It is also applied as an *absolute* tolerance to a determinant that scales with the
square of coordinate magnitude, so at world scale it is meaningless.

**Fix.** Make it `constexpr`, or take a tolerance parameter where it matters. Scale the parallel
test relative to the operand magnitudes:
`if (fabs(det) < Epsilon * std::max(s0.lengthSq(), s1.lengthSq()))`.

---

## BW-81 — Non-standard exception constructor and a case-mismatched include

**Priority** P4 · **Difficulty** Small · **Area** core

**Where:** `core/src/Primitive.cpp:427`, `core/include/core/Interpolator.h:412, 418, 467, 495`,
`core/src/Easing.cpp:627`, `core/src/VertexTransformer.cpp:684, 722`,
`core/src/PrimitiveGroup.cpp:960`, `core/include/core/DynamicWorldDataGenerator.h:16`

**Problem.** Roughly a dozen sites throw `std::exception("message")`, which is an MSVC extension —
the standard constructor takes no arguments. The project already defines `CoreException` and
`SerializationException` for exactly this.

Separately, `DynamicWorldDataGenerator.h` includes `"core/ThreadSafeQueue.h"` while the file on
disk is `ThreadsafeQueue.h`; only Windows' case-insensitive filesystem makes this compile.

**Fix.** Use the project's exception types throughout. Fix the include's case (and pick one
spelling for the file — `ThreadSafeQueue.h` matches the class name).

---

## BW-82 — `setWorldDataGeneratorFactory` does not store a factory

**Priority** P4 · **Difficulty** Small · **Area** core

**Where:** `core/src/World.cpp:425-436`, `core/include/core/WorldDataGenerator.h:55-56`

**Problem.** The method deletes the current generator, invokes the passed factory once, and
discards it. Nothing is stored, so the name promises a policy the class does not have — and every
caller passes a lambda that ignores all four of its parameters. `WorldDataGeneratorFactory`'s
signature (`Vector2, int, int, float`) is unused by every implementation.

**Fix.** Rename to `setWorldDataGenerator(WorldDataGenerator*)` taking ownership, and delete the
factory typedef. The `World` constructor's `generatorFactory` parameter can go the same way.

---

## BW-83 — `instantiatePrimitive` rebuilds a map of eight `std::function`s on every call

**Priority** P4 · **Difficulty** Trivial · **Area** core

**Where:** `core/src/World.cpp:140-159`

**Problem.** The creator table is a function-local `map<string, function<Primitive*()>>` constructed
fresh for each primitive during deserialisation — eight heap-allocated closures and eight tree
nodes per primitive loaded.

**Fix.** `static const std::map<std::string, PrimitiveCreator> primCreators = { … };`

---

## BW-84 — Vertex data is copied by value in two per-frame paths

**Priority** P4 · **Difficulty** Trivial · **Area** app / core

**Where:** `app/src/StatePlayBooleanWorld.cpp:650`, `core/src/Primitive.cpp:713`

**Problem.** Both write `auto complexPolygons = primitive->getVertices();` where `getVertices`
returns a `const&` to a `vector<ComplexPolygon>`. Each call deep-copies every vertex of the
primitive. The minimap draw does it once per visible primitive per frame.

**Fix.** `auto const&`. Also hoist the visibility test in `ImGui_renderPrimitives` out of the
innermost contour loop, where it is currently recomputed per contour rather than per primitive.

---

## BW-85 — Ring-buffer caps are off by one, and the debug overlay ignores view scale

**Priority** P4 · **Difficulty** Trivial · **Area** app / editor

**Where:** `app/src/StatePlayBooleanWorld.cpp:291-293`, `:527-529`, `:716-723`;
`editor/src/Undo.cpp:536-538`

**Problem.** Three `while (container.size() >= MAX) pop_front();` loops cap at `MAX - 1`, so the
named limits of 128 messages, 10 clipping records and 20 undo levels are actually 127, 9 and 19.

In `ImGui_renderView`, the player-radius and view-distance circles are drawn with world units as
pixel radii while every other coordinate goes through `wpVecToImVec2`, which applies the view
scale — correct only because the scale is currently hard-coded to `{1,1}`.

**Fix.** Use `>` in all three loops. Multiply the circle radii by `viewScale.x`.

---

## BW-86 — ImGui context and allocator overrides are set across the DLL boundary and never restored

**Priority** P4 · **Difficulty** Small · **Area** app

**Where:** `app/src/StatePlayBooleanWorld.cpp:1050-1091`

**Problem.** `_renderImGui` saves the previous context, plot context and allocator functions, sets
its own, and then leaves all three restorations commented out with a note that it is not really
needed. Since this runs inside an application DLL against the launcher's ImGui, the override
persists for the host after the call returns. It works today; it will not survive a second
consumer.

**Fix.** Restore all three at the end of the function. If the current behaviour is genuinely
intended, delete the dead saves and the commented lines and state the assumption in one comment —
the half-done version is the worst of both.

---

## BW-87 — `getWDG` can return null and no caller checks

**Priority** P4 · **Difficulty** Trivial · **Area** app

**Where:** `app/src/StatePlayBooleanWorld.cpp:280-284`, `:189-194`, `:356-361`, `:639-641`

**Problem.** `getWDG()` is a `dynamic_cast` that returns null whenever the world's generator is not
a `DynamicWorldDataGenerator` — which is the case for any world built through
`DefaultWorldDataGenerator`, including the default `World` constructor. `setup`,
`destroyGameObjects` and `ImGui_renderPrimitives` all dereference it immediately.

**Fix.** Assert once in `setup` with a clear message that the play state requires a dynamic
generator, and null-check in the two later uses. Or make the requirement structural by having
`Map` guarantee the generator type.

---

## BW-88 — Shared material tables are per-translation-unit statics, and a define is misspelled

**Priority** P4 · **Difficulty** Trivial · **Area** common

**Where:** `BooleanWorld/common/include/common/MaterialRegistry.h:29-46`,
`BooleanWorld/common/include/common/GameDefines.h:16`

**Problem.** `MaterialNames` and `MaterialParams` are declared `static` at namespace scope in a
header, so every including translation unit gets its own copy — wasteful, and a source of confusion
if anything ever compares addresses. `BW_WORLD_CRILING_HEIGHT_MAX` is a misspelling of "ceiling".

**Fix.** `inline constexpr` for both tables. Rename the define (and its uses, if any).

---

## BW-89 — An unreachable guard and a no-op pre-work step

**Priority** P4 · **Difficulty** Trivial · **Area** editor / app

**Where:** `editor/src/Document.cpp:293-316`,
`app/src/StateMapTransitionBooleanWorld.cpp:339-349`

**Problem.** `Document::openDoc` calls `reset()`, which does `mWorld.reset()`, and then tests
`if (mWorld) throw EditorException("Tried to create a new document with an existing one.")` —
always false.

`StateMapTransitionBooleanWorld::getPreWork` returns a lambda whose entire body is
`addText("Destroying world renderer")`; the renderer is actually destroyed later, in
`processResources`. The status text is a lie and the step does nothing.

**Fix.** Delete the guard. Either destroy the renderer in the pre-work step (matching
`StateMapUnloadBooleanWorld`, which does) or drop the step and its message.

---

## BW-90 — `dllSetArgument` accepts unknown argument names

**Priority** P4 · **Difficulty** Trivial · **Area** launcher

**Where:** `app/src/DLL.cpp:105-119`, `:121-152`

**Problem.** The function tests one name and returns `0` — success — for everything else, so a
misspelled launcher argument is silently ignored. `dllGetNextStateFactory` also drives a file-scope
counter that `dllOnExit` never resets, so a second entry into the DLL enumerates nothing.

**Fix.** Return non-zero for unrecognised names. Reset `nextStateFactory` in `dllOnEntry`.

---

# Appendix A — Dead code

Unreferenced outside its own definition, or reachable but provably inert. Deleting this is the
cheapest reduction in surface area available. Difficulty for the whole appendix: **Small**, save
for `PrimitiveGroup` and the app headers which are pure deletions.

| Item | Where | Status |
| --- | --- | --- |
| `MetricsDataProvider.h` | `app/include` | Never included; `update()` is a stub writing only a timestamp |
| `PrimitiveLineDataProvider.h` | `app/include` | Never included |
| `DebugLineDataProvider.h` | `app/include` | Never included |
| `WorldTriangle.h` | `app/include` | Never referenced anywhere in the tree |
| `ScrollingBuffer.h` | `app/include` | Never included; the editor's `UiHelpers.h` has the live copy |
| `PrimitiveGroup` | `core` | Never constructed; only its own files and a `friend` reference it |
| `BuildFaceTriangles` | `core/Arrangement.cpp` | Declared and defined; no callers |
| `PointInFace(Face, cycles, graph)` | `core/Arrangement.cpp` | Overload with no callers |
| `IsLeafSolidBoundaryInsideSolid` | `core/Arrangement.cpp` | Result erased on the next line — BW-12 |
| `Cycle::primitiveIndices` | `core/Arrangement.h` | Never populated |
| `Face::owningPolygon` / `holePolygon` | `core/Arrangement.h` | Never assigned; the comment describes behaviour that does not exist |
| `PSLG::sourceContours` | `core/Arrangement.h` | Read only by the dead `IsLeafSolidBoundaryInsideSolid` |
| `ClipStats` | `core/Stats.h` | Seven counters, two operators; nothing writes any field |
| `DynamicWorldDataGenerator::copyFrom` | `core` | Defined; the copy ctor and `operator=` use the base's instead — BW-22 |
| `MAX_NUM_PENDING_CLIPPINGS` | `core/DynamicWorldDataGenerator.cpp` | `#define`d, never referenced — BW-16 |
| `InfluenceEye::inArc` | `core` | Returns `false` unconditionally under a design question in a comment |
| `TransformFlow::saw` | `core` | Duplicated inline at the `Saw` case; the member is unreachable |
| `Collider::checkCollision` | `willpower.collide` | Every case body is commented out |
| `MathsUtils::distanceToRay` / `pointInConvexPolygon` / `convexPolygonArea` | `willpower.common` | Unused, and the last two are incorrect — BW-21, BW-44, BW-79 |
| `Vertex`'s `int64_t` constructor parameter | `core/Vertex.h` | Accepted and discarded — Clipper z residue |
| `World::getWorldData` parameters | `core` | `position` and `angle` both ignored — BW-69 |
| `getGridCellFrameNumber` | `core/World.cpp` | Byte-identical to `getGridCellPrimitivesVersion` — BW-70 |
| `ThreadSafeQueue::back()` | `core/ThreadsafeQueue.h` | Returns a reference after releasing the lock; no callers |

---

# Appendix B — Optimisation candidates

Ordered by expected payoff. The first two are the only ones likely to be visible in a frame time
or a generation time; the rest are steady drips. All estimates are algorithmic — nothing was
profiled.

| ID | Target | Current cost | Change | Difficulty |
| --- | --- | --- | --- | --- |
| PERF-1 | `BuildPSLG` segment intersection | `O(S²)` pairs plus an `O(C·S)` snap re-check with a cubic tail; no broad phase | Build the `AccelerationGrid` broad phase ADR-0002 specifies — BW-11. Almost certainly the largest single win available. | Medium |
| PERF-2 | Undo snapshots | A full `World` deep copy per level, each cloning a concurrencpp thread-pool runtime; `getActionHistory` copies the whole stack per call | Iterate the stack by `const&` (one line), then move to serialised snapshots — BW-07 | Trivial / Large |
| PERF-3 | `AccelerationGrid` cells | `std::set<uint32_t>` per cell — 16,384 red-black trees for a 128×128 grid; every query allocates a whole tree and returns it by value | Flat sorted vectors per cell; return through a reusable caller-owned vector. Fixes the `set_union` UB in BW-04 at the same time. | Medium |
| PERF-4 | `Vector2::rotate` in `transformVertex` | `sin` and `cos` evaluated in `double`, twice per vertex — the angles are constant for the whole primitive | Hoist the sin/cos pair out of the vertex loop and pass the rotation as a precomputed pair; use `sinf`/`cosf`. Cache `calculateWorldPosition` per frame too (BW-50). | Small |
| PERF-5 | `BuildPolygonHierarchy` | `O(C²)` with an `O(V)` point-in-cycle test inside | Sort candidates by area and use the bounding-box prefilter as a real index rather than a linear scan | Medium |
| PERF-6 | `ArrangementWorldData::getNearestVertexIndex` | Linear scan over every arrangement vertex per call | Reuse the triangle grid, or add a vertex grid alongside it | Small |
| PERF-7 | `Primitive::triangulate` | Quadratic in complex-polygon count and reads out of bounds — BW-09 | Reset the accumulator per iteration; the quadratic behaviour disappears with the correctness fix | Small |
| PERF-8 | Per-mesh vertex buffers in `WorldRenderer` | Every mesh is sized for the whole world's triangle count, so memory is meshes × total | Size each mesh from its own triangle count in a counting pass, or use one shared buffer with per-mesh ranges | Small |
| PERF-9 | `World::findPrimitiveIndex(exact)` | Linear over all primitives, building a full triangulation per candidate, with the ignore set passed by value | Use the existing primitive grid, cache triangulations, take the ignore set by `const&` | Medium |
| PERF-10 | `instantiatePrimitive` | A map of eight `std::function`s constructed per primitive during load | Make it `static const` — BW-83 | Trivial |
| PERF-11 | earcut on `float` coordinates | Loses the exact integer topology the arrangement guarantees — BW-41 | Instantiate on `double`: one `using` declaration in each of two functions | Trivial |
| PERF-12 | `World::removePrimitives` | `std::find` over the index list per primitive — `O(n·m)` | Sort the index list once and use a two-pointer sweep, or hash it | Trivial |

---

# Suggested order of work

A pragmatic sequence, front-loading the cheap high-value fixes.

**1. One sitting — trivial, high value.**
BW-02, BW-05, BW-06, BW-08, BW-25, BW-28, BW-19, BW-20, BW-21, BW-24, BW-27, BW-13, BW-30,
BW-07(a), BW-35, BW-51. Sixteen findings, all Trivial, including four of the ten Critical.

**2. Next — the remaining Critical and the correctness-critical High.**
BW-01, BW-03, BW-04, BW-09, BW-10, BW-29, BW-22, BW-23, BW-26, BW-33.

**3. Then — the engine gaps the ADRs already promised.**
BW-11 (broad phase), BW-12 (nested-leaf decision), BW-31 (sample point), BW-32 (ADR-0009 layer
mask). These are where the geometry engine stops being trustworthy under degenerate input, and
BW-11 is the performance story.

**4. Then — the threading and lifetime contracts.**
BW-15, BW-16, BW-14, BW-54. BW-14 is the largest and benefits from BW-15 and BW-16 landing first.

**5. Ongoing — dead code and the P4 list.**
Appendix A is a single mechanical pass. BW-68 (the forwarding layers) is worth its own plan.

Two changes are worth landing together rather than separately: **BW-07 and BW-63** both touch the
undo stack, and BW-63 makes BW-07's cost worse until BW-07 lands.
