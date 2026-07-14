#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build-windows}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
GUI_OPTION="${STL_SLICER_BUILD_GUI:-OFF}"
TEST_OPTION="${STL_SLICER_BUILD_TESTS:-OFF}"
ARCH_OPTION="${STL_SLICER_ARCH:-}"

if [[ -z "${BIN:-}" ]]; then
    BIN="$HOME/my_msvc/bin/x64"
fi

export CCACHE_DISABLE=1

# shellcheck source=/dev/null
. "$HOME/msvc-wine/msvcenv-native.sh"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/windows-clang-msvc.cmake" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DSTL_SLICER_BUILD_GUI="$GUI_OPTION" \
    -DSTL_SLICER_BUILD_TESTS="$TEST_OPTION" \
    ${ARCH_OPTION:+-DSTL_SLICER_ARCH="$ARCH_OPTION"}

cmake --build "$BUILD_DIR"
