@echo off
REM Configures and builds BooleanWorld with CMake.
REM
REM Usage: RebuildAll.bat [Debug|Release|Profiling]     (default: Release)
REM
REM The two submodules under ext\ keep their own Visual Studio solutions and
REM are not part of the CMake build; CMake builds them with MSBuild on first
REM configure if their libraries are missing. See cmake\Submodules.cmake.

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
echo   Launcher\build\vs2026\bin\x64\%CONFIG%\Launcher.exe
echo   Applications\BooleanWorld\editor\bin\x64\%CONFIG%\editor.exe
echo.
echo Run: cd Launcher\build\vs2026\bin\x64\%CONFIG% ^&^& Launcher.exe BooleanWorld.cfg
EXIT /B 0

:fail
echo.
echo BUILD FAILED (%CONFIG%^|x64).
EXIT /B 1
