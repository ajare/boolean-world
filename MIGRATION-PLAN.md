# BooleanWorld extraction — migration record

Source: `D:\Code\Projects\Willpower`
This repo: the stripped-down result, containing only what the
`Applications/BooleanWorld` projects need.

**Status: complete and verified.** A fresh
`git clone --recurse-submodules` of this repo builds end to end with
`RebuildAll.bat Release` and the game reaches its Play state.

---

## 1. What was removed

### Whole Willpower modules (51 files)

`willpower.editor`, `willpower.eventuality`, `willpower.wayfinder` are
unreachable from BooleanWorld. Two independent signals agreed, and the build
then confirmed it: nothing in `Applications/BooleanWorld`, `AppLib` or
`Launcher` includes their headers, no `.vcxproj` links their `.lib`, and
`Willpower.sln` builds clean without them.

### Other directories

`Applications/HexWorld`, `Applications/Template`, `Applications/resources`
(shared assets for the other apps), `Willpower/scratchpad` (the only consumer
of `BinarySerializer`), `Willpower/tests`, `Willpower/3rd party` (referenced by
no project file), `Willpower/doc`, `node_modules` + the empty `package.json`,
`bitbucket-pipelines.yml`, and all `obj/ bin/ lib/ .vs/` build output. Also
`willpower.collide/build/vs2017`, a stale toolset directory no solution
referenced.

### Files pruned inside retained modules (121)

| Module | Files | Before → After |
|---|---:|---|
| `willpower.common` | 26 | 66 → 40 |
| `willpower.firepower` | 18 | 28 → 10 |
| `willpower.geometry` | 10 | 52 → 42 |
| `willpower.application` | 8 | 95 → 87 |
| `willpower.serialization` | 8 | 14 → 6 |
| `willpower.viz` | 2 | 38 → 36 |
| `AppLib` | 1 | 95 → 94 |
| `willpower.collide` | 0 | 13 → 13 |

Highlights: the entire spline family and `*BatchRenderable` hierarchy from
`common` (BooleanWorld renders through `willpower.viz`); the beam and bomb
subsystems from `firepower` (AppLib uses only `MeshCollisionManager.h` and
`BeamShard.h`); the concrete `Binary`/`Text` serializers, leaving only the
chunk core that `geometry/SerializerMeshChunk` actually reaches.

Each removal took the `.h`/`.cpp` **and** its `<ClCompile>`/`<ClInclude>`
entries in both the `.vcxproj` and the `.vcxproj.filters` — these are explicit
lists, so a stale entry is a hard build error.

---

## 2. Three files the static analysis got wrong

All three were flagged unused, and the build proved otherwise. Every one has a
**filename that does not match the symbol it defines**, which is what defeated
the header-to-source pairing:

| File | Actually defines |
|---|---|
| `collide/src/CollideAABB.cpp` | `ColliderAABB` (missing `r`) |
| `application/…/TestFileDefaultDefinitionFactory.cpp` | `TextFileDefault…` (typo: Test/Text) |
| `application/…/MaterialResourceDefaultDefinitionFactory.cpp` | `MaterialDefault…` |

The two factories are the self-registering kind that static include analysis
can never see — they are instantiated by `ResourceManager.cpp`, not included by
name. Staging them last, as planned, is what caught them.

---

## 3. Other defects found and fixed

- **`experiments` had no dependency on `core`.** It was the only project in
  `BooleanWorld.sln` without one, so a clean parallel (`/m`) build could link
  it before `cored.lib` existed (`LNK1181`). Pre-existing.
- **Stale `ClInclude` in `BooleanWorld.vcxproj`** pointing at
  `..\..\..\HexWorld\include\NotImplementedException.h` — a path that resolved
  to `BooleanWorld\HexWorld\` and never existed.
- **Win32/x86 configurations removed.** `vendor/lib` ships x64 only, so they
  could never link, and they had drifted: `BooleanWorld.vcxproj`'s Win32 config
  linked `Willpower.Firepower.lib` + `Willpower.Geometry.lib` while its x64
  config links `MppMesh.lib` + `core.lib` + `yaml-cpp.lib`.
- **`RebuildAll.bat` rewritten.** It pointed at `vs2017` paths that no longer
  exist and rebuilt Win32. It now builds vs2026/x64 in dependency order —
  including MassivePolyPusher's *nested* `ext/utils`, which it links instead of
  the top-level one. The fresh-clone test is what caught that omission.
- **FMOD `.bank` files are now tracked.** A clean clone built but could not
  run: resource validation failed on `Master.bank`, `Master.strings.bank` and
  `Themes.bank`, and the app exited during Load. They are FMOD Studio build
  output that the upstream repo also ignores, so a clean clone of the
  *original* has the same defect. They are validated even with audio disabled,
  so they are required to reach Play. 92K total. Revert that commit if you
  would rather regenerate them from the FMOD Studio project.

---

## 4. Verification performed

| Check | Result |
|---|---|
| `Willpower.sln` Debug + Release x64 | clean |
| `AppLib.sln` Debug + Release x64 | clean |
| `BooleanWorld.sln` Release x64, all 7 projects | clean |
| `BooleanWorld.sln` Debug x64 | clean except `editor` + `floored` — see below |
| `Launcher.sln` Debug + Release x64 | clean |
| `Launcher.exe` → `BooleanWorld.dll` | reaches **`Entering state: Play`**, zero errors in `LauncherLog.html` and `mpp.log` |
| `editor.exe`, `floored.exe` | launch and stay up; **not** driven through a map load (GUI) |
| `experiments.exe` gtest suite | 4 pass / 20 fail — **byte-identical failure set to the source tree** |
| `scripts/gen-world.py` | generates a world against the freshly built `core-dll.dll` |
| fresh `--recurse-submodules` clone → `RebuildAll.bat` → run | **builds and reaches Play** |

### Two known pre-existing failures, both reproduced in the untouched source tree

1. **`editor` and `floored` do not compile in Debug|x64.** Vendored
   `spdlog/fmt` v9 uses `stdext::checked_array_iterator`, which the current
   MSVC STL removed. Release is unaffected because the path is gated on
   `#if defined(_SECURE_SCL) && _SECURE_SCL`. Verified identical in
   `D:\Code\Projects\Willpower`. Fixing it means updating vendored spdlog,
   which is out of scope here.
2. **20 of 24 `experiments` tests fail** — PSLG hierarchy assertions such as
   `CountRoots(hierarchy): 2 vs expected 1`. The failing-test set is identical
   between this repo and the source tree, so the migration did not cause them.

---

## 5. Deliberately not done

- **The four ImGui copies are untouched.** `Launcher`, `app`, `editor` and
  `floored` each vendor their own ImGui + ImPlot (~130k lines; the
  `Launcher`/`app` pair is a *newer* ImGui than the `editor`/`floored` pair).
  Consolidating changes behaviour and belongs in its own change.
- **Submodules are unmodified**, as required. `ext/MassivePolyPusher` still
  carries `demo-suite`, `model-convert` and `program-builder`, which
  BooleanWorld never uses.
- **`vendor/` is untouched.** It holds prebuilt libraries for packages
  BooleanWorld does not link (`entt`, `poly2tri`, `SDL3`, …). Pruning it is
  safe now that the build is green, guided by the union of every
  `AdditionalDependencies` in the tree.
- **`Launcher/build/support/`** still holds per-machine drops for seven
  `%COMPUTERNAME%` values. Delete the dead machines when you know which they
  are — this one is `ASTRALEMPRESS`.
- **No namespace or directory rename.** `WP_NAMESPACE` and the `willpower/…`
  include prefix appear in hundreds of files; renaming would make every future
  cherry-pick from upstream Willpower conflict.

---

## 6. CMake conversion

The four solutions -- `Willpower.sln`, `AppLib.sln`, `BooleanWorld.sln`,
`Launcher.sln` -- and their 16 `.vcxproj` files were replaced by CMake. The
submodules under `ext/` were **not** converted; they keep their own solutions
and are imported as prebuilt libraries (`cmake/Prebuilt.cmake`), built on
demand with MSBuild (`cmake/Submodules.cmake`).

One `CMakeLists.txt` per module, so the folder structure is unchanged.

### Verified equivalent

- All eight DLLs export **identical symbol sets** to the MSBuild build
  (`dumpbin /exports`, compared name by name).
- `editor.exe` imports the identical DLL set.
- Embedded version info matches (`Version.rc` is still compiled into the seven
  Willpower modules and Launcher -- and nothing else, as before).
- `experiments` produces the identical failing-test set.
- `Launcher.exe` reaches `Entering state: Play`; `editor`, `floored` and
  `gen-world.py` all work.

### Four settings that had to be matched exactly

Each of these was a real bug found by building and running, not by reading:

1. **`CharacterSet=Unicode`** -> `UNICODE`/`_UNICODE`. Load-bearing:
   `ApplicationDLL::load` builds a `wstring` and calls `LoadLibrary`, which
   only resolves to `LoadLibraryW` when `UNICODE` is defined.
2. **`SDLCheck`** is per project, not global. Six Willpower modules had it
   **off**; enabling `/sdl` everywhere emitted extra `__autoclassinit2`
   symbols and broke export parity.
3. **`YAML_CPP_STATIC_DEFINE` is per target, not inherited.** The static
   yaml-cpp consumers define it directly so their declarations match the
   linked library.
4. **Output directories must match the original.** `editor`, `experiments` and
   `profiler` resolve paths like `../../../../core/doc` against the working
   directory. A single unified output tree makes those point outside the repo,
   and `editor.exe` dies at startup with `0xC0000409` when
   `directory_iterator` throws on the missing folder.

### What CMake replaced outright

`CopySupportFiles.bat` and `CopyWillpowerBinaries.bat` are gone. Runtime DLLs
are staged from `$<TARGET_RUNTIME_DLLS:...>`, so the dependency list is derived
from the link graph instead of being hand-maintained -- which is what let the
old script keep copying `willpower.editor` and `willpower.wayfinder` long
after nothing linked them.

`Launcher` additionally stages `$<TARGET_RUNTIME_DLLS:BooleanWorld>`: the game
DLL is loaded by name, so its dependencies (`MppHelper` via `AppLib`) are not
in Launcher's own link closure.

## 7. Build order

`RebuildAll.bat [Debug|Release]` configures and builds with CMake.
CMake resolves the order for everything it owns; the only ordering that still
has to be stated is the submodules, because MassivePolyPusher links its own
nested `ext/utils` rather than the top-level one:

```
ext/MassivePolyPusher/ext/utils  →  ext/Utils  →  ext/MassivePolyPusher
                          (MSBuild, cmake/Submodules.cmake)
                                     ↓
        Willpower  →  AppLib  →  BooleanWorld  →  Launcher   (CMake)
```

Then run `Launcher.exe BooleanWorld.cfg` from
`Launcher/build/vs2026/bin/x64/Release`.
