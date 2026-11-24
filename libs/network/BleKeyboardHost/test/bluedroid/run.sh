#!/bin/sh
set -eu
cd "$(dirname "$0")"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/freeink-bluedroid.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -Istubs -I../../include \
  lifecycle.cpp ../../src/BleKeyboardHost.cpp ../../src/HidKeymap.cpp -o "$BUILD_DIR/test"
"$BUILD_DIR/test"
