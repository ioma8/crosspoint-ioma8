#!/usr/bin/env bash
set -euo pipefail

ARDUINOJSON_DIR=".pio/libdeps/default/ArduinoJson/src"
if [[ ! -d "$ARDUINOJSON_DIR" ]]; then
  echo "ArduinoJson dependency not found at $ARDUINOJSON_DIR; run pio run first" >&2
  exit 1
fi

mkdir -p test/koreader_credentials/build
c++ -std=c++20 -Wall -Wextra -Werror \
  -Itest/koreader_credentials/stubs \
  -Ilib/KOReaderSync \
  -I"$ARDUINOJSON_DIR" \
  test/koreader_credentials/koreader_credentials_host_test.cpp \
  lib/KOReaderSync/KOReaderCredentialCodec.cpp \
  -o test/koreader_credentials/build/koreader_credentials_host_test

test/koreader_credentials/build/koreader_credentials_host_test
