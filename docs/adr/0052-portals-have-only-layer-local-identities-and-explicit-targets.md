# ADR-0052: Portals have only Layer-local identities and explicit targets

**Status:** Accepted
**Supersedes in part:** ADR-0050 (authored ownership, identity, persistence and Liquid arbitration order), ADR-0051 (coexistence boundary)

Following #490, named Portals and explicit same-Layer stable-ID targets are the sole authored model. Portal loops exist only as inferred generated cycles. Runtime lookup, selection, undo, rendering, traversal and lighting use `(Layer ID, Portal ID)`; no reserved loop namespace or loop-local endpoint identity remains. Liquid trials retain atomic directed-cycle semantics and arbitrate by Layer ID then smallest member Portal ID.

Legacy pair and ordered-loop compatibility lives only in deserialization. For each serialized Layer, visit old loops in serialized order and apertures in explicit traversal order (the established endpoint order for pairs). Allocate fresh monotonic Layer-local IDs, preserve every aperture, and target the next migrated member, wrapping the cycle. Transitional files may already contain independent Portals: preserve them and their allocator, skip their case-insensitive names when choosing `Portal N`, and reject exhausted allocators rather than wrapping. Old loop and endpoint IDs are validation inputs, not retained identities.

New YAML and binary version 5 save only named Portals. Binary versions 1–4 remain readable. Empty keyed Worlds still omit optional Portal fields. Undo uses the same current schema as file persistence, without migration exceptions. This contracts the temporary dual model instead of perpetuating adapters whose identities could disagree between consumers.
