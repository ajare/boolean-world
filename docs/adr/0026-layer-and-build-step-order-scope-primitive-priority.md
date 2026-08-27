# ADR-0026: Layer and build-step order scope Primitive priority

**Status:** Accepted
**Date:** 2026-08-25
**Supersedes:** ADR-0001 and ADR-0021

World generation folds selected Layers in World order and each Layer's enabled LayerBuildSteps in recipe order. A Primitive's authored 0–255 priority orders output only within its owning step; composite steps may add local phases before that priority. PrefabField therefore keeps its seven nested-grid phases local to each field instead of reserving global priorities 249–255. This permits multiple ordered PrefabFields and lets later steps fold after them, at the cost of removing global priority interleaving across Layers and steps.
