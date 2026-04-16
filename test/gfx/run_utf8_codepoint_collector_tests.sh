#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

OUT_DIR="test/gfx/build"
mkdir -p "$OUT_DIR"

CXX="${CXX:-g++}"
STD="${CXXSTD:--std=c++20}"
COMMON_FLAGS=()
if [[ "$(uname -s)" == "Darwin" ]] && command -v sw_vers >/dev/null 2>&1; then
  MACOS_VERSION="$(sw_vers -productVersion | awk -F. '{print $1 ".0"}')"
  COMMON_FLAGS+=("-mmacosx-version-min=${MACOS_VERSION}")
fi

"$CXX" "$STD" -Wall -Wextra -O2 "${COMMON_FLAGS[@]}" \
  -I "lib/Utf8" -I "lib/GfxRenderer" \
  "lib/Utf8/Utf8.cpp" \
  "lib/GfxRenderer/Utf8CodepointCollector.cpp" \
  "test/gfx/utf8_codepoint_collector_test.cpp" \
  -o "$OUT_DIR/utf8_codepoint_collector_test"

"$OUT_DIR/utf8_codepoint_collector_test"
