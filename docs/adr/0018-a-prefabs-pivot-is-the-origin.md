# ADR-0018: A Prefab's pivot is the origin, not its bounds centre

**Status:** Accepted
**Date:** 2026-08-21
**Relates to:** ADR-0017 (prefab Primitives are derived but excluded from the build)

## Context

Rotating a Prefab orbits its Primitives about a pivot and advances each
Primitive's own angle by the same amount. A `DefinePrefabs` step carries a
tiling type and size, rendered as a single wireframe polygon centred on the
origin, against which the user places the Prefab's Primitives.

The obvious implementation takes the pivot from the Primitives themselves —
the bounds centre of the Prefab's contents.

## Decision

The pivot is **the origin**: the centre of the tile guide, fixed, and
independent of what the Prefab contains.

The bounds centre moves as the Prefab is edited, so the pivot for a
rotation would depend on the Prefab's current contents — and, worse, a
Prefab deliberately authored hugging one corner of its tile would snap to
tile-centred the moment it is instanced. That destroys the one thing the
guide exists to let the user express. With the origin as pivot, "a Prefab
placed on a tile" means "the Prefab's origin sits at the tile's centre",
and an off-centre Prefab stays off-centre.

The transform operations themselves are not built here. They have no
caller: the steps that place and manipulate Prefabs are not yet defined,
and whether such a step mutates a Prefab's Primitives in place or clones
them through a transform is a question that step answers. The pivot
convention is recorded now because it constrains how Prefabs are authored
today, which the API does not.

## Consequences

- Recorded before any Prefab exists, because adopting it later would
  silently change the meaning of every already-authored Prefab. This is
  the most expensive decision of this work to reverse.
- The tile guide is not decoration. It is the visible form of the pivot,
  which is why it renders whenever a `DefinePrefabs` step is active and
  independently of the ordinary snapping grid.
