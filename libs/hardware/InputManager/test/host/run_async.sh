#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d "${TMPDIR:-/tmp}/freeink-input-async.XXXXXX")
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -pthread \
  -DFREEINK_DEVICE_METALIO_EINK4=1 -DFREEINK_INPUT_ASYNC_HOST=1 \
  -Iasync_stubs -Imetalio_stubs -I../../include -I../../../BoardConfig/include \
  test_async.cpp ../../src/InputManager.cpp -o "$build/test"
"$build/test"
