# ADR-0005: WorldData is an immutable snapshot with a property palette

**Status:** Accepted
**Date:** 2026-08-16

## Context

Generation runs on a concurrencpp worker thread while `World::update` mutates
primitives every frame — updating time, animation values and vertex positions.
The result reaches consumers as a handle back into those live primitives:
`WorldRenderer.cpp:56` does `world->getPrimitive(tri.primitiveIndex)->getProperties()`
per triangle. The worker's snapshot is therefore not a snapshot at all; it
reads mutable state that the main thread is concurrently writing.

Separately, `DynamicWorldDataGenerator::getWorldData` returns
`mActiveClipping.worldData` **by value** (`DynamicWorldDataGenerator.cpp:286`)
and `World::getWorldData` returns it by value again. `WorldDataDetail::copyFrom`
deep-copies every border polygon, every arrangement polygon, the whole graph
and the whole vertex-data array — once per frame, on the path that must not be
slow.

## Decision

- Faces store **resolved properties**, not a reference to a live primitive.
- Properties are held in a per-generation **palette**: a
  `vector<PrimitivePropertySet>` copied at generation time, with faces storing
  a `uint16_t` index into it. `PrimitivePropertySet` embeds three
  `MaterialDefinition`s, so storing one per face would be wasteful when there
  are only ~123 distinct values; the palette is self-contained and compact.
- `WorldData` becomes immutable once generated and is handed across the thread
  boundary as `shared_ptr<const WorldData>`. It is never copied.

## Consequences

- The generation/consumption race is removed structurally: consumers never
  reach back into primitives for anything the geometry depends on.
- The per-frame deep copy disappears. Publishing a generation becomes a pointer
  swap; consumers hold a reference to whichever generation was current when
  they asked.
- Old generations stay alive while anything holds a reference, which is the
  intended behaviour — a consumer mid-frame keeps a coherent view — but means
  peak memory can hold more than one generation. At this scale that is
  immaterial.
- `World::getPrimitive(face.primitiveIndex)` is no longer needed for rendering.
  `primitiveIndex` is still carried for identification (editor selection,
  `getContainingTrianglePrimitiveIndex`), but it is no longer load-bearing for
  correctness, so a stale index cannot corrupt what is drawn.
- The commit gate in `DynamicWorldDataGenerator::canCommit` — holding a
  generation back while any primitive it moved is on screen — is unaffected and
  still applies.
