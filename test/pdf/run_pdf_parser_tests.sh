#!/usr/bin/env bash
# Build and run host-side tests against lib/Pdf (real parser sources).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

OUT_DIR="test/pdf/build"
mkdir -p "$OUT_DIR"

CXX="${CXX:-g++}"
STD="${CXXSTD:--std=c++20}"
COMMON_FLAGS=()
if [[ "$(uname -s)" == "Darwin" ]] && command -v sw_vers >/dev/null 2>&1; then
  MACOS_VERSION="$(sw_vers -productVersion | awk -F. '{print $1 ".0"}')"
  COMMON_FLAGS+=("-mmacosx-version-min=${MACOS_VERSION}")
fi

# Stub HalStorage must appear before lib/hal on the include path.
INCLUDES=(
  -I "test/pdf/stubs"
  -I "lib"
  -I "lib/Serialization"
  -I "lib/Pdf"
  -I "lib/Pdf/Pdf"
  -I "src/activities/reader"
  -I "lib/InflateReader"
  -I "lib/uzlib/src"
)

CC="${CC:-cc}"
$CC -c -O2 "${COMMON_FLAGS[@]}" -I "lib/uzlib/src" "lib/uzlib/src/tinflate.c" -o "$OUT_DIR/tinflate.o"
$CC -c -O2 "${COMMON_FLAGS[@]}" "test/pdf/stubs/uzlib_checksum_stubs.c" -o "$OUT_DIR/uzlib_checksum_stubs.o"

SOURCES=(
  "lib/Pdf/Pdf/PdfCachedPageReader.cpp"
  "lib/Pdf/Pdf/XrefTable.cpp"
  "lib/Pdf/Pdf/PdfObject.cpp"
  "lib/Pdf/Pdf/PageTree.cpp"
  "lib/Pdf/Pdf/StreamDecoder.cpp"
  "lib/Pdf/Pdf/ContentStream.cpp"
  "lib/Pdf/Pdf/PdfOutline.cpp"
  "lib/InflateReader/InflateReader.cpp"
  "test/pdf/pdf_parser_host_test.cpp"
)

$CXX $STD -Wall -Wextra -O2 "${COMMON_FLAGS[@]}" "${INCLUDES[@]}" "${SOURCES[@]}" "$OUT_DIR/tinflate.o" "$OUT_DIR/uzlib_checksum_stubs.o" \
  -lz -o "$OUT_DIR/pdf_parser_host_test"

if [[ $# -eq 0 ]]; then
  set -- "$ROOT/test/pdf/sample.pdf" "$ROOT/test/pdf/EE-366.pdf" "$ROOT/test/pdf/esp32-c6_datasheet_en.pdf" \
    "$ROOT/test/pdf/Problem-Solving Treatment_ Learning and Pl - IHS.pdf" \
    "$ROOT/test/pdf/Klient Vikingové CZ, spol. s r.o..pdf" \
    "$ROOT/test/pdf/Turtledove_RoadNotTaken.pdf"
fi

echo "Running PDF parser host tests..."
"$OUT_DIR/pdf_parser_host_test" "$@"
