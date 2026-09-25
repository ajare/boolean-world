# ADR-0052: Portals have only Layer-local identities and explicit targets

**Status:** Accepted
**Date:** 2026-09-25
**Supersedes in part:** ADR-0050 (authored ordered loops, authored loop and endpoint identities, minimum-two-endpoint rule, persistence, and Liquid arbitration identity)
**Supersedes in part:** ADR-0051 (temporary coexistence with authored loops)
**Relates to:** ADR-0005, ADR-0009, ADR-0013, ADR-0019

## Context

ADR-0050 made a Portal loop an authored ordered container of two or more
endpoints. That model supports directed cycles, but an aperture cannot be named,
targeted independently, or used immediately as a mirror. Authors must edit an
ordering even though every runtime consumer needs only a source and its
explicit destination.

ADR-0051 introduced independently named self-targeting Portals alongside the
legacy model so reflection could be implemented without destabilising existing
content. Keeping both authored models would leave two identity namespaces and
two ways to define routing. Selection, undo, rendering, traversal, lighting,
Liquid, and persistence could then disagree about which relationship is
authoritative.

## Decision

A **Portal** is the sole authored object. It is permanently owned by one Layer
and contains a stable, never-reused Layer-local Portal ID, a display name, one
authored aperture, and the stable ID of its **Portal target** on the same Layer.
Names are trimmed, non-empty, case-insensitively unique within the Layer, and
are labels rather than references. New Portals receive the lowest available
`Portal N` name and target themselves.

A **Portal loop** is generated, never authored. Generation partitions the
same-Layer target graph into weakly connected components. A component becomes a
loop only when every member has exactly one incoming target; following targets
must visit every member once and return to the smallest member ID. Chains and
branches remain valid editable and serializable authored states, but their
component is inactive and diagnosed. Unrelated valid components remain active.
Generated loops and diagnostics are ordered by Layer ID and smallest member
Portal ID.

A self target forms a one-Portal **Mirror Portal**. Its canonical mapping is a
true reflection across the resolved aperture plane: tangential point and vector
components are preserved, normal components are negated, elevation and
World-up are preserved, and handedness is reversed. Rendering accumulates that
parity for winding, culling, clipping, projective sampling, and shadows. Player
position, yaw, horizontal velocity and unconsumed movement, Torch placement,
light paths, and shadow paths all consume the same mapping. Existing recursion,
view, light-path, and traversal budgets bound repeated reflections.

Every inferred loop retains ADR-0050's all-or-nothing geometric activation.
Each aperture must resolve against visible wall coverage in the same immutable
Arrangement snapshot, all members must have equal authored height and adequate
player clearance, and resolved apertures narrow to the smallest authored width
without mutating authored dimensions. Selected Layers include or exclude each
inferred loop atomically. Runtime lookup, selection, undo, rendering, traversal,
and lighting use `(Layer ID, Portal ID)`; no authored loop ID, reserved loop
namespace, loop-local endpoint ID, fixed endpoint slot, or authored traversal
order remains.

Multi-Portal cycles retain ADR-0050's directed Portal liquid-adjacency,
source-Sill gating, destination-bottom-relative elevation mapping, loop-atomic
conflict rejection, and deterministic fixed-point settlement. Liquid trials are
ordered by Layer ID and the smallest member Portal ID. A Mirror Portal creates
no Portal liquid-adjacency and no Liquid-specific self-edge diagnostic.

Deleting a Portal resets every surviving Portal that targeted it to target
itself. The deletion and resets are one authoring transaction; the remainder of
a former cycle is not reconnected automatically.

Legacy pair and ordered-loop compatibility lives only in deserialization. For
each serialized Layer, visit old loops in serialized order and apertures in
explicit traversal order (the established endpoint order for pairs). Allocate
fresh monotonic Layer-local IDs, preserve every aperture, and target the next
migrated member, wrapping the cycle. Transitional files may already contain
independent Portals: preserve them and their allocator, skip their
case-insensitive names when choosing `Portal N`, and reject exhausted allocators
rather than wrapping. Old loop and endpoint IDs are validation inputs, not
retained identities.

New YAML and binary version 5 save only named Portals. Binary versions 1–4
remain readable. Empty keyed Worlds still omit optional Portal fields. Undo uses
the same current schema as file persistence, without migration exceptions.

## Consequences

- A new Portal is immediately useful as a Mirror Portal once its aperture
  resolves.
- Authors build directed routes by choosing named same-Layer targets, not by
  manipulating an ordered container.
- Temporary invalid graphs are losslessly editable while generated behaviour
  remains deterministic and isolated by component.
- One canonical mapping governs views, traversal, Torch behavior, lighting, and
  shadows, including reflection handedness.
- ADR-0050 remains in force for generated-cycle aperture resolution, recursion
  budgets, canonical multi-Portal transforms, and directed Liquid semantics;
  its authored ordered-loop and minimum-two-endpoint decisions are replaced.
- ADR-0051 remains historical evidence for the reflection expansion; its dual
  authored models and reserved transitional identity namespace are removed.
- Cross-Layer targets, arbitrary branching runtime routes, decorative
  unresolved mirrors, and Mirror Portal Liquid transport are not introduced.
