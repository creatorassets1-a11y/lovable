#!/bin/sh
# Builds and runs the platform-independent test suite on the host machine.
set -e
DIR=$(dirname "$0")
OUT=${OUT:-/tmp/hollow_tests}
clang++ -std=c++17 -O2 -DHOLLOW_NO_OPENSL -Wall \
    "$DIR/host_test.cpp" \
    "$DIR/../app/src/main/cpp/world.cpp" \
    "$DIR/../app/src/main/cpp/entity.cpp" \
    "$DIR/../app/src/main/cpp/player.cpp" \
    "$DIR/../app/src/main/cpp/audio.cpp" \
    -o "$OUT"
exec "$OUT"
