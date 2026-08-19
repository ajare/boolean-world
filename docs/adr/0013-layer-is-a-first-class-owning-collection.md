# ADR-0013: Layer is a first-class collection that owns its Primitives and WorldTriggerLines

**Status:** Accepted
**Date:** 2026-08-19
**Supersedes:** ADR-0009's ownership stance (its per-generation-set model is retained; see below)

## Context

ADR-0009 gave `Primitive`/`WorldTriggerLine` a raw `uint8_t` layer tag and
generation a bitset selecting which tag values to include. It explicitly
rejected per-primitive ownership of a layer set, because "it changes the
serialised world format for no benefit the requirement asks for."

That benefit now exists: authoring wants `Layer` as a real, named, orderable
object — something the editor creates and manages directly, not an implicit
partition inferred from a number scattered across primitives.

## Decision

`Layer` becomes a `Serializable` class that **owns** its `Primitives`, its
`WorldTriggerLines`, and its own spatial acceleration grids. `Primitive` and
`WorldTriggerLine` lose their `mLayer` field entirely — an object's layer is
simply whichever `Layer`'s collection contains it. `BW_LAYER_ALL` is dropped;
a primitive belongs to exactly one layer.

`World` owns an ordered `std::vector<Layer>` and a transient (unserialized)
active-layer index, initialised to 0 on every load. Each `Layer` carries a
stable id, assigned at creation and independent of its position in that
vector, plus a display name. `World`'s existing query facade is preserved,
delegating to the active layer, so most callers are unaffected.

A World's saved file embeds its Layers inline. A `Layer` can also be
exported/imported standalone as `.layer` (binary) or `.layer.yaml`, distinct
extensions from `.world`/`.yaml` so `Document::openDoc` stays pure
extension-dispatch.

ADR-0009's actual generation model is **kept**: a generation still folds
across a *selected set* of layers via a mask (now a set of stable Layer ids
rather than tag values), spanning them in one non-local fold. Layers still
filter; they still do not group or nest. Only the ownership question that
ADR-0009 declined is reopened here. The active-layer index is purely an
editor authoring-focus concept and does not constrain what a generation
selects.

Breaking the save format is accepted; no migration path is provided for
existing `.world`/`.yaml` files.

## Consequences

- "Changing a primitive's layer" becomes moving it between two `Layer`
  collections (a new Actions/Undo operation), not setting a field.
- Content that relied on `BW_LAYER_ALL` to appear on every layer must be
  duplicated per layer; there is no cross-layer primitive any more.
- The generation layer mask and the active-layer index are both
  runtime-only. Reopening a saved world always starts generation scoped to
  just the active layer (id 0), never "all layers" and never whatever mask
  was last used.
- `LayerSelection` (`std::bitset<256>`) is retained, now indexed by stable
  `Layer` id instead of the old per-primitive tag value.
