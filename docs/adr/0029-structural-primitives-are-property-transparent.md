# ADR-0029: Structural Primitives are property-transparent

**Status:** Accepted
**Date:** 2026-08-29

## Context

ADR-0028 distinguishes authored collision overrides from generated collision.
A related ownership ambiguity appeared when authored Difference Primitives were
made responsible for the material of walls exposed by their cuts.

PrefabField Replace mode generates hidden Difference squares solely to erase
geometry accumulated by earlier grid phases. Those squares have no authored
property set because they are implementation structure, not world content.
Treating them as ordinary Difference Primitives made their empty wall material
own exposed Borders, producing fallback-grey walls.

Inferring structural intent from an empty material was rejected. An authored
Difference with a missing material should remain visibly erroneous, and a
structural Primitive must remain transparent even if it accidentally acquires
non-empty default properties.

## Decision

A Primitive has a property-contribution role: **Contributing** or
**Transparent**. The default is Contributing. A Transparent Primitive
participates fully in membership and the boolean fold but cannot supply
properties to a generated surface.

PrefabField Replace squares are Transparent. When such a Difference produces a
Border, the surviving solid Face retains property ownership. An authored,
Contributing Difference continues to own the material of the wall exposed by
its cut.

The role is runtime build metadata, not authored serialized state. Replace
squares are regenerated from their PrefabField and marked Transparent each time
the Layer is rebuilt.

## Consequences

- Replace squares cannot leak default or accidental properties into rendered
  output.
- Authored Differences retain explicit cut-wall ownership.
- Missing authored materials are not silently hidden by fallback inheritance.
- Future structural fold Primitives can opt into the same role without special
  material-string checks.
