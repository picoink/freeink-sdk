#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d "${TMPDIR:-/tmp}/freeink-msc.XXXXXX")
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -pthread -Istubs -I../../include \
  test_msc.cpp ../../src/UsbMassStorage.cpp -o "$build/test"
"$build/test"
