# BooleanWorld

2D game engine and the BooleanWorld applications, extracted from the
[Willpower](https://wtmrsh@bitbucket.org/wtmrsh/willpower.git) repository.

See [MIGRATION-PLAN.md](MIGRATION-PLAN.md) for what was removed and why.

## Cloning

    git clone --recurse-submodules <url>

The submodules matter: `ext/MassivePolyPusher` has a nested `ext/utils` of its
own, so a non-recursive clone will not build.

## Building

    RebuildAll.bat Release

This builds everything in dependency order:

    ext/MassivePolyPusher/ext/utils -> ext/Utils -> ext/MassivePolyPusher
      -> Willpower -> AppLib -> BooleanWorld -> Launcher

Edit the `MSBUILD` path at the top of `RebuildAll.bat` if Visual Studio is
installed somewhere other than the default.

Only `x64` is supported; `vendor/lib` ships x64 libraries only.

## Running

    cd Launcher\build\vs2026\bin\x64\Release
    Launcher.exe BooleanWorld.cfg

`Launcher.exe` loads an application DLL named in the `.cfg`. The other
executables (`editor`, `floored`, `experiments`, `profiler`) build standalone
under `Applications/BooleanWorld/*/bin/x64/Release`.

## Known issues

- `editor` and `floored` do not compile in `Debug|x64`: the vendored
  `spdlog/fmt` v9 uses `stdext::checked_array_iterator`, removed from the
  current MSVC STL. Release is unaffected. Pre-existing, inherited from
  Willpower.
- 20 of 24 `experiments` tests fail on PSLG hierarchy assertions. Also
  pre-existing and identical to the source repository.
