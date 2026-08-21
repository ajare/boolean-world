# ADR-0020: MeshPrimitive stores an authoritative containment tree

**Status:** Accepted
**Date:** 2026-08-22
**Supersedes:** ADR-0016 (MeshPrimitive editing goes through a geometry proxy)
**Relates to:** ADR-0003 (exact integer topology), ADR-0010 (remove Clipper2 and path primitives)

## Context

ADR-0016 retained `vector<ComplexPolygon>` as MeshPrimitive's authored storage.
That format records a filled Ring and its direct Holes, but not an Island inside
a Hole or any deeper relationship. The editing proxy consequently had to
re-derive deeper containment from coordinates. Deep nesting is now an authored
requirement, so changing a geometric containment rule must not be able to
reinterpret saved topology.

The flat form remains useful to procedural producers, transformation,
triangulation, and World generation. It is not expressive enough to be the
source of truth.

## Decision

MeshPrimitive owns an authoritative alternating containment forest. Its roots
are Shells. A `MeshFilledRegion` stores the Ring of a Shell or Island and its
direct `MeshHole` children; each `MeshHole` stores its Ring and direct Island
children. The types make same-role parent/child links unrepresentable, require
no persistent node IDs, preserve sibling order, and impose no semantic depth
limit.

Construction validates a complete candidate before replacing storage. Rings
must be finite, simple, non-degenerate, within aggregate resource limits, and
contained by their structural parent. Sibling and root interiors may not
overlap. Every accepted Ring is normalized to anticlockwise winding and
MeshPrimitive-local coordinates. Only const tree traversal is exposed.

`vector<ComplexPolygon>` is derived in deterministic pre-order: every Shell or
Island emits one entry containing its Ring followed by only its direct Hole
Rings. Transformation, triangulation, and generation continue to consume this
derived form. A shallow compatibility converter interprets each input entry as
one root Shell with direct Holes and rejects cross-entry nesting rather than
inferring a missing hierarchy.

The ADR-0016 proxy entry points remain temporarily for downstream consumers.
Their flat representation is no longer authoritative: commits construct and
validate a complete candidate tree before replacing the authored tree. A
hierarchy-aware editing proxy and final API contraction are separate work.

Serialization records the alternating tree under MeshPrimitive's schema. The
inherited flat payload is a derived compatibility cache and is ignored when the
tree is loaded; files without the tree fail rather than silently becoming an
empty MeshPrimitive.

## Consequences

- Shell, Hole, Island, and deeper alternating relationships survive copying,
  assignment, transformation, serialization, and generation without being
  inferred from authored flat entries.
- World generation needs no new contour contract; flattening supplies the
  existing arrangement input and EvenOdd tree-native meshes alternate filled
  and empty regions at every depth.
- Flat producers remain intentionally shallow. A producer that knows deep
  topology must use tree-native construction.
- The temporary generic geometry proxy can still infer hierarchy at its
  compatibility boundary. It is not storage authority and cannot mutate a
  MeshPrimitive partially.
- ADR-0016's conclusion that flat storage was sufficient is reversed. Its
  motivation for a topology-capable editing representation remains relevant to
  the follow-up proxy work.
