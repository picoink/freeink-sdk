#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d "${TMPDIR:-/tmp}/freeink-plus-board.XXXXXX")
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -DFREEINK_DEVICE_METALIO_EINK4_PLUS=1 \
  -I../../../InputManager/test/host/metalio_stubs -I../../include test_metalio_plus.cpp -o "$build/test"
"$build/test"
