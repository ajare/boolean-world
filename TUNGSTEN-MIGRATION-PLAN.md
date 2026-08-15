# Adopting the tungsten-oxide stack in BooleanWorld

Target: bring `boolean-world` onto the same Willpower / AppLib / Launcher /
MassivePolyPusher code as `D:\Code\Projects\tungsten-oxide`, and keep the
BooleanWorld game working on it.

Reference implementation: **`cpp/tungsten-monoxide`**. It is an AppLib game with
the same shape as BooleanWorld's `app/` — `Game`, `Map`, `ProtoEntity`,
`StatePlay*`, `EntityHandler*`, `ReactiveCamera`, `GameDefinitionFactory` — already
running on the new stack. Every question of "what should this look like
afterwards" has an answer there.

---

## 1. The headline: this is smaller than it looks

The scary-sounding parts (material rework, render graph, XML→YAML) turn out to
be well-bounded, and the analysis below is what makes that safe to claim.

**Of the 23 MPP headers BooleanWorld uses, exactly one was deleted**:
`ProgrammaticMaterialStream.h`. Its replacement,
`ProgrammaticBasicMaterialStream.h`, is a method-for-method copy with the
`quality` parameter dropped. All nine `RenderSystem` methods BooleanWorld calls
still exist. `Scene`, `ModelRenderParams`, `ResourceManager`, `Colour`,
`SceneModel2d`, `UniformCollection`, `ResourceWrangler` changed only additively.

**The render graph is not on the critical path.** `RenderPipeline` still exists,
and BooleanWorld is a flat-shaded 2D game — it wants `BasicMaterial`, not
`PbrMaterial`. The PBR pipeline, `.mpppackage`, `PbrMaterialBinding` and the
whole PipelineEditor workflow can be skipped for the port and adopted later as
a deliberate visual-quality project. **This single scoping decision removes most
of the risk.**

**XML→YAML is one file.** `app/resources/Resources.xml` (260 lines). The world
files (`gen-1.yaml`, `stress-test-1.yaml`, …) are already YAML. The other 34
`.xml` files in the tree are FMOD Studio project metadata that our code never
reads. And the YAML maps 1:1 onto the XML — elements become keys, attributes
become keys, repeated elements become lists.

What is genuinely large is the **dependency topology change** and the
**mechanical `XmlNode*` → `DataNode*` cutover**, which has to happen across
three libraries at once because it changes virtual signatures.

---

## 2. Component-by-component delta

### MassivePolyPusher — replaced wholesale

| | now | after |
|---|---|---|
| origin | bitbucket `massivepolypusher` | github `ajare/massive-poly-pusher` |
| path | `ext/MassivePolyPusher` | `ext/massive-poly-pusher` |
| build | MSBuild `.sln` | its own CMake; consumed as a *build tree* |
| deps | `vendor/` prebuilt libs | FetchContent (GLEW, SDL3, assimp, yaml-cpp, ImGui) |

New modules: `mpp-app-support`, `mpp-data`, `pipeline-editor`, `tools`.
Removed from `mpp/`: `MaterialSpecification`, `MaterialStream`,
`ProgrammaticMaterialStream`, `PostEffect`, `PostEffectStream`, `Particle`,
`ParticleSystem`. Added: 46 headers — the `Basic`/`Pbr`/`PostEffect` material
triad, `RenderGraph*` (14), `Scene*`, `PbrPipeline*`, and
`LegacyMaterialConversion.h`.

`Material` is now abstract (`getShadingModel()`, `isTransparent()`,
`isDoubleSided()`, `validateInstanceUniforms()`); concrete assets are
`BasicMaterial` or `PbrMaterial`.

`Batch` gained a buffer index: `getCount()` → `getCount(0)`,
`getAttributeData("POSITION")` → `getAttributeData(0, "POSITION")`. This is why
`QuadBatchRenderer.h` and `TriangleBatchRenderer.h` differ; they update with the
submodule, but BooleanWorld's own `WorldBatch`/`DynamicRenderer` call the same
API and must follow.

`ModelRenderParams` now defaults `Flag_CastShadows` **on**. Harmless for a 2D
game, but it is a behaviour change, not just an addition.

### ext/Utils — deleted as a top-level submodule

`utils/StringUtils.h`, `utils/XmlReader.h`, `utils/FileSystem.h` and the new
`utils/YamlReader.h` now come from **MPP's nested `ext/utils`**. BooleanWorld has
53 `#include <utils/…>` sites; they keep working, only the include path's origin
changes. Note `willpower.common` also carries its *own* `StringUtils.h` — a
different file. Don't conflate them.

### willpower.common — adopt tungsten-oxide's wholesale

TO's is a **strict superset**: BooleanWorld has no header TO lacks. Only four
shared headers differ (`Platform.h`, `StackWalker.h`, `WillpowerWalker.h`,
`Exceptions.h`) and `Platform.h`'s 78-line diff is **entirely clang-format
reformatting** — no semantic change.

New and load-bearing: **`DataNode.h`** and **`StructuredData.h`** — the format
abstraction that replaces `utils::XmlNode`. `DataNode` deliberately mirrors
`XmlNode`'s shape (`getChild`, `getOptionalChild`, `getProperty`,
`getOptionalProperty`, `next`), which is what makes the factory cutover
mechanical rather than a rewrite.

Also arriving: `FileSystem.h`, `Globals.h`, `ExtendedAccelerationGrid.h`,
`TriangleIntersection.h`, `StringUtils.h`, `XmlReader.h`, `tinyxml2.h`.

This re-introduces files pruned in the earlier BooleanWorld extraction
(`BatchRenderable`, the spline family, `DynamicAccelerationGrid`,
`polypartition`, `DateTime`). Take them; re-prune at the end if desired.

### willpower.geometry — adopt tungsten-oxide's

Identical modulo formatting. TO restores the pruned filter/CSG files. BooleanWorld's
only unique file is `SerializerMeshChunk.h`.

### willpower.serialization — delete

`SerializerMeshChunk` is its only route into the codebase, and **nothing in
`Applications/`, `AppLib/` or `Launcher/` references it**. TO dropped the module
for the same reason. Delete both, exactly as the earlier extraction proved safe.

### willpower.application — adopt TO's, it is a superset

TO's headers are a strict superset (BooleanWorld has none unique). It restores
`Document`, `DocumentManager`, `InputHelper`, `Scheduler`, `SchedulerTask` and
adds `resourcesystem/DirectoryResourceLocation.h` — **moved out of Launcher**.

The real change is every definition-factory signature:

```cpp
- void create(Resource*, ResourceManager*, utils::XmlNode* node)
+ void create(Resource*, ResourceManager*, wp::DataNode* node)
```

### willpower.collide / firepower / viz — BooleanWorld keeps them, no TO counterpart

TO has none of these. They stay and must be ported.

`willpower.viz` is the one to watch: **36 files, 3,409 lines**, and BooleanWorld's
whole rendering path. Its MPP coupling is `mpp::mesh` (126), `ModelRenderParams`
(47), `ResourceManager` (46), `Colour` (46), `helper` (24), `QuadBatchOptions`
(24), `Scene` (20), `SceneModel2d` (17) — **all retained**. The only breaking
touch point is `ProgrammaticMaterialStream` at 7 call sites across 5 files.

### AppLib — merge, do not replace

TO's applib is **BooleanWorld's applib with the shooter features stripped**, plus
three `PbrMaterialBinding*` headers. BooleanWorld has 22 headers TO lacks:
`Beam*`, `Bullet*`, `Weapon`, `Battery`, `VisualSprite*`, `VisualTriMesh*`,
`AnimationDatabase`, `MapTiled*`, `ImageSetTiled*`, `PlayObjectCreators`,
`GeometryMeshRendererFactory`, `EntityFacadeRenderOptions`.

21 of 34 shared headers differ, and the diffs decompose into exactly three
things: clang-format reformatting, `XmlNode*`→`DataNode*`, and removal of
BooleanWorld-specific features. So: **keep BooleanWorld's AppLib and apply TO's
delta to it.** Replacing wholesale then re-adding 22 headers would be strictly
more work and would silently drop game features.

### Launcher — near-identical

Only `ApplicationDLL.h` (78 lines) and `ImGuiDataProvider.h` (8) differ.
`DirectoryResourceLocation` moves into `willpower.application`. TO dropped the
**GLFW backend** (`src/glfw/`), keeping SDL only. Config becomes
`Launcher.yaml`, with a new `PbrPackage` argument.

---

## 3. Decisions (locked)

1. **No PBR. `BasicMaterial` only.** BooleanWorld's materials are *procedural,
   done in shaders* — every one of the 7 material sites builds a `Program`
   resource from `world.vert`/`world.frag` and binds it with
   `setProgram(name)`. `ProgrammaticBasicMaterialStream::setProgram(std::string
   const&)` exists unchanged, and `BasicMaterialSpecification` is the old
   `MaterialSpecification` renamed and reformatted. Verified: **the `quality`
   parameter is never passed a non-default value anywhere in BooleanWorld**, so
   its removal costs nothing. PBR, `.mpppackage`, `PbrMaterialBinding` and
   PipelineEditor are all out of scope.

2. **Adopt tungsten-oxide's `.clang-format`.** Done as its own mechanical
   commit per library, before the semantic work, verified by diffing DLL
   exports before and after.

3. **SDL3 for `editor` and `floored`.** See the cost below — this is the one
   decision that grew, and it drags an ImGui upgrade with it.

4. **Keep Launcher's GLFW backend.** MPP does **not** ship GLFW (its `ext/` is
   assimp, glew, imgui, sdl, utils), so `vendor/` must retain `glfw3` headers
   and library. Everything else in `src/glfw/` stays as-is.

5. **`vendor/` after the migration.** SDL3, GLEW, yaml-cpp, ImGui, assimp and
   Utils now come from MPP's build tree — drop those. Keep spdlog, clipper2,
   mapbox, entt, rapidhash, nlohmann, inifile-cpp, concurrencpp, **glfw3**, plus
   link-only gtest, nfd, FreeImage, fmod, PerformanceAPI, fmt.

### What decision 3 actually costs

`imgui_impl_sdl3` requires ImGui ≥ 1.90. BooleanWorld's `editor` and `floored`
are on **1.89.5**; `app` and `Launcher` are on 1.92.0. So SDL3 forces
**1.89.5 → 1.92.x** for those two apps, and 1.89→1.92 is not a free upgrade
(font-atlas and draw-list changes in 1.92 especially).

Worse, `editor` and `floored` carry **`imnodes`, `imgui-knobs` and `implot`**,
and tungsten-oxide's editor carries **none of them** — it is plain ImGui + SDL3
+ OpenGL3. So there is no reference version to lift for the two riskiest addons.
`implot` can be taken from TO's launcher (1.92.0); `imnodes` and `imgui-knobs`
must be upgraded and tested by us.

**This is the least predictable part of the whole migration.** It is also,
fortunately, completely separable — see below.

---

## 3a. The codebase splits into two independent halves

This is the most useful structural fact found in the analysis, and it shapes the
whole plan:

- **`core` is MPP-free.** It includes `willpower/common/` and nothing else from
  Willpower, and no `mpp/` header at all.
- **`editor` and `floored` touch no `mpp/`, `applib/`, `willpower/viz/`,
  `willpower/application/`, `willpower/collide/` or `willpower/firepower/`.**
  They link `core` plus vendor libraries, full stop.

So:

| Half | Targets | Touched by |
|---|---|---|
| **A — engine** | `Launcher` → `BooleanWorld.dll` → `AppLib` → `willpower.application`/`viz`/`collide`/`firepower` → MPP | the entire MPP swap, DataNode cutover, YAML, material port |
| **B — tools** | `editor`, `floored`, `experiments`, `profiler`, `core-dll` → `core` → `willpower.common` | only the `willpower.common` swap (a strict superset), plus the independent SDL3/ImGui work |

**The SDL3 + ImGui 1.92 + imnodes work does not block the game, and the engine
migration does not block the tools.** If imnodes fights back, the game still
ships. Phase 6 is therefore sequenced last and can slip without consequence.

---

## 4. Implementation plan

Phases 2–5 are a **single interlocked cutover** — the `XmlNode*`→`DataNode*`
change alters virtual signatures, so `willpower.application`, `AppLib` and the
BooleanWorld app must move together. Do not plan on a green build between 3 and 5.
Everything else is independently verifiable.

### Phase 0 — Baseline and formatting (independently verifiable)

1. Confirm the current build is green (`RebuildAll.bat Release`, Launcher reaches
   `Entering state: Play`) and tag it.
2. Copy TO's `.clang-format` to the repo root. Run it over `Willpower/`,
   `AppLib/`, `Launcher/`, `Applications/BooleanWorld/` — **excluding vendored
   ImGui / ImPlot / imnodes / imgui-knobs**, which must stay upstream-formatted
   so they can still be diffed against their origins. One commit per library,
   rebuild after each.
3. Prove it was cosmetic: `dumpbin /exports` on every Willpower DLL and AppLib
   before and after must be identical. (This exact check caught a real `/sdl`
   codegen difference during the CMake conversion, so it earns its place.)

### Phase 1 — Swap the engine dependency

1. Remove submodules `ext/MassivePolyPusher` and `ext/Utils`.
2. Add `https://github.com/ajare/massive-poly-pusher` at `ext/massive-poly-pusher`,
   pinned to the commit tungsten-oxide uses. Init recursively — its nested
   `ext/utils` is where `utils/…` now comes from.
3. Port `cpp/cmake/MppBuildTree.cmake` from TO into `cmake/`. It probes for
   `build/cmake` *or* `build`, which matters — the checkout here uses `build/`.
   Replace `cmake/Prebuilt.cmake`'s `ext::*` imported targets with it.
4. Build MPP standalone; confirm `MassivePolyPusher`, `MppHelper`, `MppMesh`,
   `MppProgram`, `MppData`, `MppAppSupport`, `Utils` libraries appear.
5. Prune the now-duplicated `vendor/` entries (decision 5).

**Verify:** everything still *configures*; compilation will fail until Phase 2.

### Phase 2 — willpower.common and willpower.geometry

1. Replace both modules with tungsten-oxide's copies wholesale.
2. Re-add `SerializerMeshChunk.{h,cpp}` **only if** Phase 3 keeps
   `willpower.serialization` — it should not. Delete `willpower.serialization`.
3. Update the CMakeLists to the new file sets.

**Verify:** `Willpower.Common` and `Willpower.Geometry` build and export the
expected symbols. These have no MPP coupling, so this should go clean.

### Phase 3 — The DataNode cutover (the interlocked one)

1. Replace `willpower.application` with tungsten-oxide's copy, including
   `resourcesystem/DirectoryResourceLocation.h`; delete Launcher's copy.
2. In `AppLib`, apply TO's delta to the 21 differing shared headers and their
   `.cpp`s: `utils::XmlNode*` → `wp::DataNode*`, `#include <utils/XmlReader.h>` →
   `#include "willpower/common/DataNode.h"`. **Preserve every BooleanWorld-only
   feature** — the bullet/beam/animation members that TO stripped from `Game.h`,
   `ProtoEntity.h`, `StatePlay.h` stay.
3. Apply the same signature change to BooleanWorld's own factories:
   `GameDefinitionFactory`, `MapBooleanWorldDefinitionFactory`,
   `ProtoEntityDefinitionFactory`.
4. Do **not** port `applib/PbrMaterialBinding*` — decision 1 excludes it.

**Verify:** whole tree compiles and links. This is the phase where a long red
build is expected — sequence it as one focused push.

### Phase 4 — Resource data to YAML

1. Convert `app/resources/Resources.xml` → `Resources.yaml`, following
   `cpp/tungsten-monoxide/resources/Resources.yaml`: elements and attributes
   both become keys; repeated `<Resource>` becomes a list; `<Namespace>` nests.
   260 lines — worth a throwaway script plus a read-through, not hand typing.
2. Convert the Launcher config to `Launcher.yaml` per
   `cpp/launcher/support/TungstenMonoxide.Release.yaml`, and update the CMake
   `file(GENERATE)` block that writes `BooleanWorld.cfg` to emit YAML instead.
3. Point the `ResourceLocation` at `definition: Resources.yaml`.

**Verify:** Launcher loads and reaches `Entering state: Play`. Resource-load
failures surface here as named missing resources, which makes this phase
self-diagnosing.

### Phase 5 — Port the renderers to the new material model

The materials stay procedural — this phase renames the stream, it does not
change how shading works.

1. `willpower.viz` (5 files, 7 sites) and `app/src/WorldRenderer3d.cpp` (1 site):
   `ProgrammaticMaterialStream` → `ProgrammaticBasicMaterialStream`,
   `MaterialSpecification` → `BasicMaterialSpecification`. No `quality` arguments
   to remove — none are passed.
2. `willpower.application/src/resourcesystem/MaterialResource.cpp`: same change.
3. Update direct `Batch` calls in `WorldBatch`, `WorldBatchRenderer` and
   `viz/DynamicRenderer` for the new buffer-index parameter
   (`getCount()` → `getCount(0)`, `getAttributeData(n)` → `getAttributeData(0, n)`).
4. Confirm `world.vert`/`world.frag` still compile against the new program
   pipeline, and check the built-in uniform/attribute names MPP injects
   (`_mpp_*`) have not been renamed — `mpp.log` lists every uniform and
   attribute it binds at startup, so this is directly observable.
5. `willpower.collide` and `willpower.firepower` have no MPP material coupling —
   expect formatting and include-path changes only.

**Verify:** the game renders. Load the same world file before and after and
compare screenshots — a wrong material binding shows as flat/black geometry,
not as a crash.

### Phase 6 — SDL3 and ImGui for editor/floored (independent; may slip)

Nothing in Half A depends on this. Do it after the game is green.

1. Upgrade `editor` and `floored` from ImGui 1.89.5 to 1.92.x. Take
   `imgui*`/`implot*` from TO's launcher (1.92.0) so all four BooleanWorld ImGui
   copies converge on one version — this also finally resolves the four-way
   ImGui duplication flagged in the original extraction.
2. Replace `imgui_impl_sdl2` with `imgui_impl_sdl3` from
   `cpp/launcher/src/imgui/imgui_impl_sdl3.cpp`.
3. Port `Main.cpp` in both apps from SDL2 to SDL3. ~39 SDL symbols each, of
   which roughly a dozen changed: `SDL_CreateWindow` (no x/y), `SDL_WINDOWPOS_*`,
   `SDL_WINDOW_ALLOW_HIGHDPI` → `SDL_WINDOW_HIGH_PIXEL_DENSITY`, `SDL_QUIT` →
   `SDL_EVENT_QUIT`, `SDL_WINDOWEVENT*` → discrete `SDL_EVENT_WINDOW_*`,
   `SDL_INIT_GAMECONTROLLER` → `SDL_INIT_GAMEPAD`, `SDL_INIT_TIMER` (gone),
   `SDL_GL_DeleteContext` → `SDL_GL_DestroyContext`. TO's editor `Main.cpp` is a
   working reference for all of it.
4. **`imnodes` and `imgui-knobs` against ImGui 1.92 — the risk.** No reference
   version exists in tungsten-oxide. Upgrade both from upstream and exercise the
   editor's node graph and knob widgets specifically. If `imnodes` proves
   incompatible, the fallback is to hold `editor` on SDL2/1.89.5 while `floored`
   (which has no imnodes) moves — they are separate targets and need not agree.
5. Link `vendor::sdl3` instead of `vendor::sdl2`/`sdl2main`; SDL3 comes from
   MPP's build tree.

### Phase 7 — Cleanup and re-prune

1. Re-run the unused-file analysis from the original extraction over the new
   `willpower.common`/`geometry`/`application` and prune what BooleanWorld does
   not reach.
2. Drop the superseded `vendor/` entries; keep `glfw3` (decision 4).
3. Update `MIGRATION-PLAN.md`, `README.md`, `RebuildAll.bat` — MPP now needs a
   configure+build step before the main build, as TO's README does.

---

## 5. Verification

Reuse the gate from the original extraction; it caught real defects:

- [ ] MPP builds standalone
- [ ] `Willpower.sln` equivalent: all modules build Debug + Release x64
- [ ] AppLib builds
- [ ] All seven BooleanWorld targets build
- [ ] `Launcher.exe` reaches `Entering state: Play` with **zero** resource errors
      in `LauncherLog.html` and `mpp.log`
- [ ] `editor.exe` and `floored.exe` launch
- [ ] `experiments` — compare against the **known 20 pre-existing failures**;
      any change is a regression introduced by this work
- [ ] `gen-world.py` runs against `core-dll.dll`
- [ ] fresh `git clone --recurse-submodules` builds and runs

Two pre-existing issues carry over and must not be mistaken for migration
damage: `editor`/`floored` do not compile in `Debug|x64` (vendored spdlog/fmt vs
current MSVC STL), and 20 of 24 `experiments` tests fail.

Extra checks specific to this migration:

- Diff `dumpbin /exports` for each Willpower DLL across the clang-format commit
  to prove that step was cosmetic.
- Render the same world file before and after Phase 5 and compare screenshots.

---

## 6. Effort and risk

| Phase | Half | Size | Risk |
|---|---|---|---|
| 0 formatting | both | large diff, zero semantics | low — export-diff proves it |
| 1 dependency swap | A | moderate | **high** — build-system surgery, FetchContent |
| 2 common/geometry | both | small | low — strict superset |
| 3 DataNode cutover | A | moderate, wide | **high** — interlocked, long red build |
| 4 YAML | A | small | low — self-diagnosing |
| 5 renderers | A | small, precise | medium — visual regressions are silent |
| 6 SDL3 + ImGui | B | moderate | **high, but isolated** — imnodes has no reference |
| 7 cleanup | both | small | low |

Phases 1 and 3 are high-risk but de-risked by the same thing: tungsten-oxide
already did them, so the target state is readable rather than inventable.

Phase 6 is high-risk for the opposite reason — `imnodes`/`imgui-knobs` on ImGui
1.92 is the one place with **no reference implementation to copy**. It is
sequenced last precisely because Half B is independent: if it stalls, the game
is already shipping on the new engine.

Two risks worth naming explicitly:

- **Scope creep into PBR.** The material rework is the most visible change in
  MPP and it is tempting to adopt because it is there. BooleanWorld's materials
  are procedural shader programs; `BasicMaterial` is an exact fit and PBR would
  turn a bounded port into an open-ended art-pipeline project. Decision 1 closes
  this off — keep it closed.
- **Silent render regressions.** Phase 5 is the only phase whose failure mode is
  visual rather than a compiler or loader error. Screenshot comparison against
  the pre-migration build is the gate, not "it launched".
