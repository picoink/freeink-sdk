#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d "${TMPDIR:-/tmp}/freeink-msc.XXXXXX")
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -pthread -Istubs -I../../include \
  test_msc.cpp ../../src/UsbMassStorage.cpp -o "$build/test"
"$build/test"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -pthread -DFREEINK_DEVICE_METALIO_EINK4_PLUS=1 -Istubs -I../../include \
  test_msc.cpp ../../src/UsbMassStorage.cpp -o "$build/metalio"
"$build/metalio"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -DFREEINK_INTERNAL_FAT=1 -Istubs -I../../include \
  test_flash.cpp ../../src/FlashUsbStorage.cpp -o "$build/flash"
"$build/flash"
