@echo off
REM Configures and builds BooleanWorld with CMake.
REM
REM Usage: RebuildAll.bat [/config CONFIG]     (default: Release)
REM
REM Willpower and its nested MassivePolyPusher dependency use same-named CMake
REM build directories beneath their checkouts. For this script's build-cmake,
REM they use ext\willpower\build-cmake and
REM ext\willpower\ext\massive-poly-pusher\build-cmake. This project links their
REM binaries rather than adding their projects to the generated solution. CMake
REM builds them on demand if their libraries are missing - see cmake\Submodules.cmake.

SETLOCAL

SET CONFIG=Release

:parse_args
IF "%~1"=="" GOTO :args_done
IF /I "%~1"=="/config" (
    IF "%~2"=="" (
        >&2 ECHO ERROR: /config requires a value.
        GOTO :usage_error
    )
    SET CONFIG=%~2
    SHIFT
    SHIFT
    GOTO :parse_args
)
IF /I "%~1"=="/?" GOTO :usage_success
IF /I "%~1"=="/help" GOTO :usage_success
>&2 ECHO ERROR: unknown option: %~1
GOTO :usage_error

:args_done
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

:usage_success
CALL :usage
EXIT /B 0

:usage_error
CALL :usage
EXIT /B 1

:usage
ECHO Usage: RebuildAll.bat [/config CONFIG]
ECHO.
ECHO Options:
ECHO   /config CONFIG   Build configuration ^(default: Release^).
ECHO(  /?, /help        Show this help.
EXIT /B 0

:fail
echo.
echo BUILD FAILED (%CONFIG%^|x64).
EXIT /B 1
