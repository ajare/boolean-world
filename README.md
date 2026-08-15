# BooleanWorld

2D game engine and the BooleanWorld applications, extracted from the
[Willpower](https://wtmrsh@bitbucket.org/wtmrsh/willpower.git) repository.

See [MIGRATION-PLAN.md](MIGRATION-PLAN.md) for what was removed and why, and
for the CMake conversion.

## Cloning

    git clone --recurse-submodules <url>

The submodules matter: `ext/MassivePolyPusher` has a nested `ext/utils` of its
own, so a non-recursive clone will not build.

## Building

    RebuildAll.bat Release

or directly:

    cmake -S . -B build-cmake -G "Visual Studio 18 2026" -A x64
    cmake --build build-cmake --config Release --parallel

Configurations are `Debug`, `Release` and `Profiling`. Only `x64` is supported
-- `vendor/lib` ships x64 libraries only.

`build-cmake/BooleanWorld.sln` can be opened in Visual Studio; `Launcher` is
the startup project.

### The submodules are not part of the CMake build

`ext/Utils` and `ext/MassivePolyPusher` keep their own Visual Studio solutions
and are consumed as prebuilt imported libraries. On first configure CMake
builds them with MSBuild if their `.lib` files are missing. Pass
`-DBW_BUILD_SUBMODULES=OFF` to manage them yourself.

### Where things land

Build products go where the original solutions put them, e.g.
`Applications/BooleanWorld/editor/bin/x64/Release`. This is deliberate:
`editor`, `experiments` and `profiler` resolve paths such as
`../../../../core/doc` relative to their working directory, so they only run
correctly from those locations.

## Running

    cd Launcher\build\vs2026\bin\x64\Release
    Launcher.exe BooleanWorld.cfg

`Launcher.exe` loads an application DLL named in the `.cfg`. The build
generates a `BooleanWorld.cfg` with absolute paths next to `Launcher.exe`.

The other executables build standalone under
`Applications/BooleanWorld/<app>/bin/x64/<Config>`. Run them from that
directory.

Tests:

    ctest --test-dir build-cmake -C Release

## Known issues

Both are pre-existing and were verified to behave identically in the original
Willpower repository:

- `editor` and `floored` do not compile in `Debug|x64`: the vendored
  `spdlog/fmt` v9 uses `stdext::checked_array_iterator`, removed from the
  current MSVC STL. Release and Profiling are unaffected -- the offending path
  is guarded by `#if defined(_SECURE_SCL) && _SECURE_SCL`.
- 20 of 24 `experiments` tests fail on PSLG hierarchy assertions.
