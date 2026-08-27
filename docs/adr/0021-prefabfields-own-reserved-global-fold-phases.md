# ADR-0021: PrefabFields own reserved global fold phases

**Status:** Superseded by ADR-0026
**Date:** 2026-08-23
**Relates to:** ADR-0001 (preserve the priority-ordered fold exactly), ADR-0017 (prefab Primitives are derived but excluded from the build), ADR-0018 (a Prefab's pivot is the origin)

## Context

A `PrefabField` may contain aligned Tiles at 32, 64, 128, and 256 world-unit
sizes. A smaller Tile can either add its Prefab to the result or replace
anything already built beneath that Tile. Replacement must work across every
selected Layer and every `PrefabField`, rather than only within the field that
contains the Tile.

The existing build is a global priority-ordered left fold (ADR-0001). Local
step order cannot express replacement consistently when fields and Layers are
interleaved, so PrefabField output needs globally shared fold phases.

## Decision

Priorities 249 through 255 are reserved for seven `PrefabField` phases, in
this order:

1. priority 249: 256 Tile content;
2. priority 250: 128 Replace differences;
3. priority 251: 128 Tile content;
4. priority 252: 64 Replace differences;
5. priority 253: 64 Tile content;
6. priority 254: 32 Replace differences;
7. priority 255: 32 Tile content.

A Replace phase emits one exact Tile-sized Difference Primitive for each
occupied Replace Tile. These generated differences participate in the fold but
are hidden from ordinary editor Primitive outlines. 256 Tiles are always Add;
smaller Tiles default to Replace but may be changed to Add.

All selected Layers and all their `PrefabField`s contribute to these same
global phases. A smaller Replace Tile may therefore clear lower-priority
geometry produced by another field or Layer.

Prefab source Primitive priorities retain their meaning as relative ordering
inside a content phase. Generated clones receive the phase's reserved priority.
Equal source priorities are emitted deterministically by Tile x coordinate,
Tile y coordinate, and then Prefab Primitive list order.

Ordinary authored Primitives are limited to priorities 0 through 248. Prefab
source Primitives may use the full 0 through 255 range because those values are
relative inputs rather than final fold priorities. Loading an ordinary
Primitive in the reserved range is rejected rather than silently clamped.

## Consequences

- The seven phases make nested-grid replacement independent of Layer and step
  ordering while preserving the single global fold.
- Reserving the top seven priorities is a file-format constraint and reduces
  the ordinary authored priority range.
- Replace is intentionally global and can erase unrelated lower-priority
  geometry under its Tile.
- Deterministic emission order is part of the generated-geometry contract.
- Adding another Tile size or another per-size operation requires revisiting
  the reserved priority band and this decision.
