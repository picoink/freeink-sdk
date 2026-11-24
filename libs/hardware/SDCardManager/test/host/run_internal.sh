#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$here/../../src" "$here/test_internal.cpp" -o "$build/internal"
"$build/internal"
echo 'Internal file descriptor, flags, directories and repeated opens passed'
