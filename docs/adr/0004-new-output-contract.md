# ADR-0004: New output contract; remove WorldVertexData

**Status:** Accepted
**Date:** 2026-08-16

## Context

Under ADR-0002 properties are a property of a **face**. A face knows its
membership set, hence its winning primitive, hence its `floorZ`, `ceilingZ` and
materials — directly and unambiguously.

The current contract instead carries properties on **vertices**.
`WorldVertexData` holds two property sets per vertex (`properties[0]` for the
"previous" side, `properties[1]` for the "next" side), propagated around
contours by `ClipperUtils::interpolatePathVertices`. Where propagation cannot
determine a value it guesses:

```cpp
cData.properties[1] = nextSet ? nData.properties[0] : cData.properties[0];
```

Three defects follow from putting face-level truth on vertices:

1. **Silent index truncation.** `ZCallback::interpolateVertex` appends a
   `WorldVertexData` for every computed intersection on every pass. With
   `N + 4R` passes re-crossing the same geometry, the array far exceeds the
   count of distinct world vertices. The index is packed into 20 bits via
   `BW_BITS_TR(i, 20)`, which truncates without checking, and
   `World::validateVertexCount` only counts *primitive* vertices at add time.
   Past `BW_VERTEX_COUNT_MAX` intersections the index wraps and vertices read
   another vertex's properties.
2. **Property interpolation is a stub.** The `BW_CLIPPER_LERP_PROPERTIES` block
   computes weights `t00`/`t01`/`t10`/`t11` in all three branches and discards
   them; no value is ever written. The flag is never set anyway —
   `clipPrimitives` passes only `SET_PRIMITIVE | GEN_INTER_ON_UNION`.
3. **`is2Sided()` can never be true.** `buildPolygonGraph` runs on `mBorder`
   (`Clipper.cpp:424`), but every border polygon has `primitiveIndex == ~0u`,
   hardcoded by `addTraversedPath` (`ClipperUtils.cpp:270`). So every graph
   edge gets `p[0] = p[1] = ~0u`, and the entire two-sided branch of
   `WorldRenderer.cpp:136-210` — the step walls between primitives of differing
   height — is dead code.

Options considered were: impersonate the old contract by synthesising
per-vertex data from per-face data; port consumers to a new contract; or run
both behind a compatibility shim.

## Decision

Adopt a new contract. Remove `WorldVertexData`, `ZCallback`, and the z-bitfield
packing macros in `Defines.h`. Port consumers.

No compatibility shim. A shim would have to invent per-vertex properties from
per-face truth — precisely the ambiguity that made the current code guess — so
it would mean debugging a translation layer intended for deletion, and it could
mask real differences during comparison.

The model:

```cpp
using Membership = std::bitset<128>;    // indices into THIS generation's list

struct ArrangementVertex { int64_t x, y; };

struct ArrangementEdge {
  uint32_t v[2];        // endpoints
  uint32_t face[2];     // left, right — always both valid
};

struct ArrangementFace {
  std::vector<uint32_t> outerBoundary;                 // edge indices, CCW
  std::vector<std::vector<uint32_t>> innerBoundaries;  // holes, explicit
  Membership membership;
  bool solid;
  uint16_t paletteIndex;    // see ADR-0005
};
```

Membership is indexed into the current generation's culled primitive list, not
the world's. `Defines.h` permits 16,383 primitives, so a world-indexed bitset
would be 2KB per face; generation-indexed it is 16 bytes.

## Consequences

- Holes become structural. The current convention — "N polygons, and after each
  polygon is M holes", relied on by `Triangulator::processPolygon` and
  `WorldData::pointInPolygon` — is replaced by faces owning their inner
  boundaries.
- All three defects above are removed by construction rather than fixed: there
  are no interpolated vertices to index, no properties to interpolate, and edge
  sidedness is native.
- Consumers to port: `WorldRenderer::updateDataProviders` (the wall branch
  collapses from three near-identical blocks to one),
  `StatePlayBooleanWorld.cpp:512-514`, `editor/src/UI.cpp:2863,3412-3444`, and
  `editor/src/Actions.cpp:203` (mesh templates via `clipToClippedPolygons`).
- Step walls will appear that have never rendered. Expected, and the reason the
  ADR-0001 regression diff covers the solid region only.
