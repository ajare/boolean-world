# ADR-0016: MeshPrimitive editing goes through a `wp::geometry::Mesh` proxy

**Status:** Accepted
**Date:** 2026-08-20
**Relates to:** ADR-0003 (exact integer topology), ADR-0015 (step editing capabilities)

## Context

A `MeshPrimitive` stores `vector<ComplexPolygon>` — nested lists of
Rings, with no adjacency, no edge identity, and no
outer/hole relationship recorded anywhere. That is an appropriate
*storage* form: it is compact, it serializes directly, and everything
downstream of authoring converts it to contours and hands it to the
arrangement, which derives whatever topology it needs.

It is a poor *editing* form. Sub-object editing needs stable identities
for edges and vertices across a sequence of mutations, adjacency queries
to heal a Ring after a deletion, spatial acceleration for hit-testing
and rubber-band selection, and validation that a proposed vertex move
leaves the shape sound. Building those on flat Rings means
hand-rolling a topology layer — the classic source of subtle geometric
bugs, and one this project has already paid for once in the code the
World geometry rewrite replaced.

`willpower.geometry` already contains that layer. `wp::geometry::Mesh` is
an indexed vertex/edge/polygon structure with explicit holes,
acceleration grids, and integrity checking, and it ships the operations
this needs: `MeshOperations::splitEdge`, `Mesh::removeVertex` (reporting
the healing edge it creates), `removePolygon`, the `move*` family,
`getVertexIndicesInBoundingBox`, and `MeshValidator::validateVertexMove`
and `validatePolygonAdd`. Neither `core` nor `editor` links it today.

## Decision

Sub-object editing of a `MeshPrimitive` runs against a
`wp::geometry::Mesh` **proxy**, not against its stored polygons. `core`
and `editor` both gain a `Willpower.Geometry` link dependency.

**Authority is windowed.** The proxy is built when a `MeshPrimitive`
becomes the editor's active mesh and is authoritative for as long as it
lives; the `MeshPrimitive`'s stored polygons are re-derived from it when
an edit commits. Outside that window the stored polygons are the only
truth. Two representations of the same geometry exist only while one of
them is unambiguously in charge.

**The proxy holds rest-pose world-space coordinates**, not the stored
unit-space locals. Pick radii, selection distances, validator tolerances
and acceleration-grid cell sizes are all real-scale quantities; a proxy
in unit space would require every one of them rescaled per mesh by that
Primitive's size. This confines the transform conversion to the two
conversion points instead of scattering it through hit-testing.

**The storage mapping is fixed.** Within each `ComplexPolygon`, the Ring
at index 0 becomes the proxy's outer polygon and the remainder attach as
holes; the round-trip restores that ordering. A filled island nested
inside a hole is stored as its own top-level `ComplexPolygon`, so the
stored ordering records one level of nesting and any deeper nesting is
re-derived by geometric containment when the proxy is built. The stored
format is unchanged, and no migration is required.

**Conversion lives in `core`**, as part of `MeshPrimitive`'s own API,
because the ordering and containment rules above are properties of how a
`MeshPrimitive` is stored. A blunt polygon setter is explicitly
rejected: it would be a hole through which any caller could write
Rings violating those rules. Proxy lifetime, active-mesh
tracking, selection and validation live in `editor`, which is where the
screen-space and `Settings`-dependent concerns belong.

**The proxy's structure is kept sound**, not merely plausible. Every
Ring stays simple, and every hole stays inside its outer with holes
disjoint — enforced at creation and at every edit, with drags clamping
to the last valid position. This matters because hole-aware operations
added later will trust the structure the proxy carries; a hole recorded
as a hole but lying outside its outer is exactly the corrupt input that
makes such an operation fail in a way that is hard to trace back.

## Considered alternatives

**Flat Ring lists with hand-written topology operations.** No new
dependency, no second representation, no conversion. Rejected: it means
reimplementing edge identity, adjacency healing, spatial queries and
self-intersection validation that already exist, tested, one repository
away.

**A short-lived proxy, rebuilt per operation.** Nothing to keep in sync.
Rejected: selection would have to be stored as Ring/vertex coordinates
and re-resolved against a freshly built proxy every time, index churn
from deletions tracked by hand, and per-frame drag validation would pay
a full rebuild — reintroducing exactly the bookkeeping the proxy exists
to remove.

**A flat proxy with no hole relationships.** Faithful to what the stored
data actually records, since the fill rule — not an outer/hole
distinction — is this codebase's authority on what is solid. Rejected
because it forecloses the hole-aware operations this proxy is being
introduced to enable.

**Changing `MeshPrimitive` to store a genuine tree.** The clean answer if
nesting becomes deep or common, and ADR-0014 set the precedent for
accepting a format break. Rejected as larger than this needs; the
containment re-derivation above gives a correct proxy at a
once-per-activation cost, and this remains available later without
disturbing any editing semantics.

## Consequences

- `core` and `editor` both link `Willpower.Geometry`. `core` previously
  linked only `Willpower.Common`.
- Two representations of the same geometry exist during an editing
  session. The authority window is what keeps that safe, so anything
  that reads a `MeshPrimitive`'s polygons while a proxy is live is
  reading stale data by construction — the commit point is the only
  place they are reconciled.
- Deeper-than-one-level nesting is a geometric fact rather than a stored
  one, so it is re-derived on every proxy build. A change to the
  containment rule changes how existing files are interpreted, without
  changing the files.
- Future mesh operations should be expressed against the proxy rather
  than against stored polygons. An operation that manipulates
  `ComplexPolygon`s directly is bypassing both the topology layer and
  the invariants.
