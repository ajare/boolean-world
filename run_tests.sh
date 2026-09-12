#!/usr/bin/env bash

set -Eeuo pipefail

usage() {
    cat <<'EOF'
Usage: ./run_tests.sh [options]

Incrementally build BooleanWorld's Linux tests when needed, then run all tests.

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

[[ "$(uname -s)" == Linux ]] || fail "run_tests.sh supports Linux builds only"
command -v cmake >/dev/null 2>&1 || fail "CMake is required but was not found on PATH"
command -v ctest >/dev/null 2>&1 || fail "CTest is required but was not found on PATH"

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
fi

CACHE="$BUILD_DIR/CMakeCache.txt"
[[ -f "$CACHE" ]] || \
    fail "no configured build tree at $BUILD_DIR; run build_from_scratch.sh --with-tests first"

CACHE_SOURCE_DIR=$(awk -F= '$1 == "CMAKE_HOME_DIRECTORY:INTERNAL" { print $2; exit }' "$CACHE")
[[ -n "$CACHE_SOURCE_DIR" ]] || fail "could not determine the source directory from $CACHE"
CACHE_SOURCE_DIR=$(cd -- "$CACHE_SOURCE_DIR" && pwd -P)
[[ "$CACHE_SOURCE_DIR" == "$ROOT_DIR" ]] || \
    fail "$BUILD_DIR was configured for $CACHE_SOURCE_DIR, not $ROOT_DIR"

# An existing application-only build has no test targets. Enable them in the
# same build tree before asking the incremental build to bring stale outputs
# up to date.
if ! grep -q '^BUILD_TESTING:BOOL=ON$' "$CACHE"; then
    printf 'Enabling test targets in %s...\n' "$BUILD_DIR"
    configure_args=(-DBUILD_TESTING=ON)
    if grep -q '^CMAKE_BUILD_TYPE:' "$CACHE"; then
        configure_args+=("-DCMAKE_BUILD_TYPE=$CONFIG")
    fi
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${configure_args[@]}"
fi

"$ROOT_DIR/build_incremental.sh" --config "$CONFIG" --build-dir "$BUILD_DIR"

printf 'Running all BooleanWorld tests...\n'
ctest --test-dir "$BUILD_DIR" --build-config "$CONFIG" --output-on-failure
