@echo off
REM Rebuilds BooleanWorld and everything it depends on, in dependency order.
REM Usage: RebuildAll.bat [Debug|Release]        (default: Release)

SETLOCAL

SET CONFIG=%1
IF "%CONFIG%"=="" SET CONFIG=Release

SET MSBUILD="C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
IF NOT EXIST %MSBUILD% (
    echo ERROR: MSBuild not found at %MSBUILD%
    echo Edit RebuildAll.bat to point at your Visual Studio installation.
    EXIT /B 1
)

SET FLAGS=/t:Rebuild /p:Configuration=%CONFIG% /p:Platform=x64 /m /v:minimal /nologo

echo === Utils (nested inside MassivePolyPusher) ===
REM MassivePolyPusher links its own copy of Utils, so this must build first.
%MSBUILD% "ext\MassivePolyPusher\ext\utils\build\vs2026\Utils.sln" %FLAGS% || GOTO :fail

echo === Utils (submodule) ===
%MSBUILD% "ext\Utils\build\vs2026\Utils.sln" %FLAGS% || GOTO :fail

echo === MassivePolyPusher (submodule) ===
%MSBUILD% "ext\MassivePolyPusher\build\vs2026\MassivePolyPusher.sln" %FLAGS% || GOTO :fail

echo === Willpower ===
%MSBUILD% "Willpower\build\vs2026\Willpower.sln" %FLAGS% || GOTO :fail

echo === AppLib ===
%MSBUILD% "AppLib\build\vs2026\AppLib.sln" %FLAGS% || GOTO :fail

echo === BooleanWorld ===
%MSBUILD% "Applications\build\vs2026\BooleanWorld.sln" %FLAGS% || GOTO :fail

echo === Launcher ===
%MSBUILD% "Launcher\build\vs2026\Launcher.sln" %FLAGS% || GOTO :fail

echo.
echo Build succeeded (%CONFIG%^|x64).
EXIT /B 0

:fail
echo.
echo BUILD FAILED (%CONFIG%^|x64).
EXIT /B 1
