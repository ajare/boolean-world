# ADR-0009: Layer selection is a per-generation set

**Status:** Accepted
**Date:** 2026-08-16

## Context

Generation currently targets a single active layer. `WorldDataGenerator` holds
`uint8_t mActiveLayer`, fed from `WorldUpdateData::activeLayer`, and
`getPrimitives` filters with:

```cpp
if (pLayer != layer && layer != BW_LAYER_ALL && pLayer != BW_LAYER_ALL) continue;
```

so a primitive is included when its layer matches, when the generation asked
for `BW_LAYER_ALL` (255), or when the primitive itself is on `BW_LAYER_ALL`.

The intended model is broader: the set of primitives to process is chosen
dynamically from **one or more** layers on each generation.

## Decision

A generation selects a **set** of layers, not one.

The selection is a 256-bit mask (4 × `uint64_t`, 32 bytes) held by the
generator, replacing `uint8_t mActiveLayer`. A primitive is included when its
layer is in the mask, or when its layer is `BW_LAYER_ALL`.

Primitives keep a single `uint8_t` layer and the `BW_LAYER_ALL` magic value.
Giving each primitive its own mask would be the cleaner generalisation —
inclusion becomes `(primitive.mask & generation.mask) != 0` and the magic value
disappears — but it changes the serialised world format for no benefit the
requirement asks for. Revisit only if authors need primitives on arbitrary
layer subsets.

Priority ordering for the fold (ADR-0001) runs across the **whole selected
set**, spanning layers. Layers filter; they do not group or nest.

## Consequences

- Selecting a different layer set produces a different fold, and because the
  fold is non-local (ADR-0001) the result differs *globally*, not only where
  the added or removed primitives sit. This is the intended mechanism for
  changing world state, but it means a layer-set change is a whole-world
  change, and one that cannot be made incremental later without care.
- The layer set is an input to generation, so it belongs in the generation
  request alongside the primitive snapshot — not in per-frame update data.
  `WorldUpdateData::activeLayer` becomes a layer mask on the generation
  request.
- Because a layer-set change alters the whole world, it interacts with the
  visibility rules retained in ADR-0007: the change becomes visible only when
  the generation commits, which is gated on no moved primitive being on screen.
  A layer switch that must appear immediately therefore needs an explicit
  bypass of the commit gate, not a faster generation.
- Membership bitsets (ADR-0004) index into the generation's selected primitive
  list, so they are unaffected by which layers that list was drawn from.
- Comparison runs (ADR-0008) must pin the layer mask along with
  `setAlwaysUpdateVertices(true)` to be reproducible.
