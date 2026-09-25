#!/usr/bin/env bash

set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: ./build_incremental.sh [options]

Regenerate the existing BooleanWorld build system, then build it incrementally.
Only missing or stale outputs are rebuilt.

Options:
  --config CONFIG    Build configuration (default: Release).
  --build-dir DIR    Existing BooleanWorld build directory, relative to this
                     repository unless absolute (default: build-linux).
  -h, --help         Show this help.

Environment:
  CMAKE_BUILD_PARALLEL_LEVEL
                     Limit the number of parallel build jobs.
EOF
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

CONFIG=Release
BUILD_DIR=build-linux

while (($#)); do
    case "$1" in
        --config)
            (($# >= 2)) || fail "--config requires a value"
            CONFIG=$2
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

command -v cmake >/dev/null 2>&1 || fail "CMake is required but was not found on PATH"

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
fi

CACHE="$BUILD_DIR/CMakeCache.txt"
[[ -f "$CACHE" ]] || \
    fail "no configured build tree at $BUILD_DIR; run build_from_scratch.sh first"

CACHE_SOURCE_DIR=$(awk -F= '$1 == "CMAKE_HOME_DIRECTORY:INTERNAL" { print $2; exit }' "$CACHE")
[[ -n "$CACHE_SOURCE_DIR" ]] || fail "could not determine the source directory from $CACHE"
CACHE_SOURCE_DIR=$(cd -- "$CACHE_SOURCE_DIR" && pwd -P)
[[ "$CACHE_SOURCE_DIR" == "$ROOT_DIR" ]] || \
    fail "$BUILD_DIR was configured for $CACHE_SOURCE_DIR, not $ROOT_DIR"

GENERATOR=$(awk -F= '$1 == "CMAKE_GENERATOR:INTERNAL" { print $2; exit }' "$CACHE")

# Visual Studio exposes CMake's build-system regeneration check as ZERO_CHECK.
# Other generators provide the same behavior without that target name, so run
# an explicit configure pass as their portable equivalent.
if [[ "$GENERATOR" == Visual\ Studio* ]]; then
    printf 'Checking CMake build files with ZERO_CHECK...\n'
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target ZERO_CHECK
else
    printf 'Checking and regenerating CMake build files (ZERO_CHECK equivalent)...\n'
    configure_args=()
    if grep -q '^CMAKE_BUILD_TYPE:' "$CACHE"; then
        configure_args+=("-DCMAKE_BUILD_TYPE=$CONFIG")
    fi
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${configure_args[@]}"
fi

printf 'Building BooleanWorld incrementally...\n'
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel

printf 'Incremental build completed successfully.\n'
