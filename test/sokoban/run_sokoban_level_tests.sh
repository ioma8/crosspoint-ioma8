#!/usr/bin/env bash
set -euo pipefail

mkdir -p test/sokoban/build
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I. \
  test/sokoban/sokoban_levels_host_test.cpp \
  -o test/sokoban/build/sokoban_levels_host_test

test/sokoban/build/sokoban_levels_host_test
