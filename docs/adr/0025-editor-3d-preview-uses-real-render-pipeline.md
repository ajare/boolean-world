# ADR-0025: The editor's 3D preview renders through the real WorldRenderer3d/mpp::Scene pipeline

**Status:** Accepted
**Date:** 2026-08-26
**Supersedes:** ADR-0024's stated reason for avoiding `mpp::RenderSystem` (the decision itself — ProcMaterial authoring via `core::Serializer` — is unchanged)
**Related:** ADR-0023 (ProcMaterial subclasses Resource, not MaterialResource), ADR-0024 (the editor accesses ProcMaterial data directly)

## Context

The editor's 3D material preview (`Preview3D.cpp`) built its own geometry
per-Primitive (`PrimitivePreviewGeometry.cpp`) with no boolean/Arrangement
processing, and drew it with a hand-rolled raw-GL program
(`PreviewMaterialProgram.cpp`) that deliberately bypassed the game's
`WorldRenderer3d`/`mpp::Scene`/`RenderPipeline` stack — a choice recorded in
`PreviewMaterialProgram.h`'s doc comment and tracked as GitHub issue #256, and
cited as ADR-0024's reason for keeping the editor free of `mpp::RenderSystem`/
`ResourceManager` entirely.

That divergence produced two confirmed, diagnosed mismatches against
Launcher.exe when previewing a multi-Primitive Prefab (e.g. Prefab1 in
`world-test-1.yaml`):

- Wall segments rendered at the *editing Primitive's own* `floorZ`/`ceilingZ`,
  not the real neighbour-aware stepped heights the boolean Arrangement
  computes — walls read the wrong height/scale next to a Prefab's other
  Primitives.
- Both wall faces rendered as one flat, double-sided quad, unlike the game's
  single-sided, player-facing wall selection.

Patching `PrimitivePreviewGeometry` to approximate the Arrangement's rules
would leave two independent implementations of the same wall/floor/ceiling
tessellation logic to keep in sync by hand — exactly the kind of drift that
caused the mismatches above in the first place. The alternative is to stop
re-deriving that logic in the editor and render through the same code the
game uses.

## Decision

The editor's 3D preview now builds a real `bw::core::ArrangementWorldData`
once when the preview window opens (scoped to the same in-scope Primitive
list already computed for the old preview), and renders it through the real
`WorldRenderer`/`WorldRenderer3d`/`mpp::Scene`/`mpp::RenderPipeline` stack —
the same code path Launcher.exe uses, including bloom, tonemap, and ambient
occlusion. This requires the editor process to construct its own
`mpp::RenderSystem`, `mpp::ResourceManager`, and
`wp::application::resourcesystem::ResourceManager` against its existing SDL/GL
context, long-lived for the editor process once first constructed.

`WorldRenderer`/`WorldRenderer3d`/`WorldBatch`/`WorldTriangle3dDataProvider`/
`WorldWallOrientation`/`SubMaterialResolver`/`ProcMaterial` move into a shared
`BooleanWorldRender` static library linked by both the game and the editor,
rather than being duplicated or hand-copied.

`ProcMaterial` *authoring* (create/rename/delete Sub-materials, edit
parameters, save) is unaffected by this change and continues exactly as
ADR-0024 describes: direct `core::Serializer` reads/writes against the YAML
files under `app/resources/`, with no save path through `ResourceManager`.
The render-side `SubMaterialResolver`/manifest `ResourceManager` this ADR adds
is a second, independent, read-only representation of the same on-disk
`ProcMaterial` data — after a Sub-material save while a preview is open, the
render side's cached resources are explicitly reloaded so the two stay in
sync; nothing here gives `ResourceManager` a write path.

## Considered alternatives

**Keep the raw-GL preview, but feed it Arrangement-derived geometry instead
of `PrimitivePreviewGeometry`'s per-Primitive extrusion.** Fixes the wall-
height and single-sided-wall mismatches without reopening ADR-0024/#256, and
is a smaller, lower-risk change. Rejected because it still leaves a second,
hand-maintained rendering implementation (bloom/tonemap/AO, uniform wiring,
mesh batching) that can drift from the game's again in ways this ADR's
alternative can't — a `world_pbr.frag` divergence, a batching bug, a new
post-effect — the exact class of bug this decision exists to close off for
good, not just for wall heights specifically. Discussed with the project
owner and explicitly declined in favour of the full-pipeline option below.

**Full `WorldRenderer3d`/`mpp::Scene`/`RenderPipeline` stack (chosen).**
Larger, riskier change — new GL resource lifecycle management inside the
ImGui-hosted editor window, a second `mpp::RenderSystem` bootstrap sequence to
maintain, and a shared-library extraction to keep the editor and game
building the same code. In exchange, the preview and the game share one
rendering implementation end to end: any future shader, batching, or
post-effect change is automatically reflected in the editor preview with zero
additional editor-side work, which was judged worth the larger surface area.

## Consequences

- The editor now depends on `mpp::RenderSystem`/`ResourceManager` — the
  boundary ADR-0024 and issue #256 previously drew is gone. Any future editor
  feature can build on this bootstrap rather than avoiding it.
- `WorldRenderer`/`WorldRenderer3d` and friends live in a shared
  `BooleanWorldRender` library instead of being `app`-only; changes to that
  library now need to keep working for both the game and the editor's preview
  (e.g. no `applib`/`DLL`-specific dependencies may leak into it).
- Two independent, read-only-vs-authoring representations of `ProcMaterial`
  data coexist in the editor process (the direct-file `ProcMaterialLibrary`
  for authoring, the manifest-driven `ResourceManager`/`SubMaterialResolver`
  for rendering) — a Sub-material save must explicitly reload the render
  side's cached copy, or the preview will show a stale value until reopened.
- The 3D preview window displays via `ImGui::Image` from the render
  pipeline's offscreen output texture, replacing the previous
  scissor-rect-into-the-backbuffer approach — a better fit for ImGui hosting
  as a side effect of this change, not its goal.
- GitHub issue #256 is superseded by this ADR and closed once
  `PreviewMaterialProgram.*`/`PrimitivePreviewGeometry.*` are deleted.
