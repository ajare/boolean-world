# ADR-0051: Named Mirror Portals expand alongside legacy loops

**Status:** Partially superseded by ADR-0052 (coexistence boundary removed)
**Supersedes in part:** ADR-0050 (authored ownership and minimum cycle size)

Following #481 and its first visible slice #484, a Portal is an independently named, permanently Layer-owned aperture with a stable ID and explicit target ID; a self target generates a singleton reflection cycle, rather than an authored singleton loop. During expansion, legacy authored loops retain their existing schema and behavior, while named Portals use a separate monotonic allocator and persistence array; binary version 4 appends these fields and retains readers for versions 1–3.

Mirror resolution retains the existing wall coverage, front frame, and player-clearance rules. The canonical mapping reflects tangent/normal components without changing elevation; views use accumulated camera parity for winding and retain the existing recursive planner and budgets. Singletons never enter Liquid adjacency or its diagnostic graph.

## Expansion boundary

This slice supports self targets only. General target editing, migration away from legacy loops, reflected player traversal, and reflected Torch/light paths remain subsequent tickets. Mirror collision remains an ordinary wall until traversal is implemented.

During coexistence, transitional consumer keys use the reserved, unallocatable legacy loop ID `~0u` to mean an independently authored Portal; the endpoint field then contains its stable Layer-local Portal ID. Selection, picking, undo, render buckets, and generated lookup retain that authored identity, never a generated cycle position. Legacy loop IDs and Portal IDs may therefore overlap without aliasing.
