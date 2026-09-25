@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "BUILD_TYPE=Release"
set "BUILD_DIR=build-windows"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="/config" (
    if "%~2"=="" (
        set "ERROR_MESSAGE=/config requires a value"
        goto fatal
    )
    set "BUILD_TYPE=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="/build-dir" (
    if "%~2"=="" (
        set "ERROR_MESSAGE=/build-dir requires a value"
        goto fatal
    )
    set "BUILD_DIR=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="/?" goto usage_success
if /i "%~1"=="/help" goto usage_success
set "ERROR_MESSAGE=unknown option: %~1 (run with /? for usage)"
goto fatal

:args_done
where cmake >nul 2>&1
if errorlevel 1 (
    set "ERROR_MESSAGE=CMake is required but was not found on PATH"
    goto fatal
)

for %%I in ("%~dp0.") do set "ROOT_DIR=%%~fI"
pushd "%ROOT_DIR%"
if errorlevel 1 (
    set "ERROR_MESSAGE=could not enter repository directory: %ROOT_DIR%"
    goto fatal
)
set "PUSHD_DONE=true"

for %%I in ("%BUILD_DIR%") do set "BUILD_DIR=%%~fI"
set "CACHE=%BUILD_DIR%\CMakeCache.txt"
if not exist "%CACHE%" (
    set "ERROR_MESSAGE=no configured build tree at %BUILD_DIR%; run build_from_scratch.bat first"
    goto fatal
)

set "CACHE_SOURCE_DIR="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_HOME_DIRECTORY:INTERNAL=" "%CACHE%"') do set "CACHE_SOURCE_DIR=%%B"
if not defined CACHE_SOURCE_DIR (
    set "ERROR_MESSAGE=could not determine the source directory from %CACHE%"
    goto fatal
)
for %%I in ("%CACHE_SOURCE_DIR%") do set "CACHE_SOURCE_DIR=%%~fI"
if /i not "%CACHE_SOURCE_DIR%"=="%ROOT_DIR%" (
    set "ERROR_MESSAGE=%BUILD_DIR% was configured for %CACHE_SOURCE_DIR%, not %ROOT_DIR%"
    goto fatal
)

set "GENERATOR="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" "%CACHE%"') do set "GENERATOR=%%B"
if not defined GENERATOR (
    set "ERROR_MESSAGE=could not determine the CMake generator from %CACHE%"
    goto fatal
)

rem Visual Studio exposes CMake's build-system regeneration check as ZERO_CHECK.
rem Other generators provide the same behavior without that target name, so run
rem an explicit configure pass as their portable equivalent.
if /i "%GENERATOR:~0,13%"=="Visual Studio" goto regenerate_visual_studio

echo Checking and regenerating CMake build files ^(ZERO_CHECK equivalent^)...
findstr /b /c:"CMAKE_BUILD_TYPE:" "%CACHE%" >nul
if errorlevel 1 (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%"
) else (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%"
)
if errorlevel 1 (
    set "ERROR_MESSAGE=CMake build-system regeneration failed"
    goto fatal
)
goto build

:regenerate_visual_studio
echo Checking CMake build files with ZERO_CHECK...
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --target ZERO_CHECK
if errorlevel 1 (
    set "ERROR_MESSAGE=ZERO_CHECK failed"
    goto fatal
)

:build
echo Building BooleanWorld incrementally...
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --parallel
if errorlevel 1 (
    set "ERROR_MESSAGE=incremental build failed"
    goto fatal
)

echo Incremental build completed successfully.
popd
exit /b 0

:usage_success
call :usage
exit /b 0

:usage
echo Usage: build_incremental.bat [options]
echo.
echo Regenerate the existing BooleanWorld build system, then build it incrementally.
echo Only missing or stale outputs are rebuilt.
echo.
echo Options:
echo   /config CONFIG      Build configuration ^(default: Release^).
echo   /build-dir DIR      Existing BooleanWorld build directory, relative to this
echo                       repository unless absolute ^(default: build-windows^).
echo(  /?, /help           Show this help.
echo.
echo Environment:
echo   CMAKE_BUILD_PARALLEL_LEVEL
echo                       Limit the number of parallel build jobs.
exit /b 0

:fatal
if defined PUSHD_DONE popd
>&2 <nul set /p "=error: %ERROR_MESSAGE%"
>&2 echo.
exit /b 1
