@echo off
REM Configures and builds BooleanWorld with CMake.
REM
REM Usage: RebuildAll.bat [Debug|Release]     (default: Release)
REM
REM MassivePolyPusher (ext\massive-poly-pusher) keeps its own CMake build and is
REM never modified by this project. CMake configures and builds it on demand if
REM its libraries are missing - see cmake\Submodules.cmake. That first build
REM fetches GLEW, SDL3, assimp and yaml-cpp and takes several minutes.

SETLOCAL

SET CONFIG=%1
IF "%CONFIG%"=="" SET CONFIG=Release

SET BUILDDIR=%~dp0build-cmake

where cmake >nul 2>nul
IF ERRORLEVEL 1 (
    echo ERROR: cmake was not found on PATH.
    echo Install CMake 3.25 or newer, or use the copy shipped with Visual Studio.
    EXIT /B 1
)

IF NOT EXIST "%~dp0ext\massive-poly-pusher\mpp\include" (
    echo ERROR: ext\massive-poly-pusher is empty.
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
echo Binaries keep the original layout, e.g.
echo   src\Launcher\build\vs2026\bin\x64\%CONFIG%\Launcher.exe
echo   src\BooleanWorld\editor\bin\x64\%CONFIG%\editor.exe
echo.
echo Run: cd src\Launcher\build\vs2026\bin\x64\%CONFIG% ^&^& Launcher.exe BooleanWorld.yaml
EXIT /B 0

:fail
echo.
echo BUILD FAILED (%CONFIG%^|x64).
EXIT /B 1
