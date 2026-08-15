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

- **Debug builds do not run.** All three GUI applications abort at startup in
  `Debug|x64`; `Release` and `Profiling` are fine. For the Launcher the cause is
  MassivePolyPusher's `GL_CHECK`, which is compiled in only when `_DEBUG` is
  defined and throws on any GL error — it reports `GL_INVALID_ENUM` during the
  first 2D draw, before any game content loads. That check lives in the
  submodule, so it cannot be addressed from this repo, and tungsten-oxide has
  never built or run its own Launcher in Debug either. `editor` and `floored`
  abort too; their Debug path had never been reachable before spdlog was
  upgraded, so it has never worked rather than having regressed.
- 20 of 24 `experiments` tests fail on PSLG hierarchy assertions. Pre-existing,
  inherited from Willpower.
