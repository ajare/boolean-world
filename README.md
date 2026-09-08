# BooleanWorld

2D game engine and the BooleanWorld applications.

Originally extracted from the
[Willpower](https://github.com/ajare/willpower) repository, which is consumed as
a submodule under `ext/willpower`, and since moved onto the same engine stack as
[tungsten-oxide](https://github.com/ajare) — see
[MIGRATION-PLAN.md](MIGRATION-PLAN.md) for the original extraction and
[TUNGSTEN-MIGRATION-PLAN.md](TUNGSTEN-MIGRATION-PLAN.md) for the engine move.

## Cloning

    git clone --recurse-submodules <url>

`ext/willpower` has nested submodules, including MassivePolyPusher, so a
non-recursive clone will not build. For an existing clone, run:

    git submodule update --init --recursive

## Building

Only x64 builds are supported. CMake builds Willpower and its nested
MassivePolyPusher dependency on demand, so the first build can take several
minutes.

### Linux

A C++23 compiler, CMake 3.26 or newer, and the OpenGL/X11 development packages
needed by SDL3 are required. Configure with a single-config generator:

    cmake -S . -B build-linux \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF
    cmake --build build-linux --parallel

Use `Debug` instead of `Release` for a debug build. To build and run the tests,
omit `-DBUILD_TESTING=OFF`, then run:

    ctest --test-dir build-linux --output-on-failure

The build consumes Willpower and MassivePolyPusher from same-named standalone
build trees. For `build-linux`, these are `ext/willpower/build-linux` and
`ext/willpower/ext/massive-poly-pusher/build-linux`. To use already-built
dependency artifacts without allowing BooleanWorld to update them, add
`-DBW_BUILD_WILLPOWER=OFF` when configuring.

The pinned Linux FMOD and Steam Audio headers and shared objects are staged
under `vendor/`, parallel to the Windows SDK files, and audio is enabled by
default. The launcher copies the unversioned and SONAME-versioned FMOD objects
plus `libphonon.so` and `libphonon_fmod.so` beside itself.

To refresh those staged files from SDK archives, extract FMOD Engine 2.03.14
and the **Steam Audio FMOD integration** 4.8.1, then run:

    ./build_from_scratch.sh \
        --fmod-sdk /path/to/fmodstudioapi20314linux \
        --steam-audio-sdk /path/to/steamaudio_fmod

The paths must contain FMOD's `api/{core,studio}` trees and Steam Audio FMOD's
`lib/linux-x64/{libphonon.so,libphonon_fmod.so}`. A normal build needs no SDK
arguments. Set `BW_ENABLE_FMOD=OFF` only for an intentionally audio-free build.

### Windows

    RebuildAll.bat /config Release

Or invoke CMake directly:

    cmake -S . -B build-cmake -G "Visual Studio 18 2026" -A x64
    cmake --build build-cmake --config Release --parallel

Windows configurations include `Debug`, `Release`, `Shipping`, and `MemCheck`.
`build-cmake/BooleanWorld.sln` can be opened in Visual Studio; `Launcher` is
the startup project. Run a CMake configure before opening the FMOD Studio
project: it stages the Steam Audio plugin in the project's `Plugins/` directory.
Without it, Studio opens with unrecognised effects on every event.

### Submodule libraries are built separately

The generated BooleanWorld solution contains only projects under `src/`.
Willpower and MassivePolyPusher are consumed as imported binaries from
same-named build trees beneath their checkouts. A BooleanWorld `build-windows`
tree uses `ext/willpower/build-windows` and
`ext/willpower/ext/massive-poly-pusher/build-windows`; another build-directory
name is propagated in the same way. CMake configures and builds Willpower on
demand if its libraries are missing. The first build takes several minutes.

Pass `-DBW_BUILD_WILLPOWER=OFF` to manage the build yourself. On Linux:

    cmake -S ext/willpower -B ext/willpower/build-linux \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    cmake --build ext/willpower/build-linux --parallel

On Windows:

    cmake -S ext/willpower -B ext/willpower/build-windows -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=OFF
    cmake --build ext/willpower/build-windows --config Release --parallel

`utils`, SDL3, GLEW and yaml-cpp all come from that tree. `vendor/` supplies
only what MassivePolyPusher does not: spdlog, fmt, concurrencpp, entt, mapbox,
FMOD, gtest, nfd, nlohmann, inifile-cpp, rapidhash,
spline_library and Superluminal.

### Where things land

All generated binaries, import libraries, staged runtime dependencies, and
runtime support files stay beneath the selected CMake build directory. Runtime
targets use `<build-dir>/bin/<Config>/<Target>/`; import and static libraries
use `<build-dir>/lib/<Config>/<Target>/`.

## Running

    cd build-cmake\bin\Release\Launcher
    Launcher.exe BooleanWorld.yaml

On Linux the staged launcher is self-contained apart from its RPATH-resolved
engine libraries:

    cd build-linux/bin/Release/Launcher
    ./Launcher Game.yaml

`Launcher.exe` loads an application DLL named in the config. The build
generates `BooleanWorld.yaml` with absolute paths next to `Launcher.exe`.

To override that on a given machine, put your own config in
`src/Launcher/support/<COMPUTERNAME>/<Config>/` — the build stages that
directory over the generated one. Only `ASTRALEMPRESS` is checked in.

Other executables use their own target directories beneath
`build-cmake/bin/<Config>/`.

The Python tooling loads `core-dll` from the default `build-cmake` tree. Set
`BOOLEANWORLD_BUILD_DIR` when using a differently named CMake build directory.

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

## The GL context is not a core profile

MassivePolyPusher draws 2D text as point sprites whenever the driver reports a
maximum point size of 16 or more, and that path calls
`glEnable(GL_POINT_SPRITE)` — an enum removed in the core profile. Under a core
context every 2D projection change raises `GL_INVALID_ENUM`, which `Release`
queues harmlessly but `Debug` turns into a throw via the engine's `GL_CHECK`.

So `Launcher` and `editor` both ask for GL 3.x without a profile mask. Do not
add one back without checking that code path first.
