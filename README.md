# BooleanWorld

2D game engine and the BooleanWorld applications.

Originally extracted from the
[Willpower](https://wtmrsh@bitbucket.org/wtmrsh/willpower.git) repository, and
since moved onto the same engine stack as
[tungsten-oxide](https://github.com/ajare) — see
[MIGRATION-PLAN.md](MIGRATION-PLAN.md) for the original extraction and
[TUNGSTEN-MIGRATION-PLAN.md](TUNGSTEN-MIGRATION-PLAN.md) for the engine move.

## Cloning

    git clone --recurse-submodules <url>

`ext/massive-poly-pusher` has its own nested submodules (assimp, glew, sdl,
utils), so a non-recursive clone will not build.

## Building

    RebuildAll.bat Release

or directly:

    cmake -S . -B build-cmake -G "Visual Studio 18 2026" -A x64
    cmake --build build-cmake --config Release --parallel

Configurations are `Debug`, `Release` and `Profiling`. Only `x64` is supported.

`build-cmake/BooleanWorld.sln` can be opened in Visual Studio; `Launcher` is
the startup project.

### MassivePolyPusher is built separately

`ext/massive-poly-pusher` keeps its own CMake build and is never modified by
this project — it is consumed as a *build tree*: import libraries from
`build/lib/<CONFIG>`, DLLs from `build/bin/<CONFIG>`. CMake configures and
builds it on demand if its libraries are missing. That first build fetches
GLEW, SDL3, assimp and yaml-cpp and takes several minutes.

Pass `-DBW_BUILD_MPP=OFF` to manage it yourself:

    cmake -S ext/massive-poly-pusher -B ext/massive-poly-pusher/build -G "Visual Studio 18 2026" -A x64
    cmake --build ext/massive-poly-pusher/build --config Release --parallel

`utils`, SDL3, GLEW and yaml-cpp all come from that tree. `vendor/` supplies
only what MassivePolyPusher does not: spdlog, Clipper2, concurrencpp, entt,
mapbox, FMOD, FreeImage, GLFW, gtest, nfd and Superluminal.

### Where things land

Build products go where the original Visual Studio solutions put them, e.g.
`Applications/BooleanWorld/editor/bin/x64/Release`. This is deliberate:
`editor`, `experiments` and `profiler` resolve paths such as
`../../../../core/doc` relative to their working directory, so they only run
correctly from those locations.

## Running

    cd Launcher\build\vs2026\bin\x64\Release
    Launcher.exe BooleanWorld.yaml

`Launcher.exe` loads an application DLL named in the config. The build
generates `BooleanWorld.yaml` with absolute paths next to `Launcher.exe`.

To override that on a given machine, put your own config in
`Launcher/build/support/<COMPUTERNAME>/vs2026/x64/<Config>/` — the build stages
that directory over the generated one. Only `ASTRALEMPRESS` is checked in.

The other executables build standalone under
`Applications/BooleanWorld/<app>/bin/x64/<Config>`. Run them from that
directory.

Tests:

    ctest --test-dir build-cmake -C Release

## Configuration is YAML

Both the resource manifest (`app/resources/Resources.yaml`) and the launcher
config are YAML. The resource system rejects anything that is not `.yaml`/`.yml`.

`tools/xml_to_resource_yaml.py` converts an old XML manifest, following the
mapping the loader expects: elements and attributes both become keys, mixed
text becomes a `value` key, and repeated siblings become a sequence.

Tiled `.tmx`/`.tsx` map data is still XML — only resource *definitions* moved.

## Formatting

`tools/format.py [--check] [library ...]` runs clang-format over the project's
own sources. Vendored third-party code (ImGui, ImPlot, imnodes, StackWalker,
Clipper 1) is excluded by explicit path.

## Known issues

- 20 of 24 `experiments` tests fail on PSLG hierarchy assertions. Pre-existing,
  inherited from Willpower.

## The GL context is not a core profile

MassivePolyPusher draws 2D text as point sprites whenever the driver reports a
maximum point size of 16 or more, and that path calls
`glEnable(GL_POINT_SPRITE)` — an enum removed in the core profile. Under a core
context every 2D projection change raises `GL_INVALID_ENUM`, which `Release`
queues harmlessly but `Debug` turns into a throw via the engine's `GL_CHECK`.

So `Launcher`, `editor` and `floored` all ask for GL 3.x without a profile
mask. Do not add one back without checking that code path first.
