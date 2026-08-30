# Emboss presets are a single global catalog

**Status:** Accepted
**Date:** 2026-08-30
**Supersedes:** ADR-0023's decision that Embossing belongs to a Sub-material

Embossing is independent of a procedural Technique and must be reusable across ProcMaterial catalogs, while each Primitive surface may apply it independently of its Sub-material. A single global Embossing catalog owns named Emboss presets; floor, ceiling, and wall each reference one preset by stable id, or none. This deliberately avoids putting presets in ProcMaterial, which would duplicate shared relief definitions across catalogs.

The World YAML and binary formats make a hard break: a surface now carries its own Emboss-preset reference and old levels are not accepted. `world-test-1.yaml` alone is migrated once, preserving each old Sub-material's Embossing by creating and assigning a matching preset.
