# BooleanWorld extraction — migration plan

Source: `D:\Code\Projects\Willpower`
Target: `D:\Code\Projects\boolean-world` (new git repo)

Goal: a repo containing only what the `Applications/BooleanWorld` projects need,
with `Willpower` and `AppLib` pruned to their used surface. Folder structure is
preserved. Submodules (`ext/Utils`, `ext/MassivePolyPusher`) are copied as
submodule references and **never edited**.

---

## 1. What the analysis found

Include-graph analysis was run over every non-vendored `.h/.cpp` in
`Applications/BooleanWorld` and `Launcher`, resolving includes against the same
search roots the `.vcxproj` files use, then transitively pulling each reached
header's sibling `.cpp` in the same module.

### Module usage (files reached / files present)

| Module | Used | Total | Verdict |
|---|---:|---:|---|
| `AppLib` | 93 | 95 | keep |
| `willpower.application` | 84 | 95 | keep, prune 10 |
| `willpower.viz` | 35 | 38 | keep, prune 2 |
| `willpower.geometry` | 42 | 53 | keep, prune 10 |
| `willpower.common` | 39 | 66 | keep, prune 26 |
| `willpower.collide` | 11 | 13 | keep, prune 1 |
| `willpower.serialization` | 5 | 14 | keep, prune 8 |
| `willpower.firepower` | 9 | 28 | keep, prune 18 |
| **`willpower.editor`** | **0** | 17 | **delete whole module** |
| **`willpower.eventuality`** | **0** | 9 | **delete whole module** |
| **`willpower.wayfinder`** | **0** | 25 | **delete whole module** |

Corroborated independently by the linker inputs: no `.vcxproj` under
`Applications/BooleanWorld`, `AppLib`, or `Launcher` lists
`Willpower.Editor*.lib`, `Willpower.Eventuality*.lib`, or
`Willpower.Wayfinder*.lib` in `AdditionalDependencies`, and none lists those
modules' `include` directories in `AdditionalIncludeDirectories`. Both signals
agree, so those three modules are dead for BooleanWorld.

### Dependency shape that survives

```
Launcher.exe ──loads──> BooleanWorld.dll (Applications/BooleanWorld/app)
                             │
     core.lib (BooleanWorld/core) ─────┤
                             │
                          AppLib.dll
                             │
   ┌────────┬────────┬───────┼────────┬──────────────┬─────────┐
 Common  Collide  Firepower Geometry  Serialization  Viz   Application
   │                            └── SerializerMeshChunk → Serialization
   └──────────────────── mpp / mpp-helper / mpp-mesh / mpp-program (submodule)
                         Utils (submodule), vendor/ (prebuilt 3rd-party)
```

`willpower.serialization` is *only* reachable through
`willpower.geometry/SerializerMeshChunk.{h,cpp}` — nothing in BooleanWorld or
AppLib includes a `willpower/serialization/` header directly. It stays, but
only its `Serializer`-chunk core is used; the `BinarySerializer` /
`TextSerializer` / `SerializerDirectoryChunk` concrete implementations are not.

### Sibling BooleanWorld projects, all kept

`app` (the DLL Launcher loads), `core` (static lib, the shared engine),
`core-dll` (DLL wrapper used by `scripts/*.py`), `editor`, `floored`,
`experiments`, `profiler` — six executables plus the DLL, all defined in
`Applications/build/vs2026/BooleanWorld.sln`, which is already standalone and
references nothing from `HexWorld` or `Template`.

### Size

Source is small; the ~7.5 GB in the tree is almost entirely `obj/`, `bin/`,
`lib/`, `.vs/`. Excluding those, the whole retained set is on the order of
50 MB, dominated by `Applications/BooleanWorld/app/resources` (7 MB) and
`vendor/` (161 MB of prebuilt libs, which must come across).

---

## 2. Directories to drop entirely

| Path | Reason |
|---|---|
| `Applications/HexWorld`, `Applications/Template` | other applications |
| `Applications/resources` | shared assets for HexWorld/Template/Shmup/TGD; nothing in BooleanWorld references them |
| `Willpower/willpower.editor` | unreferenced (see above) |
| `Willpower/willpower.eventuality` | unreferenced |
| `Willpower/willpower.wayfinder` | unreferenced |
| `Willpower/scratchpad` | Willpower's own sample app; the only consumer of `BinarySerializer` |
| `Willpower/tests` | VS2026 solution stub, no BooleanWorld coverage |
| `Willpower/3rd party` | not referenced by any `.vcxproj` or `.props`; superseded by `vendor/` |
| `Willpower/doc`, `Willpower/build/doxyfile` | engine-wide docs |
| `node_modules`, `package.json`, `package-lock.json` | `package.json` is `{}` — empty stub |
| `bitbucket-pipelines.yml` | CI for the old repo; rewrite if needed |
| all `obj/`, `bin/`, `lib/`, `.vs/`, `*.user`, `*.suo` | build output |

**Judgement call flagged:** `Launcher/build/support/` holds per-machine DLL
drops keyed by `%COMPUTERNAME%` (seven machine names). I'd carry it across
as-is rather than guess which are live — it is small and
`CopyWillpowerBinaries.bat` depends on the layout. Delete the ones you know are
dead machines after the migration.

---

## 3. Files to prune inside retained modules

The full list of 124 candidates is in `prune-candidates.txt` alongside this
plan, one repo-relative path per line. Highlights:

- **`willpower.common`** (26 files) — the whole spline family
  (`CubicBSpline`, `CentripetalCatmullRomSpline`, and their `*Looping`
  variants), `DynamicAccelerationGrid` + `CachedDynamicAccelerationGrid`,
  `polypartition`, `DateTime`, and the entire `*BatchRenderable` hierarchy
  (`BatchRenderable`, `Quad`, `Triangle`, `TriangleStrip`,
  `IndexedTriangle`) — BooleanWorld renders through `willpower.viz` instead.
- **`willpower.firepower`** (18 files) — the beam subsystem (`Beam`,
  `BeamManager`, `BeamResource`, `BeamVertexDataBuilder`,
  `BeamDefaultDefinitionFactory`), the bomb subsystem (`BombManager`,
  `BombVertexDataBuilder`), `AreaQuery`, `ObjectCollisionManager`,
  `DynamicCollisionObject`, `MeshCollisionCircle/Edge`. AppLib uses only
  `firepower/MeshCollisionManager.h` and `firepower/BeamShard.h`.
- **`willpower.geometry`** (10 files) — `CsgUtils`, `MeshValidator`,
  `PolygonFilter`, `VertexFilter`, `MeshPropertyCollection`,
  `OperationStatus`.
- **`willpower.serialization`** (8 files) — `BinarySerializer`,
  `TextSerializer`, `SerializerDirectoryChunk`, and `Serializer` itself.
- **`willpower.application`** (10 files) — `Scheduler`, `SchedulerTask`,
  `InputHelper`, `Document`, `DocumentManager`, and two unused
  `resourcesystem` default-definition factories.
- **`AppLib`** (1 file) — `include/applib/VisualTriMesh.h`.

`build/version/Version.h` is excluded from the prune list in every module — it
is generated by `SetVersion.ps1` and referenced by the `.rc` build.

**Confidence caveat.** Static include analysis cannot see three things:

1. TUs compiled into a DLL purely for side effects (factory
   self-registration in a static initialiser).
2. Types used only polymorphically through a base-class pointer, where the
   derived header is never included by the consumer.
3. Anything reached through the resource system by *string name* from a YAML
   or XML resource file rather than by symbol.

The `*DefaultDefinitionFactory.cpp` files are exactly the shape that trips
hazard 1, so they are staged last and verified separately (step 5).

---

## 4. Migration steps

### Step 0 — prepare the target repo

```
cd D:\Code\Projects\boolean-world
git init
```

Copy `.gitignore` from the source and strip the `HexWorld` / `Template` /
`app-2d` / `Editor/build/` / `Willpower/tests/` entries.

### Step 1 — bring the submodules across

Do **not** copy `ext/Utils` and `ext/MassivePolyPusher` as files. Record the
exact commits first, then re-add:

```
cd D:\Code\Projects\Willpower
git submodule status            # capture the two SHAs

cd D:\Code\Projects\boolean-world
git submodule add https://wtmrsh@bitbucket.org/wtmrsh/utils.git ext/Utils
git submodule add https://wtmrsh@bitbucket.org/wtmrsh/massivepolypusher.git ext/MassivePolyPusher
git -C ext/Utils checkout <SHA>
git -C ext/MassivePolyPusher checkout <SHA>
git add ext/Utils ext/MassivePolyPusher && git commit -m "Pin submodules"
```

Also copy `ignore = dirty` onto both entries in the new `.gitmodules`, matching
the source. The three `GeometryEditor/ext/*` entries in the source
`.gitmodules` are stale — `GeometryEditor/` does not exist in the tree — so
drop them.

Note that `ext/MassivePolyPusher` has its own nested `ext/utils` submodule;
clone the new repo with `--recurse-submodules` and confirm it populates before
going further.

### Step 2 — copy the retained tree

Mirror these paths from source to target, preserving structure and excluding
`obj bin lib .vs *.user *.suo *.log`:

```
AppLib/{build,doc,include,src}
Applications/BooleanWorld/{app,common,core,core-dll,editor,experiments,floored,profiler,scripts,tiled}
Applications/build/{SetVersion.ps1,version,vs2026/BooleanWorld.sln}
Launcher/{build,include,src,version}
Willpower/build/{SetVersion.ps1,vs2026}
Willpower/version
Willpower/willpower.{application,collide,common,firepower,geometry,serialization,viz}
vendor/{bin,include,lib}
README.md
RebuildAll.bat
```

`robocopy` with `/MIR /XD obj bin lib .vs /XF *.user *.suo` is the reliable way
to do this on Windows.

`Applications/BooleanWorld/scripts/` contains checked-in DLLs
(`core-dll.dll`, `Willpower.Common.dll`, `Utils.dll`, `FreeImage.dll`) that the
Python tooling loads. They are build output living outside an ignored
directory — carry them for now so `gen-world.py` keeps working, and consider
replacing them with a post-build copy step later.

### Step 3 — commit the verbatim copy, *then* prune

Commit the untouched copy first. Every subsequent removal is then a reviewable
diff against a known-good baseline, and `git bisect` works if a later build
breaks.

### Step 4 — remove the three dead modules

Delete `Willpower/willpower.{editor,eventuality,wayfinder}`, then update:

- `Willpower/build/vs2026/Willpower.sln` — remove the three `Project(...)`
  blocks (`Willpower.Editor` `{E1C5DBEF-…}`, `Willpower.Eventuality`
  `{2A29739F-…}`, `Willpower.Wayfinder` `{FE3B504D-…}`) and their
  `GlobalSection(ProjectConfigurationPlatforms)` lines. Also remove the
  `ScratchPad` project `{17705D15-…}` and its `ProjectDependencies` on
  Wayfinder and Firepower.
- `Launcher/build/CopyWillpowerBinaries.bat` — delete the
  `willpower.editor` and `willpower.wayfinder` copy lines.
- `RebuildAll.bat` — it still points at `vs2017` paths that no longer exist
  and rebuilds `Win32`. Rewrite it against `vs2026` and `x64` while you are
  here.

Build the full solution. This should be clean — nothing links those libs.

### Step 5 — prune files inside retained modules

Work **one module at a time**, rebuilding the whole `BooleanWorld.sln` after
each. For each file removed:

1. Delete the `.h`/`.cpp` pair.
2. Remove the matching `<ClCompile Include=…>` / `<ClInclude Include=…>` from
   that module's `.vcxproj` — these are explicit lists, not wildcards, so a
   stale entry is a hard build error.
3. Remove the matching entry from the `.vcxproj.filters` alongside it.

Suggested order, lowest risk first:

1. `willpower.common` — the splines, grids, `polypartition`, `DateTime`.
2. `willpower.geometry` — filters and `CsgUtils`.
3. `willpower.application` — `Scheduler`, `InputHelper`, `Document*`.
4. `willpower.collide` — `CollideAABB.cpp`.
5. `AppLib` — `VisualTriMesh.h`.
6. `willpower.viz` — `DynamicGeometryMeshRenderer.cpp`,
   `SplineRenderParams.cpp`.
7. `willpower.serialization` — the concrete serializers.
8. `willpower.firepower` — the beam and bomb subsystems. **Highest risk**:
   AppLib has its own `Beam`/`BeamManager`/`BeamInstance` types that shadow
   firepower's, so confirm which one each call site binds to before deleting.

The `*BatchRenderable` hierarchy in `willpower.common` should be removed as one
unit (base + all four derived), not piecemeal.

Leave the `*DefaultDefinitionFactory.cpp` files
(`MaterialResourceDefaultDefinitionFactory`, `TestFileDefaultDefinitionFactory`,
`BeamDefaultDefinitionFactory`, `BulletDefaultDefinitionFactory`) for **last**,
and after removing them do a *runtime* check, not just a build: launch the app
and the editor and load `app/resources/gen-1.yaml` and a `tiled/maps` level. A
missing self-registering factory produces a resource-not-found at load time,
not a link error.

### Step 6 — fix up build configurations

The `.vcxproj` files carry stale `Win32`/`x86` configurations whose link inputs
have drifted from `x64` — e.g. `BooleanWorld.vcxproj`'s Win32 config links
`Willpower.Firepower.lib` and `Willpower.Geometry.lib`, while its x64 config
links `MppMesh.lib`, `core.lib`, and `yaml-cpp.lib` instead. `vendor/lib`
ships `x64` only, so the Win32 configurations cannot build at all.

Remove `Debug|Win32` and `Release|Win32` from every `.vcxproj` and from both
`.sln` files. This eliminates the drift as a class of bug and roughly halves
the config surface.

Also reconcile the x64 link inputs against the pruned module set so the two
solutions agree.

### Step 7 — verification gate

Before declaring the migration done:

- [ ] `Willpower.sln` builds Debug|x64 and Release|x64, zero warnings-as-errors regressions
- [ ] `AppLib.sln` builds both configs
- [ ] `BooleanWorld.sln` builds all seven projects, both configs
- [ ] `Launcher.sln` builds both configs
- [ ] `Launcher.exe` loads `BooleanWorld.dll` and reaches the play state
- [ ] `editor.exe` opens and loads a map from `tiled/maps`
- [ ] `floored.exe` runs
- [ ] `experiments.exe` — its gtest suite passes
- [ ] `scripts/gen-world.py` runs against `core-dll.dll`
- [ ] fresh `git clone --recurse-submodules` of the new repo builds from
      scratch on a clean machine

The last item is the one that catches accidental dependencies on files that
exist locally but were never committed.

---

## 5. Things deliberately not done

- **Not de-duplicating the four ImGui copies.** `Launcher`,
  `BooleanWorld/app`, `BooleanWorld/editor`, and `BooleanWorld/floored` each
  vendor their own ImGui + ImPlot (~130k lines total, and the app/Launcher
  pair is a *newer* ImGui than the editor/floored pair). Consolidating them is
  a real cleanup worth doing, but it changes behaviour and belongs in its own
  change after the migration is verified green.
- **Not pruning inside submodules.** `ext/MassivePolyPusher` carries
  `demo-suite`, `model-convert`, and `program-builder` that BooleanWorld never
  uses, but the constraint is explicit and they cost nothing at build time.
- **Not touching `vendor/`.** It holds prebuilt libraries for roughly a dozen
  packages BooleanWorld does not link (`fmod`, `entt`, `poly2tri`, `freeimage`,
  `SDL3`, …). Pruning it is easy and safe *after* step 7 passes, guided by the
  union of every `AdditionalDependencies` in the final tree — but doing it
  before then adds noise to the failure diagnosis when a build breaks.
- **Not renaming the `Willpower` namespace or directory.** The `WP_NAMESPACE`
  macro and `willpower/…` include prefix appear in hundreds of files;
  renaming buys nothing and would make every future cherry-pick from the
  upstream Willpower repo conflict.
