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
once when the preview opens (scoped to the same in-scope Primitive
list already computed for the old preview), and renders it through the real
`WorldRenderer`/`WorldRenderer3d`/`mpp::Scene`/`mpp::RenderPipeline` stack —
the same code path Launcher.exe uses, including bloom, tonemap, and ambient
occlusion. This requires the editor process to construct its own
`mpp::RenderSystem`, `mpp::ResourceManager`, and
`wp::application::resourcesystem::ResourceManager` against its existing SDL/GL
context. These are constructed once during editor startup, immediately after
the GL context, and live for the rest of the process: only one instance may
ever exist per process, and its shader compilation and manifest scan are
one-time costs better paid before the first frame than as a stall on the
first preview open.

The preview renders into the editor's world viewport — the dockspace's
central node, the same region the 2D level geometry is edited in — standing
in for the `World` window rather than opening a near-fullscreen window of its
own. It is therefore no longer input-blocking: the panels around it stay
visible, and its own surface picking and Sub-material authoring stay live —
the picked surface's editor is a `Selected surface` collapsible header in the
left `Editing` panel, not a floating window. Because the preview still holds
direct pointers into the open World's Primitives and draws an Arrangement
built once when it opened, everything around it is frozen while it is open:
the toolbar's preview toggle and that one header are the only live controls,
and the panel's own editing headers are hidden rather than greyed out, so the
document cannot be restructured underneath it. Sharing the window means
sharing the pointer: right-drag turns the camera (grabbing the pointer
only for the duration of the drag), left-click picks the surface under the
cursor, and Escape closes the preview. Surfaces are marked by their own border
— a wall's quad, or a floor/ceiling face's clipped polygon including its
holes — drawn as a wireframe in a small raw-GL pass over the pipeline's
finished image with depth testing off, rather than by tinting the material
underneath: yellow for the surface under the pointer, red for the selected one,
which keeps its border for as long as it stays selected.

Assigning the selected surface a Sub-material writes to the Primitive that
owns the Arrangement polygon the surface belongs to: the one that won the fold
there, whose property set the polygon carries, whatever else overlaps it. A
floor or ceiling names its polygon through the triangle that was picked; a
wall names the side of its edge that gave it its extent � the solid side of a
border, the lower side of a floor step, the higher side of a ceiling step.
That owner is identified by its position in the Primitive list the Arrangement
was built from, never by `Primitive::getId()`: an id is a Primitive's index
within its own Layer, so in a World of several Layers the same id names one
Primitive per Layer. Because the assignment moves the surface between mesh
buckets � which are keyed by Sub-material id and baked when the scene is
built � an explicit pick rebuilds both the Arrangement snapshot and the render
scene, while parameter, colour and emboss edits keep the uniform-only draft
path.

A selected floor or ceiling can also be moved from inside the preview:
Shift+Up/Down nudges it by eight units, Ctrl+Shift+Up/Down by one, on the
Primitive resolved the same way, and Shift therefore takes the arrow keys away
from the camera for as long as it is held. Neither surface may pass the other,
so a nudge is clamped at the opposing surface and one with nowhere left to go
opens no transaction at all. A moved surface changes the Arrangement's own
geometry, so it rebuilds the snapshot and the scene exactly as an assignment
does. Walls are not nudgeable: a wall has no height of its own, only the gap
between the two polygons it stands between.

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
- The 3D preview displays via `ImGui::Image` from the render pipeline's
  offscreen output texture, replacing the previous
  scissor-rect-into-the-backbuffer approach — which is what lets it sit in
  the world viewport as ordinary window content.
- Editor startup now pays the render system's shader compilation and manifest
  scan (several seconds in a Debug build) before the first frame. A failure
  there is non-fatal: the editor runs on, and the preview reports that it
  cannot be rendered.
- Live-reflecting edits made while the preview is open remains future work.
  Until then the surrounding panels are disabled rather than the preview's
  Arrangement being rebuilt per edit.
- GitHub issue #256 is superseded by this ADR and closed once
  `PreviewMaterialProgram.*`/`PrimitivePreviewGeometry.*` are deleted.
