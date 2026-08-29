# World resources are host-resolved before deserialization

**Status:** Accepted

An authored World serializes the exact sorted set of Willpower Resource names referenced by all of its content, while `core::World` remains resource-system agnostic. The host reads that dependency header first, resolves and retains the resources through Willpower before fully deserializing the World, and rendering consumes the already-loaded resource—wall normal maps therefore use `ImageResource`'s MPP texture rather than a custom file decoder or texture cache. This leaves ADR-0030's ownership of a wall normal-map override on an authored External edge unchanged; only the override's image identity and loading boundary change.
