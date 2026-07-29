#!/usr/bin/env bash
# cwb build. Qt is located by cmake/QtDiscovery.cmake (no env needed).
# CES_SRC=<path> builds against a local ces checkout instead of the pinned
# remote (FetchContent SOURCE_DIR override) -- needed while depending on ces
# features that are not yet in the pin.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="$here/build"
extra=()
[ -n "${CES_SRC:-}" ] && extra+=("-DFETCHCONTENT_SOURCE_DIR_CES=${CES_SRC}")
cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE="${1:-Debug}" "${extra[@]}"
cmake --build "$build" -j
