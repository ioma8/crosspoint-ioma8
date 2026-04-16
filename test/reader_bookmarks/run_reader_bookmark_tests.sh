#!/usr/bin/env bash
set -euo pipefail

mkdir -p test/reader_bookmarks/build
c++ -std=c++20 -Wall -Wextra -Werror -I. -Isrc -Ilib/ReaderCore \
  test/reader_bookmarks/reader_bookmarks_host_test.cpp \
  lib/ReaderCore/ReaderBookmarkCodec.cpp \
  -o test/reader_bookmarks/build/reader_bookmarks_host_test

test/reader_bookmarks/build/reader_bookmarks_host_test
