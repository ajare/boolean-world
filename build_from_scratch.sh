#!/usr/bin/env bash

set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: ./build_from_scratch.sh [options]

Build Willpower, MassivePolyPusher, and BooleanWorld from clean build trees.

Options:
  --with-mpp-lfs       Download MassivePolyPusher's Git LFS files.
  --with-tests         Build BooleanWorld's test targets.
  --build-type TYPE    CMake build type (default: Release).
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
        --build-type)
            (($# >= 2)) || fail "--build-type requires a value"
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

WILLPOWER_SCRIPT="$ROOT_DIR/ext/willpower/build_from_scratch.sh"
[[ -f "$WILLPOWER_SCRIPT" ]] || fail "Willpower was not checked out correctly"

willpower_args=(--build-type "$BUILD_TYPE" --build-dir build)
if [[ "$WITH_MPP_LFS" == true ]]; then
    willpower_args+=(--with-mpp-lfs)
fi

printf 'Building Willpower and MassivePolyPusher from scratch...\n'
bash "$WILLPOWER_SCRIPT" "${willpower_args[@]}"

if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
fi

[[ -n "$BUILD_DIR" && "$BUILD_DIR" != / && "$BUILD_DIR" != "$ROOT_DIR" ]] || \
    fail "refusing to remove unsafe build directory: $BUILD_DIR"
case "$BUILD_DIR/" in
    "$ROOT_DIR/ext/willpower/"*)
        fail "BooleanWorld build directory must not be inside ext/willpower"
        ;;
esac

printf 'Removing previous BooleanWorld build output...\n'
rm -rf -- "$BUILD_DIR"

if [[ "$WITH_TESTS" == true ]]; then
    BUILD_TESTING=ON
else
    BUILD_TESTING=OFF
fi

printf 'Configuring BooleanWorld %s build in %s...\n' "$BUILD_TYPE" "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DBUILD_TESTING="$BUILD_TESTING" \
    -DBW_BUILD_WILLPOWER=OFF

printf 'Building BooleanWorld...\n'
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

printf 'Build completed successfully.\n'
printf 'Launcher: %s/bin/%s/Launcher/Launcher\n' "$BUILD_DIR" "$BUILD_TYPE"
printf 'Editor:   %s/bin/%s/editor/editor\n' "$BUILD_DIR" "$BUILD_TYPE"
