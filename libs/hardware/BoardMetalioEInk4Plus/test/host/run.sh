#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d "${TMPDIR:-/tmp}/freeink-waveform.XXXXXX")
trap 'rm -rf "$build"' EXIT
${CC:-cc} -std=c11 -Istubs -I../../src/waveform -I../../../../display/epdiy/src \
  waveform.c ../../src/waveform/my_waveform.c ../../src/waveform/my_waveform_trim.c -o "$build/test"
"$build/test" > "$build/actual"
diff -u waveform.expected "$build/actual"
echo 'Waveform profile hashes match the pre-migration baseline'
