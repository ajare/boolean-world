@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "WITH_MPP_LFS=false"
set "WITH_TESTS=false"
set "BUILD_TYPE=Release"
set "BUILD_DIR=build-windows"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="/with-mpp-lfs" (
    set "WITH_MPP_LFS=true"
    shift
    goto parse_args
)
if /i "%~1"=="/with-tests" (
    set "WITH_TESTS=true"
    shift
    goto parse_args
)
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
where git >nul 2>&1
if errorlevel 1 (
    set "ERROR_MESSAGE=Git is required but was not found on PATH"
    goto fatal
)
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

git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    set "ERROR_MESSAGE=%ROOT_DIR% is not a Git checkout"
    goto fatal
)
if not exist "CMakeLists.txt" (
    set "ERROR_MESSAGE=run this script from the BooleanWorld checkout"
    goto fatal
)
if not exist ".gitmodules" (
    set "ERROR_MESSAGE=run this script from the BooleanWorld checkout"
    goto fatal
)

echo Synchronizing and checking out all submodules...
git submodule sync --recursive
if errorlevel 1 (
    set "ERROR_MESSAGE=failed to synchronize submodules"
    goto fatal
)
git submodule update --init --recursive
if errorlevel 1 (
    set "ERROR_MESSAGE=failed to check out submodules"
    goto fatal
)

set "WILLPOWER_DIR=%ROOT_DIR%\ext\willpower"
if not exist "%WILLPOWER_DIR%\CMakeLists.txt" (
    set "ERROR_MESSAGE=Willpower was not checked out correctly"
    goto fatal
)
set "MPP_DIR=%WILLPOWER_DIR%\ext\massive-poly-pusher"
if not exist "%MPP_DIR%\CMakeLists.txt" (
    set "ERROR_MESSAGE=MassivePolyPusher was not checked out correctly"
    goto fatal
)

if not defined BUILD_DIR (
    set "ERROR_MESSAGE=refusing to remove an empty build directory"
    goto fatal
)
for %%I in ("%BUILD_DIR%") do (
    set "BUILD_DIR=%%~fI"
    set "BUILD_DIR_NAME=%%~nxI"
)
set "WILLPOWER_BUILD_DIR=%WILLPOWER_DIR%\%BUILD_DIR_NAME%"
set "MPP_BUILD_DIR=%MPP_DIR%\%BUILD_DIR_NAME%"
if /i "%BUILD_DIR%"=="%ROOT_DIR%" (
    set "ERROR_MESSAGE=refusing to remove unsafe build directory: %BUILD_DIR%"
    goto fatal
)
for %%I in ("%BUILD_DIR%\..") do set "BUILD_PARENT=%%~fI"
if /i "%BUILD_DIR%"=="%BUILD_PARENT%" (
    set "ERROR_MESSAGE=refusing to remove unsafe build directory: %BUILD_DIR%"
    goto fatal
)

set "CHECK_DIR=%BUILD_DIR%"
:check_protected_directory
if /i "%CHECK_DIR%"=="%WILLPOWER_DIR%" (
    set "ERROR_MESSAGE=BooleanWorld build directory must not be inside ext\willpower"
    goto fatal
)
for %%I in ("%CHECK_DIR%\..") do set "CHECK_PARENT=%%~fI"
if /i "%CHECK_DIR%"=="%CHECK_PARENT%" goto protected_directory_checked
set "CHECK_DIR=%CHECK_PARENT%"
goto check_protected_directory

:protected_directory_checked
if /i "%WITH_MPP_LFS%"=="true" (
    git lfs version >nul 2>&1
    if errorlevel 1 (
        set "ERROR_MESSAGE=/with-mpp-lfs requires Git LFS, but 'git lfs' is not installed"
        goto fatal
    )
    echo Downloading MassivePolyPusher Git LFS files...
    git -C "%MPP_DIR%" lfs install --local
    if errorlevel 1 (
        set "ERROR_MESSAGE=failed to initialize Git LFS for MassivePolyPusher"
        goto fatal
    )
    git -C "%MPP_DIR%" lfs pull
    if errorlevel 1 (
        set "ERROR_MESSAGE=failed to download MassivePolyPusher Git LFS files"
        goto fatal
    )
)

echo Removing previous Willpower and MassivePolyPusher build output...
if exist "%WILLPOWER_BUILD_DIR%" rmdir /s /q "%WILLPOWER_BUILD_DIR%"
if exist "%WILLPOWER_BUILD_DIR%" (
    set "ERROR_MESSAGE=could not remove Willpower build directory: %WILLPOWER_BUILD_DIR%"
    goto fatal
)
if exist "%MPP_BUILD_DIR%" rmdir /s /q "%MPP_BUILD_DIR%"
if exist "%MPP_BUILD_DIR%" (
    set "ERROR_MESSAGE=could not remove MassivePolyPusher build directory: %MPP_BUILD_DIR%"
    goto fatal
)

echo Configuring Willpower build tree...
rem BooleanWorld enables its FMOD-backed audio by default on Windows. Configure
rem the separately-built Willpower DLL with the same backend; otherwise the game
rem compiles Steam Audio support while AudioSystem::getCoreSystem() remains the
rem no-op implementation and Launcher fails on entering Play.
set "FMOD_INCLUDE_DIR=%ROOT_DIR%\vendor\include\fmod"
set "FMOD_LIB_DIR=%ROOT_DIR%\vendor\lib\vs2026\x64\Release"
set "FMOD_BIN_DIR=%ROOT_DIR%\vendor\bin\vs2026\x64\Release"
cmake -S "%WILLPOWER_DIR%" -B "%WILLPOWER_BUILD_DIR%" ^
    -DWILLPOWER_ENABLE_FMOD=ON ^
    -DWILLPOWER_FMOD_CORE_INCLUDE="%FMOD_INCLUDE_DIR%\core" ^
    -DWILLPOWER_FMOD_STUDIO_INCLUDE="%FMOD_INCLUDE_DIR%\studio" ^
    -DWILLPOWER_FMOD_CORE_LIBRARY="%FMOD_LIB_DIR%\fmod_vc.lib" ^
    -DWILLPOWER_FMOD_STUDIO_LIBRARY="%FMOD_LIB_DIR%\fmodstudio_vc.lib" ^
    -DWILLPOWER_FMOD_CORE_DLL="%FMOD_BIN_DIR%\fmod.dll" ^
    -DWILLPOWER_FMOD_STUDIO_DLL="%FMOD_BIN_DIR%\fmodstudio.dll"
if errorlevel 1 (
    set "ERROR_MESSAGE=Willpower CMake configuration failed"
    goto fatal
)
set "MULTI_CONFIG=false"
findstr /b /c:"CMAKE_CONFIGURATION_TYPES:" "%WILLPOWER_BUILD_DIR%\CMakeCache.txt" >nul
if not errorlevel 1 set "MULTI_CONFIG=true"
if /i "%MULTI_CONFIG%"=="false" (
    echo Selecting Willpower %BUILD_TYPE% build type...
    cmake -S "%WILLPOWER_DIR%" -B "%WILLPOWER_BUILD_DIR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%"
    if errorlevel 1 (
        set "ERROR_MESSAGE=Willpower CMake configuration failed"
        goto fatal
    )
)
if /i "%MULTI_CONFIG%"=="false" (
    rem Building Willpower first runs its ExternalProject configure step, which
    rem creates MassivePolyPusher's independent CMake build directory.
    echo Building Willpower %BUILD_TYPE%...
    cmake --build "%WILLPOWER_BUILD_DIR%" --config "%BUILD_TYPE%" --parallel
    if errorlevel 1 (
        set "ERROR_MESSAGE=Willpower build failed"
        goto fatal
    )
    echo Building MassivePolyPusher support %BUILD_TYPE%...
    cmake --build "%MPP_BUILD_DIR%" --config "%BUILD_TYPE%" --parallel --target MppAppSupport
    if errorlevel 1 (
        set "ERROR_MESSAGE=MassivePolyPusher support build failed"
        goto fatal
    )
) else (
    rem BooleanWorld's Visual Studio solution contains all configurations, so
    rem its configure step validates every underlying dependency configuration
    rem regardless of which configuration this invocation will ultimately build.
    rem MemCheck reuses Debug, while Shipping has dedicated dependency binaries.
    rem Willpower must build first to configure MassivePolyPusher's build tree.
    echo Building Willpower Debug...
    cmake --build "%WILLPOWER_BUILD_DIR%" --config Debug --parallel
    if errorlevel 1 (
        set "ERROR_MESSAGE=Willpower Debug build failed"
        goto fatal
    )
    echo Building MassivePolyPusher support Debug...
    cmake --build "%MPP_BUILD_DIR%" --config Debug --parallel --target MppAppSupport
    if errorlevel 1 (
        set "ERROR_MESSAGE=MassivePolyPusher Debug support build failed"
        goto fatal
    )
    echo Building Willpower Release...
    cmake --build "%WILLPOWER_BUILD_DIR%" --config Release --parallel
    if errorlevel 1 (
        set "ERROR_MESSAGE=Willpower Release build failed"
        goto fatal
    )
    echo Building MassivePolyPusher support Release...
    cmake --build "%MPP_BUILD_DIR%" --config Release --parallel --target MppAppSupport
    if errorlevel 1 (
        set "ERROR_MESSAGE=MassivePolyPusher Release support build failed"
        goto fatal
    )
    echo Building Willpower Shipping...
    cmake --build "%WILLPOWER_BUILD_DIR%" --config Shipping --parallel
    if errorlevel 1 (
        set "ERROR_MESSAGE=Willpower Shipping build failed"
        goto fatal
    )
    echo Building MassivePolyPusher support Shipping...
    cmake --build "%MPP_BUILD_DIR%" --config Shipping --parallel --target MppAppSupport
    if errorlevel 1 (
        set "ERROR_MESSAGE=MassivePolyPusher Shipping support build failed"
        goto fatal
    )
)

echo Removing previous BooleanWorld build output...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" (
    set "ERROR_MESSAGE=could not remove build directory: %BUILD_DIR%"
    goto fatal
)

if /i "%WITH_TESTS%"=="true" (
    set "BUILD_TESTING=ON"
) else (
    set "BUILD_TESTING=OFF"
)

echo Configuring BooleanWorld %BUILD_TYPE% build in %BUILD_DIR%...
if /i "%MULTI_CONFIG%"=="true" (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DBUILD_TESTING="%BUILD_TESTING%" -DBW_BUILD_WILLPOWER=OFF
) else (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" -DBUILD_TESTING="%BUILD_TESTING%" -DBW_BUILD_WILLPOWER=OFF
)
if errorlevel 1 (
    set "ERROR_MESSAGE=CMake configuration failed"
    goto fatal
)

echo Building BooleanWorld...
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --parallel
if errorlevel 1 (
    set "ERROR_MESSAGE=build failed"
    goto fatal
)

set "LAUNCHER_EXE=Launcher.exe"
if /i "%BUILD_TYPE%"=="Debug" set "LAUNCHER_EXE=Launcherd.exe"
echo Build completed successfully.
echo Launcher: %BUILD_DIR%\bin\%BUILD_TYPE%\Launcher\%LAUNCHER_EXE%
echo Editor:   %BUILD_DIR%\bin\%BUILD_TYPE%\editor\editor.exe
popd
exit /b 0

:usage_success
call :usage
exit /b 0

:usage
echo Usage: build_from_scratch.bat [options]
echo.
echo Build Willpower, MassivePolyPusher, and BooleanWorld from clean build trees.
echo.
echo Options:
echo   /with-mpp-lfs       Download MassivePolyPusher's Git LFS files.
echo   /with-tests         Build BooleanWorld's test targets.
echo   /config CONFIG      Build configuration ^(default: Release^).
echo   /build-dir DIR      BooleanWorld build directory, relative to this repository
echo                       unless absolute ^(default: build-windows^).
echo(  /?, /help           Show this help.
echo.
echo Environment:
echo   CC, CXX               Select the C and C++ compilers during configuration.
echo   CMAKE_GENERATOR       Select a CMake generator.
echo   CMAKE_BUILD_PARALLEL_LEVEL
echo                         Limit the number of parallel build jobs.
exit /b 0

:fatal
if defined PUSHD_DONE popd
>&2 <nul set /p "=error: %ERROR_MESSAGE%"
>&2 echo.
exit /b 1
