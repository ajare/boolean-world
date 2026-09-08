#!/usr/bin/env bash

set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: ./build_from_scratch.sh [options]

Build Willpower, MassivePolyPusher, and BooleanWorld from clean build trees.

Options:
  --with-mpp-lfs       Download MassivePolyPusher's Git LFS files.
  --with-tests         Build BooleanWorld's test targets.
  --fmod-sdk DIR       Refresh staged audio files from an extracted Linux FMOD SDK.
  --steam-audio-sdk DIR
                        Steam Audio FMOD SDK root (required with --fmod-sdk).
  --config CONFIG      Build configuration (default: Release).
  --build-dir DIR      BooleanWorld build directory, relative to this repository
                       unless absolute (default: build-linux).
  -h, --help           Show this help.

Environment:
  CC, CXX               Select the C and C++ compilers during configuration.
  CMAKE_GENERATOR       Select a CMake generator.
  CMAKE_BUILD_PARALLEL_LEVEL
                        Limit the number of parallel build jobs.
EOF
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

WITH_MPP_LFS=false
WITH_TESTS=false
FMOD_SDK=
STEAM_AUDIO_SDK=
BUILD_TYPE=Release
BUILD_DIR=build-linux

while (($#)); do
    case "$1" in
        --with-mpp-lfs)
            WITH_MPP_LFS=true
            shift
            ;;
        --with-tests)
            WITH_TESTS=true
            shift
            ;;
        --fmod-sdk)
            (($# >= 2)) || fail "--fmod-sdk requires a value"
            FMOD_SDK=$2
            shift 2
            ;;
        --steam-audio-sdk)
            (($# >= 2)) || fail "--steam-audio-sdk requires a value"
            STEAM_AUDIO_SDK=$2
            shift 2
            ;;
        --config)
            (($# >= 2)) || fail "--config requires a value"
            BUILD_TYPE=$2
            shift 2
            ;;
        --build-dir)
            (($# >= 2)) || fail "--build-dir requires a value"
            BUILD_DIR=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1 (run with --help for usage)"
            ;;
    esac
done

command -v git >/dev/null 2>&1 || fail "Git is required but was not found on PATH"
command -v cmake >/dev/null 2>&1 || fail "CMake is required but was not found on PATH"

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
cd "$ROOT_DIR"

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "$ROOT_DIR is not a Git checkout"
[[ -f CMakeLists.txt && -f .gitmodules ]] || fail "run this script from the BooleanWorld checkout"

printf 'Synchronizing and checking out all submodules...\n'
git submodule sync --recursive
git submodule update --init --recursive

WILLPOWER_DIR="$ROOT_DIR/ext/willpower"
WILLPOWER_SCRIPT="$WILLPOWER_DIR/build_from_scratch.sh"
[[ -f "$WILLPOWER_SCRIPT" ]] || fail "Willpower was not checked out correctly"

if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
fi

[[ -n "$BUILD_DIR" && "$BUILD_DIR" != / && "$BUILD_DIR" != "$ROOT_DIR" ]] || \
    fail "refusing to remove unsafe build directory: $BUILD_DIR"
case "$BUILD_DIR/" in
    "$WILLPOWER_DIR/"*)
        fail "BooleanWorld build directory must not be inside ext/willpower"
        ;;
esac

BUILD_DIR_NAME=$(basename -- "$BUILD_DIR")
WILLPOWER_BUILD_DIR="$WILLPOWER_DIR/$BUILD_DIR_NAME"
willpower_args=(--config "$BUILD_TYPE" --build-dir "$BUILD_DIR_NAME")
if [[ "$WITH_MPP_LFS" == true ]]; then
    willpower_args+=(--with-mpp-lfs)
fi
if [[ -n "$FMOD_SDK" || -n "$STEAM_AUDIO_SDK" ]]; then
    [[ -n "$FMOD_SDK" && -n "$STEAM_AUDIO_SDK" ]] || \
        fail "--fmod-sdk and --steam-audio-sdk must be supplied together"
    FMOD_SDK=$(cd -- "$FMOD_SDK" && pwd -P) || fail "FMOD SDK directory does not exist"
    STEAM_AUDIO_SDK=$(cd -- "$STEAM_AUDIO_SDK" && pwd -P) || fail "Steam Audio SDK directory does not exist"

    printf 'Refreshing vendored Linux audio SDK files...\n'
    install -m 0644 "$FMOD_SDK"/api/core/inc/* vendor/include/fmod/core/
    install -m 0644 "$FMOD_SDK"/api/studio/inc/* vendor/include/fmod/studio/
    install -m 0644 "$STEAM_AUDIO_SDK/lib/linux-x64/libphonon.so" vendor/lib/linux/x64/Release/
    install -m 0644 "$STEAM_AUDIO_SDK/lib/linux-x64/libphonon_fmod.so" vendor/lib/linux/x64/Release/
    install -m 0755 "$FMOD_SDK/api/core/lib/x86_64/libfmod.so" vendor/lib/linux/x64/Release/libfmod.so.14.14
    install -m 0755 "$FMOD_SDK/api/studio/lib/x86_64/libfmodstudio.so" vendor/lib/linux/x64/Release/libfmodstudio.so.14.14
fi

printf 'Building Willpower and MassivePolyPusher from scratch...\n'
bash "$WILLPOWER_SCRIPT" "${willpower_args[@]}"

# Reconfigure the freshly-created Willpower tree against the same staged FMOD
# files Boolean World imports.
cmake -S "$WILLPOWER_DIR" -B "$WILLPOWER_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DWILLPOWER_ENABLE_FMOD=ON \
    -DWILLPOWER_FMOD_CORE_INCLUDE="$ROOT_DIR/vendor/include/fmod/core" \
    -DWILLPOWER_FMOD_STUDIO_INCLUDE="$ROOT_DIR/vendor/include/fmod/studio" \
    -DWILLPOWER_FMOD_CORE_LIBRARY="$ROOT_DIR/vendor/lib/linux/x64/Release/libfmod.so" \
    -DWILLPOWER_FMOD_STUDIO_LIBRARY="$ROOT_DIR/vendor/lib/linux/x64/Release/libfmodstudio.so"
cmake --build "$WILLPOWER_BUILD_DIR" --config "$BUILD_TYPE" \
    --parallel --target Willpower.Application

printf 'Removing previous BooleanWorld build output...\n'
rm -rf -- "$BUILD_DIR"

if [[ "$WITH_TESTS" == true ]]; then
    BUILD_TESTING=ON
else
    BUILD_TESTING=OFF
fi

printf 'Configuring BooleanWorld %s build in %s...\n' "$BUILD_TYPE" "$BUILD_DIR"
configure_args=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DBUILD_TESTING="$BUILD_TESTING"
    -DBW_BUILD_WILLPOWER=OFF)
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${configure_args[@]}"

printf 'Building BooleanWorld...\n'
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

printf 'Build completed successfully.\n'
printf 'Launcher: %s/bin/%s/Launcher/Launcher\n' "$BUILD_DIR" "$BUILD_TYPE"
printf 'Editor:   %s/bin/%s/editor/editor\n' "$BUILD_DIR" "$BUILD_TYPE"
