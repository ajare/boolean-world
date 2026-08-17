@echo off
REM Configures and builds BooleanWorld with CMake.
REM
REM Usage: RebuildAll.bat [Debug|Release]     (default: Release)
REM
REM Willpower and its nested MassivePolyPusher dependency keep their own CMake
REM build under ext\willpower\build. This project links their binaries rather
REM than adding their projects to the generated solution. CMake builds them on
REM demand if their libraries are missing - see cmake\Submodules.cmake.

SETLOCAL

SET CONFIG=%1
IF "%CONFIG%"=="" SET CONFIG=Release
SET LAUNCHER_EXE=Launcher.exe
IF /I "%CONFIG%"=="Debug" SET LAUNCHER_EXE=Launcherd.exe

SET BUILDDIR=%~dp0build-cmake

where cmake >nul 2>nul
IF ERRORLEVEL 1 (
    echo ERROR: cmake was not found on PATH.
    echo Install CMake 3.25 or newer, or use the copy shipped with Visual Studio.
    EXIT /B 1
)

IF NOT EXIST "%~dp0ext\willpower\CMakeLists.txt" (
    echo ERROR: ext\willpower is empty.
    echo Run: git submodule update --init --recursive
    EXIT /B 1
)

IF NOT EXIST "%BUILDDIR%\CMakeCache.txt" (
    echo === Configuring ===
    cmake -S "%~dp0." -B "%BUILDDIR%" -G "Visual Studio 18 2026" -A x64 || GOTO :fail
)

echo === Building %CONFIG% ^| x64 ===
cmake --build "%BUILDDIR%" --config %CONFIG% --parallel || GOTO :fail

echo.
echo Build succeeded (%CONFIG%^|x64).
echo.
echo Binaries are under the CMake build tree, e.g.
echo   build-cmake\bin\%CONFIG%\Launcher\%LAUNCHER_EXE%
echo   build-cmake\bin\%CONFIG%\editor\editor.exe
echo.
echo Run: cd build-cmake\bin\%CONFIG%\Launcher ^&^& %LAUNCHER_EXE% BooleanWorld.yaml
EXIT /B 0

:fail
echo.
echo BUILD FAILED (%CONFIG%^|x64).
EXIT /B 1
