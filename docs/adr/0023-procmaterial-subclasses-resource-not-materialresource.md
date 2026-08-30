# ProcMaterial subclasses Resource directly, not MaterialResource

**Status:** Superseded in part by ADR-0035 (Embossing ownership only)
**Date:** 2026-08-25

Willpower already ships `wp::application::resourcesystem::MaterialResource` —
the obvious base class for anything named "material". It wraps exactly one
`Program` dependent resource and a set of texture samplers into an
`mpp::ProgrammaticBasicMaterialStream`; its `load()`/`unload()` do real
GPU-resource work through `mpp::RenderSystem`. ProcMaterial needs none of
that: it holds two program *references* (the 3D and 2D world shaders
`WorldRenderer3d` already binds directly), a Technique schema per
procedural Technique, and a named catalog of Sub-materials — parameter
values, not a GPU resource. Every custom resource type boolean-world has
added before this (`Map`, `ProtoEntity`) subclasses `Resource` directly, for
the same reason: their data didn't fit an existing willpower resource
type's loading behaviour either.

## Decision

`ProcMaterial : public wp::application::resourcesystem::Resource`, with its
own `ProcMaterialResourceFactory` and `ProcMaterialResourceDefinitionFactory`,
registered in `DLL.cpp` alongside `Map` and `ProtoEntity`. It performs no
GPU-resource loading in `create()`/`load()` — `WorldRenderer3d` resolves and
binds the referenced 3D/2D program names itself, exactly as it does today
for `world_pbr.frag`/`world_pbr_2d.frag`.

## Considered alternatives

**Subclass MaterialResource, as first proposed.** The name matches, but
`load()`/`unload()` would need overriding wholesale to fetch two programs
and skip the single-texture-stream machinery — inheriting the class in name
only, while carrying an unused single-`Program` assumption and
texture-definition machinery ProcMaterial has no use for.

## Consequences

- The Technique dispatch (the `switch(materialIndex)` in `world_pbr.frag`/
  `world_pbr_2d.frag`, and the duplicate `supernaturalEmission` if-chain)
  stays a hand-maintained GLSL construct; ProcMaterial only supplies the
  data that names and bounds each Technique, not the dispatch itself. A
  reader expecting the shader switch to become data-driven by this change
  will be surprised — it deliberately doesn't.
- `common::MaterialRegistry.h`'s authorable content (names, param defs,
  min/max/default, base colour) and Game.yaml's `Materials:` override
  section are retired; only a minimal compiled Technique index→name table
  remains, as the single source the shader switch and ProcMaterial's
  Technique schemas both agree with.
