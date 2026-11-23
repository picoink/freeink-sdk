#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d "${TMPDIR:-/tmp}/freeink-sd-native.XXXXXX")
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -Inative_stubs \
  -I../../../UsbMassStorage/test/host/stubs -I../../src \
  test_native.cpp ../../src/SdmmcBlockDevice.cpp -o "$build/test"
"$build/test"
