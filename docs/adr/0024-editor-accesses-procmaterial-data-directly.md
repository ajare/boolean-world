# The editor reads and writes ProcMaterial data directly, not through ResourceManager

**Status:** Superseded by ADR-0025
**Date:** 2026-08-25
**Related:** ADR-0023 (ProcMaterial subclasses Resource, not MaterialResource)

> **Note (ADR-0025):** only this ADR's stated *reason* — that the editor's 3D
> preview deliberately does without `mpp::RenderSystem`/`ResourceManager` — is
> superseded; ADR-0025 has the editor construct those for real-pipeline 3D
> preview rendering. The *decision* below (ProcMaterial authoring goes through
> `core::Serializer` directly, never through `ResourceManager`) is unchanged
> and still in effect.

The editor needs to both load ProcMaterial resources (for the wall/floor/
ceiling material picker) and author them (create/rename/delete Sub-materials,
edit their parameters, save back to disk) — a read/write need no existing
editor-facing system satisfies. willpower's `ResourceManager`/`Resource`
lifecycle is read-only end to end: `Resource`'s virtual hooks (`create`,
`load`, `parseData`) all run disk-to-memory, `ResourceDefinitionFactory::
create()` only consumes a `DataNode`, and `DataNode` itself has no
mutators — there is no save path anywhere in the willpower layers
boolean-world links against. Standing up `ResourceManager` in the editor at
all would also mean constructing an `mpp::RenderSystem`, which the editor's
3D preview deliberately does without — it drives raw GL directly instead of
the game's `WorldRenderer3d`/`mpp::Scene` stack (see
`PreviewMaterialProgram.h`'s doc comment, GitHub issue #256).

## Decision

The editor loads and saves ProcMaterial resource files directly with
`bw::core::Serializer`, the same mechanism it already uses for World/Layer
files — never through `ResourceManager`. ProcMaterial follows the `Map`
resource pattern: a `TextFile` dependent resource pointing at a separate
YAML file, whose content both the editor (via `core::Serializer`, for
authoring) and the runtime (via `ResourceManager`, for loading into the
game) parse the same way. `Resources.yaml` and `app/resources/` stay the
single location for ProcMaterial files; the editor writes into that tree
rather than a separate editor-owned location.

## Considered alternatives

**Reintroduce ResourceManager into the editor for reading, hand-roll a
separate save path.** Would reverse the direction of issue #256's decision
to keep the editor's preview independent of the game's render/resource
stack, and still wouldn't solve saving — willpower's resource system has no
write path to build on regardless of which way loading goes.

## Consequences

- The editor never depends on `mpp::RenderSystem` or `ResourceManager`; a
  future editor feature that wants either is choosing to cross a boundary
  this ADR and issue #256 both draw deliberately, not stumbling into a gap.
- Editor and runtime independently read the same ProcMaterial YAML shape
  with two separate `core::Serializer` call sites rather than one shared
  parse path — the same trade `Map`'s files already make.
